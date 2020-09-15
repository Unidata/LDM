#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
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

	/* Create and set up the socket */
	int sock = 0;    
	sock = create_socket(50000);
    
	/* Create public-private key pair */

	/**********************************************************************************************/
	/* RSA_F4: 65537 inline                                                                       */
	/* int BN_set_word(BIGNUM *a, BN_ULONG w);                                                    */
	/* https://www.openssl.org/docs/man1.1.0/man3/BN_set_word.html                                */
	/* BN_set_word() set a to the values w.                                                       */
	/**********************************************************************************************/
	int ret = 0, bit = 2048;
	unsigned long e = RSA_F4;
	RSA *rsa = NULL;
	BIGNUM *bne = NULL;

	bne = BN_new();
	ret = BN_set_word(bne, e);
	if(ret != 1) {
		exit(EXIT_FAILURE);
	}


	/**********************************************************************************************/
	/* int RSA_generate_key_ex(RSA *rsa, int bits, BIGNUM *e, BN_GENCB *cb);                      */
	/* https://www.openssl.org/docs/man1.0.2/man3/RSA_generate_key_ex.html                        */
	/* RSA_generate_key_ex() generates                                                            */
	/* 		a key pair and stores it in the RSA structure provided in rsa.                        */
	/**********************************************************************************************/
	rsa = RSA_new();
	ret = RSA_generate_key_ex(rsa, bit, bne, NULL);
	if(ret != 1) {
		exit(EXIT_FAILURE);
	}


	/* Convert RSA structure to C-string structure */
	BIO *pub = BIO_new(BIO_s_mem());

	if (!PEM_write_bio_RSAPublicKey(pub, rsa)){
		printf("Error Writing RSAPubKey to BIO.\n");
	}
	

	/**********************************************************************************************/
	/* int BIO_pending(BIO *b);                                                                   */
	/* https://www.openssl.org/docs/man1.1.0/man3/BIO_pending.html                                */
	/* return the number of pending characters in the BIOs read and write buffers.                */
	/**********************************************************************************************/
	size_t pub_len = BIO_pending(pub); // Doesn't include a terminating 0
	char *pub_key = (char *)malloc(pub_len);
	pub_key[pub_len] = 1;
	BIO_read(pub, pub_key, pub_len); // Doesn't 0-terminate
	printf("pub_len=%zu, pub_key[pub_len]=0x%x\n", pub_len, pub_key[pub_len]);

	printf("%.*s\n", (int)pub_len, pub_key);


	/* Clean up */
	BIO_free_all(pub);
	BN_free(bne);

    
	/* Send the subsriber's public key (C-string) to the publisher */
	writeMsg(sock, pub_key, pub_len + 1);

	free(pub_key);

    
	/* Read the encrypted shared secret */
	char buff[1500] = {};
	size_t encryptLen = readMsg(sock, buff);
	printf("read %d bytes - publisher's encrypted HMAC key\n", encryptLen);



	/* Decrypt the shared secret using the subscriber's private key */
	int padding = RSA_PKCS1_OAEP_PADDING;
	

	/* decrypt should be of the same data type as random_key at the publisher side */
	uint8_t decrypt[1500];

	/**********************************************************************************************/
	/* int RSA_private_decrypt(int flen, const unsigned char *from,                              */
    /* 			unsigned char *to, RSA *rsa, int padding);                                        */
	/* https://www.openssl.org/docs/man1.0.2/man3/RSA_private_decrypt.html                        */
	/* RSA_private_decrypt() decrypts the flen bytes at from using the private key rsa and        */
	/* 		stores the plaintext in to.                                                           */
	/* to must point to a memory section large enough to hold the decrypted data                  */
	/* 		(which is smaller than RSA_size(rsa)).                                                */
	/* padding is the padding mode that was used to encrypt the data.                             */
	/* returns the size of the recovered plaintext.                                               */
	/**********************************************************************************************/

	ret = RSA_private_decrypt(encryptLen, buff, decrypt, rsa, padding);
	if(ret == -1) {
		printf("ERROR decrypting\n");
		fprintf(stderr, "%s\n", ERR_error_string(ERR_get_error(), NULL));
		exit(EXIT_FAILURE);
	}
	
	printf("decrypted msg length: %d\n", ret);



	/* HMAC demo: receive and verify the HMAC code */

	/* Receive publisher calculated HMAC code */
	uint8_t buffer[1500] = {};
	size_t hmacLen = readMsg(sock, buffer);
	printf("read %d bytes - publisher's calculated HMAC code\n", hmacLen);


	/* Convert the received HMAC key from uint8_t to EVP_PKEY */
	EVP_PKEY *secret = NULL;
	secret = EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC, NULL, decrypt, ret);


	/* Subscriber calculates the HMAC code based on the provided text file */
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


	/* Initialize signature context prt */

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



	int i = 0;
	printf("subscriber calculates HMAC code: \n");
	for (i = 0; i < md_len; i++) {
		printf("%02X", md_value[i]);
	}
	printf("\n");


	fclose(f);
	EVP_MD_CTX_free(md_ctx);
	EVP_PKEY_free(secret);
	RSA_free(rsa);

	
	/* Verify the received HMAC code */
	ret = CRYPTO_memcmp(buffer, md_value, md_len);
	if (ret == 0) {	
		printf("verification succeeds. return value %d\n", ret);
	} else {
		printf("verification fails. return value %d\n", ret);
	}
	

	close(sock);

	return 0;
    
}






/* Create socket */
int create_socket(int port) {
	int s;
	struct sockaddr_in serv_addr;
    
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	s = socket(AF_INET, SOCK_STREAM, 0);
	if(s < 0) {
		perror("Unable to create socket");
		exit(EXIT_FAILURE);
	}


	//struct timeval timeout;
	//timeout.tv_sec = 1;
	//timeout.tv_usec = 0;
	
	//if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) {
	//	perror("Error SO_RCVTIMEO");
	//	exit(EXIT_FAILURE);
	//}


	if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
		perror("Invalid address");
		exit(EXIT_FAILURE);
	}


	if(connect(s, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
		perror("Unable to connect");
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
