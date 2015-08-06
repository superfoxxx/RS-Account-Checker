#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <curl/curl.h>

#include "common.h"

struct memstruct{
	char *memory;
	size_t size;
};

/*
	libcurl in C is much different than in PHP, and doesn't by default output the response.
	So, we have to make our own function to handle this.
	Luckily, there's a great example here: http://curl.haxx.se/libcurl/c/getinmemory.html
*/
static size_t StoreCurl(void *contents, size_t size, size_t nmemb, void *userp) {

	size_t realsize = size * nmemb;
	struct memstruct *mem = (struct memstruct *)userp;

	mem->memory = realloc(mem->memory, mem->size + realsize + 1);
	if(mem->memory == NULL) {
		/* out of memory! */ 
		printf("not enough memory (realloc returned NULL)\n");
		return 0;
	}


	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;

	return realsize;
}

/*
	Do the actual cURL.
	Return the CURLcode, and place the response(if applicable) into response: do not put more than 'length' bytes into response.

	At this point, checks must have already been made in regard to: username/password length, memory allocation(rseponse),
	proxies type, and filechecks.
*/

CURLcode check(char *response, size_t length, const char *username, const char* password, const char* proxy) {

	CURL *curl = curl_easy_init();

	struct memstruct CurlStruct;

	CurlStruct.memory = malloc(1);
	*CurlStruct.memory = 0;
	CurlStruct.size = 0;

	if(!curl) {
		free(CurlStruct.memory);
		return -1; //?!?!?!?!
	}

	//Prepare custom headers.
	char *userenc = curl_easy_escape(curl, username, 0);
	char *passenc = curl_easy_escape(curl, password, 0);

	size_t plen = snprintf(NULL, 0, "rem=on&username=%s&password=%s&submit=Log+In&mod=www&ssl=1&dest=account_settings.ws", userenc, passenc);
	plen += 1;
	char *post = malloc(plen);
	snprintf(post, plen, "rem=on&username=%s&password=%s&submit=Log+In&mod=www&ssl=1&dest=account_settings.ws", userenc, passenc);

	curl_free(userenc);
	curl_free(passenc);

	curl_easy_setopt(curl, CURLOPT_URL, "https://secure.runescape.com/m=weblogin/login.ws"); //This may change in the future.
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Windows NT 6.3; rv:36.0) Gecko/20100101 Firefox/36.0"); //Possibly change this in the future. They block useragents when they're old(Probably my fault)
	curl_easy_setopt(curl, CURLOPT_REFERER, "https://secure.runescape.com/m=weblogin/loginform.ws?mod=www&ssl=1&reauth=1&dest=account_settings.ws"); //Likewise, may be needed to change.
	curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1); //When followlocation takes place.
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1);
	curl_easy_setopt(curl, CURLOPT_HEADER, 0);
//(Allow encoding.. for now)	curl_easy_setopt(curl, CURLOPT_ENCODING, "identity");
	curl_easy_setopt(curl, CURLOPT_POST, 1);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
	curl_easy_setopt(curl, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS4); //Socks5 generally works on socks4 connections.
	curl_easy_setopt(curl, CURLOPT_PROXY, proxy);

	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StoreCurl);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&CurlStruct);
	printf("Before\n");
	CURLcode res = curl_easy_perform(curl);
	printf("After\n");
	if(res != CURLE_OK)
		fdo_log(GENLOG, "cURL Error: %s, Account: %s:%s, Proxy: %s (Size: %zu)", curl_easy_strerror(res), username, password, proxy, CurlStruct.size);
	else {

		strncpy(response, CurlStruct.memory, length);
		response[length-1] = '\0';
	}

	curl_easy_cleanup(curl);
	free(CurlStruct.memory);
	free(post);

	return res;
}
