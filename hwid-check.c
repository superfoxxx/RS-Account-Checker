#define _BSD_SOURCE
#include <openssl/md5.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <pwd.h>
#include <stdbool.h>

void xor_decrypt(char *string, const char* key, unsigned int len) {
        unsigned int i;
        size_t kl = strlen(key);
        for(i=0; i<len; i++) {
                string[i]=string[i]^key[i % kl];
        }
}

/*
	Read /var/lib/dbus/machine-id and output an malloc'd string of it.
*/
char *getmID(void) {
	char chr[] = {65, 31, 57, 67, 76, 91, 34, 43, 123, 19, 47, 64, 60, 93, 42, 18, 59, 88, 95, 45, 15, 94, 92, 47, 0}; xor_decrypt(chr, "niX1c7KITwM5OrGsX06Cjs5K", 24);

	int mid = open(chr, O_RDONLY);
	if(mid < 0)
		return NULL;
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

/*
	Read ~/.getmID()-chk This file (should) contain 120 characters
*/
char *getRandContents(void) {

	char *mid = getmID();
	if(!mid)
		return NULL;

	char file[80] = "";
	struct passwd *pw = getpwuid(getuid()); const char *homedir = pw->pw_dir;
	sprintf(file, "%s/.%s-chk", homedir, mid);
	free(mid);


	FILE *pFile = fopen(file, "r");
	if(!pFile)
		return NULL;
	char *output = malloc(121);
	fgets(output, 121, pFile);
	fclose(pFile);

	if(strlen(output) != 120)
		return NULL;

	return output;
}

char *getHWID(void) {
	char gh[65];
	int gho = gethostname(gh, 64);
	gh[65-1] = 0;

	char *gcr = getRandContents();
	if(!gcr) {
		printf("Cannot get random file contents.\n");
		return false;
	}

	char *mid = getmID();
	if(!mid) {
		printf("Cannot get HWID component.\n");
		free(gcr);
		return false;
	}

	char *out = calloc(216, 1);
	strncat(out, gcr, 120);
	strncat(out, mid, 32);
	strncat(out, gh, 64);

	unsigned char c[MD5_DIGEST_LENGTH];
	MD5_CTX mdContext;
	MD5_Init (&mdContext);
	MD5_Update (&mdContext, out, strlen(out));
	MD5_Final (c,&mdContext);

	char *md5out = malloc(33);
	for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(md5out+(2*i), "%02x", c[i]);
	}

	free(out);
	free(mid);
	free(gcr);

	return md5out;
}
