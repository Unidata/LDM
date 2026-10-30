#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <toml.h>

#include "ldm.h"
#include "pq.h"
#include "timestamp.h"

#ifndef HOSTNAMESIZE
#define HOSTNAMESIZE 256
#endif

#define EVENT_BUFFER_SIZE (64 * 1024)
#define MAX_PRODUCT_ID 1024

typedef enum { SEQ_STATIC, SEQ_RANDOM } seq_mode_t;
typedef enum { ID_FILENAME, ID_TRUNCATED } id_mode_t;

typedef struct {
    char watch_dir[PATH_MAX];
    char queue_path[PATH_MAX];
    char feedtype_text[128];
    feedtypet feedtype;
    seq_mode_t seq_mode;
    uint32_t static_seqno;
    id_mode_t id_mode;
    size_t product_id_max;
    bool include_hidden;
} config_t;

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig) { (void)sig; stop_requested = 1; }

static void fail_toml(const char *key, const char *why) {
    fprintf(stderr, "configuration error for '%s': %s\n", key, why);
    exit(EXIT_FAILURE);
}

static char *required_string(toml_table_t *table, const char *key) {
    toml_datum_t d = toml_string_in(table, key);
    if (!d.ok) fail_toml(key, "missing string");
    return d.u.s;
}

static void copy_checked(char *dst, size_t size, const char *src, const char *key) {
    if (snprintf(dst, size, "%s", src) >= (int)size)
        fail_toml(key, "value is too long");
}

static config_t load_config(const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) { perror(path); exit(EXIT_FAILURE); }

    char errbuf[256];
    toml_table_t *root = toml_parse_file(fp, errbuf, sizeof(errbuf));
    fclose(fp);
    if (!root) { fprintf(stderr, "%s: %s\n", path, errbuf); exit(EXIT_FAILURE); }

    toml_table_t *w = toml_table_in(root, "watch");
    toml_table_t *l = toml_table_in(root, "ldm");
    toml_table_t *s = toml_table_in(root, "sequence");
    toml_table_t *p = toml_table_in(root, "product_id");
    if (!w || !l || !s || !p) fail_toml("tables", "[watch], [ldm], [sequence], and [product_id] are required");

    config_t cfg = { .product_id_max = 255, .include_hidden = false };
    char *v = required_string(w, "directory");
    copy_checked(cfg.watch_dir, sizeof(cfg.watch_dir), v, "watch.directory"); free(v);
    v = required_string(l, "queue_path");
    copy_checked(cfg.queue_path, sizeof(cfg.queue_path), v, "ldm.queue_path"); free(v);
    v = required_string(l, "feedtype");
    copy_checked(cfg.feedtype_text, sizeof(cfg.feedtype_text), v, "ldm.feedtype"); free(v);

    if (strfeedtypet(cfg.feedtype_text, &cfg.feedtype) != 0)
        fail_toml("ldm.feedtype", "not recognized by this LDM build");

    v = required_string(s, "mode");
    if (!strcmp(v, "random")) cfg.seq_mode = SEQ_RANDOM;
    else if (!strcmp(v, "static")) cfg.seq_mode = SEQ_STATIC;
    else fail_toml("sequence.mode", "must be 'random' or 'static'");
    free(v);
    toml_datum_t i = toml_int_in(s, "value");
    if (cfg.seq_mode == SEQ_STATIC) {
        if (!i.ok || i.u.i < 0 || (uint64_t)i.u.i > UINT32_MAX)
            fail_toml("sequence.value", "required for static mode and must fit uint32_t");
        cfg.static_seqno = (uint32_t)i.u.i;
    }

    v = required_string(p, "mode");
    if (!strcmp(v, "filename")) cfg.id_mode = ID_FILENAME;
    else if (!strcmp(v, "truncated")) cfg.id_mode = ID_TRUNCATED;
    else fail_toml("product_id.mode", "must be 'filename' or 'truncated'");
    free(v);
    i = toml_int_in(p, "max_length");
    if (i.ok) {
        if (i.u.i < 1 || i.u.i >= MAX_PRODUCT_ID) fail_toml("product_id.max_length", "out of range");
        cfg.product_id_max = (size_t)i.u.i;
    }
    if (cfg.id_mode == ID_FILENAME) cfg.product_id_max = MAX_PRODUCT_ID - 1;

    toml_datum_t b = toml_bool_in(w, "include_hidden");
    if (b.ok) cfg.include_hidden = b.u.b;
    toml_free(root);
    return cfg;
}

static uint32_t random_u32(void) {
    uint32_t value;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) { perror("/dev/urandom"); exit(EXIT_FAILURE); }
    ssize_t n = read(fd, &value, sizeof(value));
    close(fd);
    if (n != (ssize_t)sizeof(value)) { fprintf(stderr, "short read from /dev/urandom\n"); exit(EXIT_FAILURE); }
    return value;
}

static int md5_data(const void *data, size_t size, unsigned char out[16]) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned int n = 0;
    if (!ctx) return -1;
    int ok = EVP_DigestInit_ex(ctx, EVP_md5(), NULL) == 1 &&
             EVP_DigestUpdate(ctx, data, size) == 1 &&
             EVP_DigestFinal_ex(ctx, out, &n) == 1 && n == 16;
    EVP_MD_CTX_free(ctx);
    return ok ? 0 : -1;
}

