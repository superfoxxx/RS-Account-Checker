#define _BSD_SOURCE
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <openssl/md5.h>

/*
	This code was to create HWID-specific strings for verification purposes.
	I don't even remember which flags are required for compilation.
*/

char *getRand(void) {
	char chr[] = {110, 13, 6, 31, 74, 32, 70, 23, 86, 7, 86, 46, 0}; xor_decrypt(chr, "AicieU4v8c9C", 12);

	int randomData = open(chr, O_RDONLY);
	if(randomData < 0)
		return NULL;
	char MyRandomData[121];
	char *RealOut = calloc(121, 1);
	size_t randomDataLen = 0;
	while (randomDataLen < 121-1) {
//		ssize_t result = read(randomData, myRandomData + randomDataLen, 121 - randomDataLen);
		ssize_t result = read(randomData, MyRandomData, 1);
		if (result < 0) {
			free(RealOut);
			close(randomData);
			return NULL;
		}
		if(MyRandomData[0] == '\0' || MyRandomData[0] == '\r' || MyRandomData[0] == '\n' || MyRandomData[0] < 33 || MyRandomData[0] >= 127) {
			continue;
		}
		RealOut[randomDataLen] = MyRandomData[0];
		randomDataLen++;
	}
	close(randomData);
	return RealOut;
}

char *getmID(void) {
	char chr[] = {65, 31, 57, 67, 76, 91, 34, 43, 123, 19, 47, 64, 60, 93, 42, 18, 59, 88, 95, 45, 15, 94, 92, 47, 0}; xor_decrypt(chr, "niX1c7KITwM5OrGsX06Cjs5K", 24);
	printf("%s\n", chr);

	int mid = open(chr, O_RDONLY);
	if(mid < 0) {
		printf("10th\n");
		return NULL;
	}
	char *id = calloc(33, 1);
	for(int i=0; i<33-1; i++) {
		ssize_t result = read(mid, id+i, 1);
		if(result < 0) {
			free(id);
			close(mid);
			return NULL;
		}
	}
	close(mid);
	return id;
}
void xor_decrypt(char *string, const char* key, unsigned int len) {
        unsigned int i;
        size_t kl = strlen(key);
        for(i=0; i<len; i++) {
                string[i]=string[i]^key[i % kl];
        }
}

bool doCRFile(char *rand) {
	char chr[] = {110, 62, 92, 111, 106, 70, 30, 37, 35, 38, 0};xor_decrypt(chr, "KMsAO53F", 10);
	printf("%s\n", chr);
	char *mid = getmID();
	if(!mid) {
		printf("9th Checkpoint not working\n");
		return false;
	}

	struct passwd *pw = getpwuid(getuid());

	const char *homedir = pw->pw_dir;
	char out[80];
	sprintf(out, chr, homedir, mid);
	free(mid);

	FILE *fp = fopen(out, "w");
	if(!fp) {
		printf("%s\n", strerror(errno));
		return false;
	}

	fputs(rand, fp);
	fclose(fp);
	return true;
}



int main(void) {


	printf("Running HWID Creator.\n");
	printf("Be sure to run this from the machine that will be running the checker, and the same user.\n");

	char *rand = getRand();
	if(!rand) {
		printf("Error when running first breakpoint.\n");
		exit(2);
	}

	if(!doCRFile(rand)) {
		printf("Error when running third breakpoint.\n");
		exit(2);
	}

	char *mid = getmID();
	if(!mid) {
		char chr[] = {99, 7, 3, 44, 81, 104, 15, 19, 4, 37, 9, 14, 29, 21, 2, 47, 64, 66, 113, 4, 119, 0, 79, 87, 124, 84, 0}; xor_decrypt(chr, "LolA4GbvcDdosfg", 26);
		printf("Error when running second breakpoint\n");
		unlink(chr);
		free(rand);
		exit(2);
	}

	char gh[65];
	int gho = gethostname(gh, 64);
	gh[65-1] = 0;
	if(gho == -1) {
		printf("Error when running fourth breakpoint.(%s)\n", strerror(errno));
		char chr[] = {99, 7, 3, 44, 81, 104, 15, 19, 4, 37, 9, 14, 29, 21, 2, 47, 64, 66, 113, 4, 119, 0, 79, 87, 124, 84, 0}; xor_decrypt(chr, "LolA4GbvcDdosfg", 26);
		unlink(chr);
		free(rand);
		free(mid);
		exit(2);
	}

	char out[216] = ""; //More than enough.
	strncat(out, rand, 120);
	strncat(out, mid, 32);
	strncat(out, gh, 64);


	unsigned char c[MD5_DIGEST_LENGTH];
	unsigned char data[1024];
	int bytes;
	MD5_CTX mdContext;
	MD5_Init (&mdContext);
	MD5_Update (&mdContext, out, strlen(out));
	MD5_Final (c,&mdContext);
	printf("\nHWID IS: \n\n");
	for(int i = 0; i < MD5_DIGEST_LENGTH; i++) printf("%02x", c[i]);

	printf("\n\n");
	free(rand);
	free(mid);
	exit(0);
}
