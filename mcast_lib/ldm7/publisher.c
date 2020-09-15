#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>

#include <openssl/rsa.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>


int create_socket(int port);
//EVP_PKEY *EVP_PKEY_new_raw_private_key(int type, ENGINE *e, const unsigned char *key, int keylen);
EVP_MD_CTX* EVP_MD_CTX_new();

static void writeMsg(
	const int		sd,
	const uint8_t*	bytes,
	const size_t	nbytes);

static size_t readMsg(
	const int		sd,
	uint8_t*		bytes);



int main(int argc, char **argv)
{
	/* Argument reminder */
	if (argc < 2) {
		printf("Usage: %s [FILE]\n", argv[0]);
		return 1;
	}


	/* Create socket */
	int sock;
	sock = create_socket(50000);
	EVP_PKEY *secret = NULL;

	/* Generate HMAC key */
	//size_t key_len = 64;
	size_t key_len = 32;
	uint8_t random_key[key_len];
	RAND_bytes(random_key, sizeof(random_key));


	/**********************************************************************************************/
	/* EVP_PKEY *EVP_PKEY_new_raw_private_key(int type, ENGINE *e,                                */
	/*                                        const unsigned char *key, size_t keylen);           */
	/* https://www.openssl.org/docs/man1.1.1/man3/EVP_PKEY_new_raw_private_key.html               */
	/* EVP_PKEY_new_raw_private_key() allocates a new EVP_PKEY.                                   */
	/* If e is non-NULL then the new EVP_PKEY structure is associated with the engine e.          */
	/* The type argument indicates what kind of key this is.                                      */
	/* 		one of EVP_PKEY_HMAC, EVP_PKEY_POLY1305, EVP_PKEY_SIPHASH, EVP_PKEY_X25519,           */
	/*		EVP_PKEY_ED25519, EVP_PKEY_X448 or EVP_PKEY_ED448.                                    */
	/* key points to the raw private key data for this EVP_PKEY which should be of length keylen. */
	/*		The length should be appropriate for the type of the key.                             */
	/**********************************************************************************************/
	secret = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, NULL, random_key, key_len);

	size_t secret_len;
	secret_len = EVP_PKEY_size(secret);
	printf("HMAC key length: %d\n", secret_len);


	/* Handle connections */
	//while(1) {
		struct sockaddr_in addr;
		unsigned len = sizeof(addr);
		int ret = 0;

	
		/* Accept client's request */
		int client = accept(sock, (struct sockaddr*)&addr, &len);
		if (client < 0) {
			perror("Unable to accept");
			exit(EXIT_FAILURE);
		}


		/* Obtain subscriber's public key */
		char buff[1500] = {};
		size_t pubKeyLen = readMsg(client, buff);
		printf("read %d bytes - subscriber's public key\n", pubKeyLen);
		

		/* Convert the subsriber's public key from C-string to RSA structure */

		/******************************************************************************************/
		/* BIO *BIO_new_mem_buf(const void *buf, int len);                                        */
		/* https://www.openssl.org/docs/man1.0.2/man3/BIO_new_mem_buf.html                        */
		/* BIO_new_mem_buf() creates a memory BIO using len bytes of data at buf,                 */
		/* 		if len is -1 then the buf is assumed to be nul terminated and                     */
		/* 		its length is determined by strlen.                                               */
		/* The BIO is set to a read only state and as a result cannot be written to.              */
		/* 		This is useful when some data needs to be made available from a static area       */
		/* 		of memory in the form of a BIO.                                                   */
		/* The supplied data is read directly from the supplied buffer: it is not copied first,   */
		/* 		so the supplied area of memory must be unchanged until the BIO is freed.          */
		/******************************************************************************************/
		BIO *keybio = BIO_new_mem_buf(buff, -1);
		if(keybio == NULL) {
			printf("ERROR generating keybio\n");
			exit(EXIT_FAILURE);
		}		

		RSA *pub_key = NULL;

		/******************************************************************************************/
		/* RSA *PEM_read_bio_RSAPublicKey(BIO *bp, RSA **x, pem_password_cb *cb, void *u);        */
		/* https://www.openssl.org/docs/man1.1.0/man3/PEM_read_bio_RSAPublicKey.html              */
		/******************************************************************************************/
		pub_key = PEM_read_bio_RSAPublicKey(keybio, NULL, NULL, NULL);
		if (pub_key == NULL){
			printf("ERROR reading the public key\n");
		}

		
		/* Encrypt the shared secret using the subscriber's public key */

		/******************************************************************************************/
		/* RSA_PKCS1_OAEP_PADDING                                                                 */
		/* https://linux.die.net/man/3/rsa_public_encrypt                                         */
		/* EME-OAEP as defined in PKCS #1 v2.0 with SHA-1 , MGF1 and an empty encoding parameter. */
		/* This mode is recommended for all new applications.                                     */
		/******************************************************************************************/
		int padding = RSA_PKCS1_OAEP_PADDING;
		unsigned char *encrypt = NULL;
		encrypt = (unsigned char*)malloc(RSA_size(pub_key));
		

		/* Convert the HMAC key into char and encrypt */
		unsigned char sec_buf[1500] = {};

		/******************************************************************************************/
		/* int EVP_PKEY_get_raw_private_key(const EVP_PKEY *pkey, unsigned char *priv,            */
		/*                                  size_t *len);                                         */
		/* It fills the buffer provided by priv with raw private key data.                        */
		/* The size of the priv buffer should be in *len on entry to the function,                */
		/*		and on exit *len is updated with the number of bytes actually written.            */
		/******************************************************************************************/

		if (EVP_PKEY_get_raw_private_key(secret, sec_buf, &key_len) != 1){
			printf("Error Writing HMAC Key to BIO.\n");
		}
		

		/******************************************************************************************/
		/* int RSA_public_encrypt(int flen, const unsigned char *from,                            */
        /*              unsigned char *to, RSA *rsa, int padding);                                */
		/* https://www.openssl.org/docs/manmaster/man3/RSA_public_encrypt.html                    */
		/* flen must not be more than RSA_size(rsa)                                               */
		/* 		- 11 for the PKCS #1 v1.5 based padding modes, not more than RSA_size(rsa)        */
		/* 		- 42 for RSA_PKCS1_OAEP_PADDING and exactly RSA_size(rsa) for RSA_NO_PADDING.     */
		/* When a padding mode other than RSA_NO_PADDING is in use,                               */
		/* 		then RSA_public_encrypt() will include some random bytes into the ciphertext and  */
		/* 		therefore the ciphertext will be different each time,                             */
		/* 		even if the plaintext and the public key are exactly identical.                   */
		/* The returned ciphertext in to will always be zero padded to exactly RSA_size(rsa) bytes*/
		/* to must point to a memory section large enough to hold the maximal                     */
		/* 		possible decrypted data (which is equal to RSA_size(rsa) for RSA_NO_PADDING,      */
		/* 		RSA_size(rsa) - 11 for the PKCS #1 v1.5 based padding modes and RSA_size(rsa) -   */
		/* 		42 for RSA_PKCS1_OAEP_PADDING).                                                   */
		/* padding is the padding mode that was used to encrypt the data. to and from may overlap.*/
		/* RSA_public_encrypt() returns the size of the encrypted data (i.e., RSA_size(rsa)).     */
		/******************************************************************************************/

		ret = RSA_public_encrypt(key_len, sec_buf, encrypt, pub_key, padding);
		printf("encrypted msg size: %d\n", ret);
		if(ret != RSA_size(pub_key)) {
			printf("ERROR encrypt the message\n");
			fprintf(stderr, "%s\n", ERR_error_string(ERR_get_error(), NULL));
			exit(EXIT_FAILURE);
		}
		BIO_free_all(keybio);
	
	
		/* Send the encrypted shared secret to the client */
		writeMsg(client, encrypt, ret);


		/* Cleanup */
		RSA_free(pub_key);


		/* HMAC demo: if the HMAC key can be used */
		EVP_MD_CTX *md_ctx = NULL;
		
		FILE *f;
		uint8_t buf[1462];
		unsigned char md_value[EVP_MAX_MD_SIZE];
		size_t md_len = 0;
		
		
		/* Initialize md */	
		md_ctx = EVP_MD_CTX_new();
		if (md_ctx == NULL) {
			printf("ERROR create md\n");
			fprintf(stderr, "%s\n", ERR_error_string(ERR_get_error(), NULL));
			exit(EXIT_FAILURE);
		}

		

		/* Initialize signature context ptr */

		/******************************************************************************************/
		/* int EVP_DigestSignInit(EVP_MD_CTX *ctx, EVP_PKEY_CTX **pctx, const EVP_MD *type,       */
		/*						  ENGINE *e, EVP_PKEY *pkey);                                     */
		/******************************************************************************************/
		//ret = EVP_DigestSignInit(md_ctx, NULL, EVP_sha512(), NULL, secret);
		ret = EVP_DigestSignInit(md_ctx, NULL, EVP_sha256(), NULL, secret);
		if (ret != 1) {
			printf("ERROR initialize signature context\n");
			fprintf(stderr, "%s\n", ERR_error_string(ERR_get_error(), NULL));
			exit(EXIT_FAILURE);
		}


		/* Update regarding the message */
		f = fopen(argv[1], "r");
		while (!feof(f)) {
			ret = fread(buf, 1, sizeof(buf), f);
			EVP_DigestSignUpdate(md_ctx, &buf[0], ret);
		}

		//if (EVP_DigestSignUpdate(md_ctx, buf, sizeof(buf)) != 1) {
		//	printf("ERROR update digest signature\n");
		//	fprintf(stderr, "%s\n", ERR_error_string(ERR_get_error(), NULL));
		//	exit(EXIT_FAILURE);
		//}

		if (EVP_DigestSignFinal(md_ctx, md_value, &md_len) != 1) {
			printf("ERROR finalize digest signature\n");
			fprintf(stderr, "%s\n", ERR_error_string(ERR_get_error(), NULL));
			exit(EXIT_FAILURE);
		}

		fclose(f);
	
	
		int ii = 0;
		printf("publisher calculates HMAC code: \n");
		for (ii = 0; ii < md_len; ii++) {
			printf("%02X", md_value[ii]);
		}
		printf("\nHMAC code ends. Total Len: %d\n", md_len);


		/* Send the HMAC code to subscriber */	
		writeMsg(client, md_value, md_len);

		printf("*************** one run finished ***************\n");


		EVP_MD_CTX_free(md_ctx);
		free(encrypt);

		//}


	/* Cleanup */
	EVP_PKEY_free(secret);
    close(sock);

}




/* Create socket */
int create_socket(int port) {
    int s;
    struct sockaddr_in addr;
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }

	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0) {
		perror("Unable SO_REUSEADDR");
		exit(EXIT_FAILURE);
	}

    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }
    
    if (listen(s, 1) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }

    return s;
}



/* Read and Write */
static void writeMsg(
	const int		sd,
	const uint8_t*	bytes,
	const size_t	nbytes)
{
	ssize_t status = write(sd, &nbytes, sizeof(nbytes));
	assert(status == sizeof(nbytes));

	status = write(sd, bytes, nbytes);
	assert(status == nbytes);

	//printf("wrote %zu bytes\n", nbytes);
}



static size_t readMsg(
	const int		sd,
	uint8_t*		bytes)
{
	size_t nbytes;
	ssize_t status = read(sd, &nbytes, sizeof(nbytes));
	assert(status == sizeof(nbytes));

	status = read(sd, bytes, nbytes);
	assert(status == nbytes);

	//printf("read %zu bytes\n", nbytes);

	return nbytes;
}

