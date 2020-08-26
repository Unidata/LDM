#undef NDEBUG

#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>

// Create socket
int create_socket(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s >= 0);

    int yes = 1;
    int status = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    assert(status == 0);

    struct sockaddr_in addr = {
    		.sin_family      = AF_INET,
			.sin_port        = htons(port),
    		.sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    status = bind(s, (struct sockaddr*)&addr, sizeof(addr));
    assert(status == 0);

    status = listen(s, 1);
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

EVP_PKEY* EVP_PKEY_new_mac_key(int type, ENGINE *e, const unsigned char *key, int keylen);
HMAC_CTX* HMAC_CTX_new();

int main(int argc, char **argv)
{
	// Argument reminder
	if (argc < 2) {
		printf("Usage: %s [FILE]\n", argv[0]);
		return 1;
	}

	// Create socket
	int       sock = create_socket(50000);
	assert(sock >= 0);

	// Generate session key
	int     sessionKeyLen = 128;
	uint8_t sessionKey[sessionKeyLen];
	int     intStatus = RAND_bytes(sessionKey, sizeof(sessionKey));
	assert(intStatus == 1);
	//printf("initial random bytes: \n");
	//printf("%s\n", random_key);

	/*
	 * EVP_PKEY *EVP_PKEY_new_mac_key(int type, ENGINE *e,
	 *     const unsigned char *key, int keylen);
	 * https://www.openssl.org/docs/man1.1.1/man3/EVP_PKEY_new_mac_key.html
	 * EVP_PKEY_new_mac_key() allocates a new EVP_PKEY.
	 * If e is non-NULL then the new EVP_PKEY structure is associated with the engine e.
	 * The type argument indicates what kind of key this is.
	 * 		The value should be a NID for a public key algorithm that supports raw private keys,
	 * 		i.e. EVP_PKEY_HMAC, EVP_PKEY_POLY1305, EVP_PKEY_SIPHASH, EVP_PKEY_X25519, etc.
	 * key points to the raw private key data for this EVP_PKEY which should be of length keylen.
	 *		The length should be appropriate for the type of the key.
	 */
	EVP_PKEY* secret = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL, sessionKey,
			sessionKeyLen);
	assert(secret);
	//printf("EVP_PKEY: \n");
	//printf("%s\n", secret);

	// Handle connections */
	for (;;) {
		struct sockaddr_in addr;
		unsigned           len = sizeof(addr);
	
		// Accept subscriber's request */
		int subSock = accept(sock, (struct sockaddr*)&addr, &len);
		assert(subSock >= 0);

		// Obtain subscriber's public key */
		char   buff[1500] = {};
		size_t pubKeyLen = readMsg(subSock, buff);

		intStatus = printf("%s\n", buff);
		assert(intStatus == pubKeyLen);

		// Convert the subsriber's public key from C-string to RSA structure */

		/*
		 * BIO *BIO_new_mem_buf(const void *buf, int len);
		 * https://www.openssl.org/docs/man1.0.2/man3/BIO_new_mem_buf.html
		 * BIO_new_mem_buf() creates a memory BIO using len bytes of data at
		 * buf, if len is -1 then the buf is assumed to be nul terminated and
		 * its length is determined by strlen. The BIO is set to a read only
		 * state and as a result cannot be written to. This is useful when some
		 * data needs to be made available from a static area of memory in the
		 * form of a BIO. The supplied data is read directly from the supplied
		 * buffer: it is not copied first, so the supplied area of memory must
		 * be unchanged until the BIO is freed.
		 */
		BIO* keybio = BIO_new_mem_buf(buff, -1);
		assert(keybio);

		/*
		 * RSA *PEM_read_bio_RSAPublicKey(BIO *bp, RSA **x, pem_password_cb *cb, void *u);
		 * https://www.openssl.org/docs/man1.1.0/man3/PEM_read_bio_RSAPublicKey.html
		 */
		RSA* pub_key = PEM_read_bio_RSAPublicKey(keybio, NULL, NULL, NULL);
		assert(pub_key);

		// Encrypt the HMAC key using the subscriber's public key */

		/*
		 * RSA_PKCS1_OAEP_PADDING
		 * https://linux.die.net/man/3/rsa_public_encrypt
		 * EME-OAEP as defined in PKCS #1 v2.0 with SHA-1 , MGF1 and an empty
		 * encoding parameter. This mode is recommended for all new
		 * applications.
		 */
		char* encryptedHmacKey = malloc(RSA_size(pub_key));
		assert(encryptedHmacKey);

		/*
		 * int RSA_public_encrypt(int flen, const unsigned char *from,
         *              unsigned char *to, RSA *rsa, int padding);
		 * https://www.openssl.org/docs/manmaster/man3/RSA_public_encrypt.html
		 * flen must not be more than RSA_size(rsa)
		 * 		- 11 for the PKCS #1 v1.5 based padding modes, not more than
		 * 		  RSA_size(rsa)
		 * 		- 42 for RSA_PKCS1_OAEP_PADDING and exactly RSA_size(rsa) for
		 * 		  RSA_NO_PADDING.
		 * When a padding mode other than RSA_NO_PADDING is in use,
		 * 		then RSA_public_encrypt() will include some random bytes into
		 * 		the ciphertext and  therefore the ciphertext will be
		 * 		different each time, even if the plaintext and the public key
		 * 		are exactly identical.
		 * The returned ciphertext in to will always be zero padded to exactly
		 * RSA_size(rsa) bytes to must point to a memory section large enough
		 * to hold the maximal possible decrypted data (which is equal to
		 *     - RSA_size(rsa) for RSA_NO_PADDING, RSA_size(rsa)
		 *     - 11 for the PKCS #1 v1.5 based padding modes and RSA_size(rsa)
		 *     - 42 for RSA_PKCS1_OAEP_PADDING).
		 * padding is the padding mode that was used to encrypt the data. to and
		 * from may overlap.RSA_public_encrypt() returns the size of the
		 * encrypted data (i.e., RSA_size(rsa)).
		 */
		int padding = RSA_PKCS1_OAEP_PADDING; // Implies the following assertion
		assert(EVP_PKEY_size(secret) < RSA_size(pub_key) - 41);
		const int encryptedHmacKeyLen = RSA_public_encrypt(EVP_PKEY_size(secret),
				(const unsigned char*)secret, encryptedHmacKey, pub_key,
				padding);
		assert(encryptedHmacKeyLen == RSA_size(pub_key));

		printf("Encrypted msg size: %u\n", encryptedHmacKeyLen);
		BIO_free_all(keybio);

		printf("Encrypted HMAC key:\n");
		for (int i = 0; i < encryptedHmacKeyLen; ++i)
			printf("%02X", encryptedHmacKey[i]);
		putchar('\n');

		// Send the encrypted HMAC key to the client
		writeMsg(subSock, encryptedHmacKey, encryptedHmacKeyLen);

		// Cleanup */
		RSA_free(pub_key);

		// Compute the HMAC of the text file
		HMAC_CTX* hctx = HMAC_CTX_new();
		assert(hctx);

		intStatus = HMAC_Init_ex(hctx, secret, sizeof(secret), EVP_sha512(), NULL);
		assert(intStatus == 1);

		uint8_t* hmac = malloc(HMAC_size(hctx));
		assert(hmac);

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
		fclose(f); // NEW

		//HMAC_Update(hctx, buf, sizeof(buf)); // Why is this here?
		unsigned hmacLen = 0;
		intStatus = HMAC_Final(hctx, hmac, &hmacLen);
		assert(intStatus == 1);
		assert(hmacLen > 0);

		printf("Publisher's HMAC length: %u\n", hmacLen);
		printf("Publisher's HMAC: \n");
		for (int i = 0; i < hmacLen; i++)
			printf("%02X", hmac[i]);
		putchar('\n');

		// Send the HMAC to subscriber
		printf("Sending HMAC\n");
		writeMsg(subSock, hmac, hmacLen);
		printf("Sent HMAC\n");
		fflush(stdout);

		/* Doesn't help
		{
			char buf[1];
			(void)read(subSock, buf, 1);
		}
		*/

		HMAC_CTX_free(hctx);
		free(encryptedHmacKey);
		free(hmac);

		close(subSock);
	} // accept() loop

	// Cleanup */
	EVP_PKEY_free(secret);
    close(sock);
}
