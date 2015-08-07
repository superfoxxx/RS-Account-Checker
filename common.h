#include <curl/curl.h>

#define VALIDLOG 0
#define VALIDLOGMB 1
#define LOCKEDLOG 2
#define INVALIDLOG 3
#define GENLOG 4
#define DBGLOG 5

struct options {
	size_t threads; //How many threads to use.
	bool std; // If true, output logins to stdout, and don't display stderr information.
	char* basename; // If not null, logging to file is enabled. This acts as a basename for files.
	bool verbose; //Verbose logging
	int retries; //How many times to TRY(retry+1) a proxy
	FILE *Valid; // FILE for writing to working login file. If 'basename' != NULL, then open this upon start. This is ALL valid accounts(both members and nonmembers)
	FILE *ValidMb; // Likewise, but for valid accounts with memberships.
	FILE *Locked; // Likewise, but for locked accounts.
	FILE *Invalid; // Likewise, but for invalid logins.

	FILE *Accounts; // File with our user:pass combinations
	FILE *Proxies; // File with our proxy:port combinations
};

struct options O;


CURLcode check(char *response, size_t length, const char *username, const char* password, const char* proxy, int stype);

void log_locks(void); //Call when beginning program

void do_log(int logtype, const char *log); //Call when actually doing log-stuff.
void fdo_log(int logtype, const char *fmt, ...); //Likewise, but formatted.

void init_locks(void); //Call when beginning program (SSL)
#define kill_locks()
