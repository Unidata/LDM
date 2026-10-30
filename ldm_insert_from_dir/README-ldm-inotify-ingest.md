# Quick notes on an application to insert data coming into a directory as a replacement for fileIngester.
# Stonie Cooper, NSF-Unidata, 2026

# LDM inotify ingest

Linux-only stand-alone daemon that watches one directory and inserts regular files after `IN_CLOSE_WRITE`. `IN_MOVED_TO` is also accepted so atomic rename-based producers are not missed.

## Dependencies

- A current NSF-Unidata LDM build installed under `LDM_PREFIX`
- tomlc99 (`toml.h` and `libtoml`)
- OpenSSL libcrypto
- Linux inotify

## Build and run

```sh
make LDM_PREFIX=/home/ldm TOMLC99_PREFIX=/usr/local
./ldm-inotify-ingest ldm-inotify-ingest.toml
```

The process needs read/traverse permission for the watched directory and write access to the LDM product queue.

## Notes

- The LDM signature is MD5 over file content, matching normal LDM product uniqueness semantics.
- An empty file is supported.
- Symlink products are rejected through `O_NOFOLLOW`.
- `IN_MOVED_TO` is intentionally handled in addition to `IN_CLOSE_WRITE`.
- Product-ID truncation keeps the right edge of the basename, which tends to retain extensions and trailing timestamps.
- inotify is not recursive. This program watches only the configured directory.