static void make_product_id(const config_t *cfg, const char *name, char out[MAX_PRODUCT_ID]) {
    size_t limit = cfg->product_id_max;
    size_t len = strnlen(name, MAX_PRODUCT_ID - 1);
    if (len <= limit) { memcpy(out, name, len); out[len] = '\0'; return; }

    /* Preserve the rightmost characters because timestamps/extensions often occur there. */
    memcpy(out, name + len - limit, limit);
    out[limit] = '\0';
}

static int insert_file(pqueue *pq, const config_t *cfg, const char *name, const char *origin) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", cfg->watch_dir, name) >= (int)sizeof(path)) {
        fprintf(stderr, "path too long: %s/%s\n", cfg->watch_dir, name); return ENAMETOOLONG;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { perror(path); return errno; }
    struct stat st;
    if (fstat(fd, &st) < 0) { int e=errno; perror("fstat"); close(fd); return e; }
    if (!S_ISREG(st.st_mode)) { close(fd); return 0; }

    void *data = NULL;
    if (st.st_size > 0) {
        data = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) { int e=errno; perror("mmap"); close(fd); return e; }
    }

    product prod;
    memset(&prod, 0, sizeof(prod));
    char ident[MAX_PRODUCT_ID];
    make_product_id(cfg, name, ident);
    set_timestamp(&prod.info.arrival);
    prod.info.feedtype = cfg->feedtype;
    prod.info.seqno = cfg->seq_mode == SEQ_RANDOM ? random_u32() : cfg->static_seqno;
    prod.info.origin = (char *)origin;
    prod.info.ident = ident;
    prod.info.sz = (u_int)st.st_size;
    prod.data = data;

    unsigned char digest[16];
    static const unsigned char empty = 0;
    if (md5_data(st.st_size ? data : &empty, (size_t)st.st_size, digest) < 0) {
        fprintf(stderr, "MD5 calculation failed for %s\n", path);
        if (data) munmap(data, (size_t)st.st_size); close(fd); return EIO;
    }
    memcpy(prod.info.signature, digest, sizeof(digest));

    int status = pq_insert(pq, &prod);
    if (status == 0)
        fprintf(stdout, "inserted feed=%s seq=%u id=%s bytes=%lld\n",
                cfg->feedtype_text, prod.info.seqno, ident, (long long)st.st_size);
    else if (status == PQUEUE_DUP)
        fprintf(stderr, "duplicate product skipped: %s\n", ident);
    else
        fprintf(stderr, "pq_insert(%s) failed: %s (%d)\n", ident, strerror(status), status);

    if (data) munmap(data, (size_t)st.st_size);
    close(fd);
    return status;
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s CONFIG.toml\n", argv[0]); return EXIT_FAILURE; }
    config_t cfg = load_config(argv[1]);

    struct stat dst;
    if (stat(cfg.watch_dir, &dst) < 0 || !S_ISDIR(dst.st_mode)) {
        fprintf(stderr, "watch directory is invalid: %s\n", cfg.watch_dir); return EXIT_FAILURE;
    }

    pqueue *pq = NULL;
    int status = pq_open(cfg.queue_path, PQ_DEFAULT, &pq);
    if (status != 0) {
        fprintf(stderr, "pq_open(%s) failed: %s (%d)\n", cfg.queue_path, strerror(status), status);
        return EXIT_FAILURE;
    }

    int ifd = inotify_init1(IN_CLOEXEC);
    if (ifd < 0) { perror("inotify_init1"); pq_close(pq); return EXIT_FAILURE; }
    int wd = inotify_add_watch(ifd, cfg.watch_dir, IN_CLOSE_WRITE | IN_MOVED_TO | IN_ONLYDIR);
    if (wd < 0) { perror("inotify_add_watch"); close(ifd); pq_close(pq); return EXIT_FAILURE; }

    struct sigaction sa = { .sa_handler = on_signal };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL);

    char origin[HOSTNAMESIZE];
    if (gethostname(origin, sizeof(origin)) < 0) strcpy(origin, "localhost");
    origin[sizeof(origin)-1] = '\0';
    fprintf(stderr, "watching %s; queue=%s; feed=%s\n", cfg.watch_dir, cfg.queue_path, cfg.feedtype_text);

    char buf[EVENT_BUFFER_SIZE] __attribute__((aligned(__alignof__(struct inotify_event))));
    while (!stop_requested) {
        ssize_t n = read(ifd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read(inotify)"); break;
        }
        for (char *ptr = buf; ptr < buf + n; ) {
            const struct inotify_event *ev = (const struct inotify_event *)ptr;
            if ((ev->mask & IN_Q_OVERFLOW) != 0)
                fprintf(stderr, "inotify queue overflow: one or more files may have been missed\n");
            else if (ev->len && !(ev->mask & IN_ISDIR) &&
                     (ev->mask & (IN_CLOSE_WRITE | IN_MOVED_TO)) &&
                     (cfg.include_hidden || ev->name[0] != '.'))
                (void)insert_file(pq, &cfg, ev->name, origin);
            ptr += sizeof(*ev) + ev->len;
        }
    }

    inotify_rm_watch(ifd, wd);
    close(ifd);
    pq_close(pq);
    return EXIT_SUCCESS;
}
