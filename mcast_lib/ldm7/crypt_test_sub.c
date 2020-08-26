#undef NDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <arpa/inet.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/crypto.h>

HMAC_CTX* HMAC_CTX_new();
void      HMAC_CTX_free();

// Create socket
static int create_socket(int port)
{
	int s = socket(AF_INET, SOCK_STREAM, 0);
	assert(s != -1);

    struct sockaddr_in serv_addr = {
    		.sin_family=AF_INET,
			.sin_port=htons(port),
    		.sin_addr.s_addr=htonl(INADDR_LOOPBACK)
    };
	int status = connect(s, (struct sockaddr*)&serv_addr, sizeof(serv_addr));
	assert(status == 0);

	return s;
}

static void writeMsg(
		const int      sd,
		const uint8_t* bytes,
		const size_t   nbytes)
{
	ssize_t status = write(sd, &nbytes, sizeof(nbytes));
	assert(status == sizeof(nbytes));

	status = write(sd, bytes, nbytes);
	assert(status == nbytes);

	printf("Wrote %zu bytes\n", nbytes);
}

static size_t readMsg(
		const int    sd,
		uint8_t*     bytes)
{
	size_t  nbytes;
	ssize_t status = read(sd, &nbytes, sizeof(nbytes));
	assert(status == sizeof(nbytes));

	status = read(sd, bytes, nbytes);
	assert(status == nbytes);

	printf("Read %zu bytes\n", nbytes);

	return nbytes;
}

int main(int argc, char **argv)
{
	// Argument reminder
	if (argc < 2) {
		printf("Usage: %s [FILE]\n", argv[0]);
		return 1;
	}

	// Create and set up the socket
	int sock = create_socket(50000);
	assert(sock >= 0);
    
	// Create public-private key pair

	/*
	 * RSA_F4: 65537 inline
	 * int BN_set_word(BIGNUM *a, BN_ULONG w);
	 * https://www.openssl.org/docs/man1.1.0/man3/BN_set_word.html
	 * BN_set_word() set a to the values w.
	 */
	BIGNUM* bne = BN_new();
	assert(bne);
	int     intStatus = BN_set_word(bne, RSA_F4);
	assert(intStatus == 1);

	/*
	 * int RSA_generate_key_ex(RSA *rsa, int bits, BIGNUM *e, BN_GENCB *cb);
	 * https://www.openssl.org/docs/man1.0.2/man3/RSA_generate_key_ex.html
	 * RSA_generate_key_ex() generates
	 * 		a key pair and stores it in the RSA structure provided in rsa.
	 */
	RSA* rsa = RSA_new();
	assert(rsa);
	intStatus = RSA_generate_key_ex(rsa, 2048, bne, NULL);
	assert(intStatus == 1);

	// Convert RSA structure to C-string structure
	BIO* pub = BIO_new(BIO_s_mem());
	assert(pub);

	intStatus = PEM_write_bio_RSAPublicKey(pub, rsa);
	assert(intStatus);

	/*
	 * int BIO_pending(BIO *b);
	 * https://www.openssl.org/docs/man1.1.0/man3/BIO_pending.html
	 * return the number of pending characters in the BIOs read and write
	 * buffers.
	 */
	size_t pubKeyLen = BIO_pending(pub);
	assert(pubKeyLen);
	char*  pubKey = malloc(pubKeyLen + 1);
	assert(pubKey);
	intStatus = BIO_read(pub, pubKey, pubKeyLen);
	assert(intStatus == pubKeyLen);
	pubKey[pubKeyLen] = 0; // NEW

	intStatus = printf("%s\n", pubKey);
	assert(intStatus == pubKeyLen + 1);

	// Clean up
	BIO_free_all(pub);
	BN_free(bne);
    
	// Send the subsriber's NUL-terminated public key string to the publisher
	writeMsg(sock, pubKey, pubKeyLen+1);

	free(pubKey); // Done with public key

	// Read the encrypted HMAC key
	char encryptedHmacKey[1500];
	readMsg(sock, encryptedHmacKey);

	printf("Encrypted HMAC key:\n");
	const int encryptedHmacKeyLen = RSA_size(rsa);
	for (int i = 0; i < encryptedHmacKeyLen; ++i)
		printf("%02X", encryptedHmacKey[i]);
	putchar('\n');

	/* Decrypt the encrypted HMAC key using the subscriber's private key
	 *     int RSA_private_decrypt(int flen, const unsigned char *from,
     * 			unsigned char *to, RSA *rsa, int padding);
	 * https://www.openssl.org/docs/man1.0.2/man3/RSA_private_decrypt.html
	 * RSA_private_decrypt() decrypts the flen bytes at from using the private
	 * key rsa and stores the plaintext in to. to must point to a memory section
	 * large enough to hold the decrypted data (which is smaller than
	 * RSA_size(rsa)). padding is the padding mode that was used to encrypt the
	 * data. returns the size of the recovered plaintext.
	 */
	char* hmacKey = malloc(RSA_size(rsa)); // RSA_size(rsa) > necessary
	assert(hmacKey);

	const int hmacKeyLen = RSA_private_decrypt(RSA_size(rsa), encryptedHmacKey,
			hmacKey, rsa, RSA_PKCS1_OAEP_PADDING);
	assert(hmacKeyLen > 0);

	printf("HMAC key:\n");
	for (int i = 0; i < hmacKeyLen; ++i)
		printf("%02X", hmacKey[i]);
	putchar('\n');

	// Calculate the HMAC based on the provided text file and HMAC key
	HMAC_CTX* hctx = HMAC_CTX_new();
	assert(hctx);

	intStatus = HMAC_Init_ex(hctx, hmacKey, RSA_size(rsa), EVP_sha512(), NULL);
	assert(intStatus == 1);

	uint8_t* subHmac = malloc(HMAC_size(hctx));
	assert(subHmac);
	//printf("HMAC size allocated: %d\n", HMAC_size(hctx));

	uint8_t buf[1462];
	FILE*   f = fopen(argv[1], "r");
	assert(f);
	for (size_t sizeStatus = fread(buf, 1, sizeof(buf), f); sizeStatus;
			sizeStatus = fread(buf, 1, sizeof(buf), f)) {
		intStatus = HMAC_Update(hctx, buf, sizeStatus);
		assert(intStatus == 1);
	}
	assert(feof(f));
	assert(!ferror(f));
	//HMAC_Update(hctx, buf, sizeof(buf)); // Why is this here?
	unsigned subHmacLen = 0;
	intStatus = HMAC_Final(hctx, subHmac, &subHmacLen);
	assert(intStatus == 1);

	printf("Subscriber's HMAC length: %u\n", subHmacLen);
	printf("Subscriber's HMAC: \n");
	for (int i = 0; i < subHmacLen; ++i)
		printf("%02X", subHmac[i]);
	putchar('\n');

	fclose(f);
	HMAC_CTX_free(hctx);
	RSA_free(rsa);

	// Receive the publisher's HMAC
	uint8_t pubHmac[1462];
	size_t  pubHmacLen = readMsg(sock, pubHmac);

	// Verify the received HMAC
	assert(subHmacLen == pubHmacLen);
	intStatus = CRYPTO_memcmp(pubHmac, subHmac, subHmacLen);
	assert(intStatus == 0);
	
	free(hmacKey);
	free(subHmac);

	close(sock);

	return 0;
}
