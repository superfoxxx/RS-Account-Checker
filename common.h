#include <curl/curl.h>

#define VALIDLOG 0
#define LOCKEDLOG 1
#define INVALIDLOG 2
#define GENLOG 3

struct options {
	size_t threads; //How many threads to use.
	bool std; // If true, output logins to stdout, and don't display stderr information.
	const char* basename; // If not null, logging to file is enabled. This acts as a basename for files.
	FILE *Valid; // FILE for writing to working login file. If 'basename' != NULL, then open this upon start.
	FILE *Locked; // Likewise, but for locked accounts.
	FILE *Invalid; // Likewise, but for invalid logins.

	FILE *Accounts; // File with our user:pass combinations
	FILE *Proxies; // File with our proxy:port combinations
};

struct options O;


CURLcode check(char *response, size_t length, const char *username, const char* password, const char* proxy);

void log_init(void); //Call when beginning program

void do_log(int logtype, const char *log); //Call when actually doing log-stuff.
void fdo_log(int logtype, const char *fmt, ...); //Likewise, but formatted.

CURLcode check(char *response, size_t length, const char *username, const char* password, const char* proxy);

void init_locks(void); //Call when beginning program (SSL)
#define kill_locks()
