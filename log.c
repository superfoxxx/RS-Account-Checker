#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <pthread.h>

#include "common.h"

#define LOGBUFSZ 65536

#define COL_RED "\033[31;01m[ACCT]" //Invalid login
#define COL_YELLOW "\033[33;01m[ACCT]" //Locked login
#define COL_GREEN "\033[32;01m[ACCT]" //Working login
#define COL_GRAY "\033[30;01m[INFO]" //Debug/general
#define COL_RES "\033[0m" //Reset

pthread_mutex_t logkey;
pthread_mutex_t filekey;



void log_locks(void) {
	pthread_mutex_init(&logkey, NULL);
	pthread_mutex_init(&filekey, NULL);
}


/*
	Simply write 'log' into 'loc', with locking. Does NOT put a newline afterwards.
*/
void do_write(const char *log, FILE *loc) {
	pthread_mutex_lock(&logkey);
	fputs(log, loc);
	pthread_mutex_unlock(&logkey);
}

/*
	Variable do_write function
	This uses vfprintf, however.

	Make sure 'fmt' is a fmt, and not user-input..
*/
void fdo_write(FILE *loc, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	pthread_mutex_lock(&logkey);
	vfprintf(loc, fmt, ap);
	pthread_mutex_unlock(&logkey);

	va_end(ap);
}


/*
	Return appropriate color/tag for the logtype.
*/
const char* levtag(int logtype) {
	switch(logtype) {
	case VALIDLOG: case VALIDLOGMB: return COL_GREEN;
	case LOCKEDLOG: return COL_YELLOW;
	case INVALIDLOG: return COL_RED;
	case GENLOG: return COL_GRAY;
	}
	return 0;
}

/*
	This function is used to output when an account has been tried, and its success/failure, or just for general debug info.
	char* log is the username:password! Unless log=GENLOG.
	stderr ONLY.
*/
void logserr(int logtype, const char *log) {
	const char *fmt;

	switch(logtype) {
	case VALIDLOG:
		fmt = "%s[TS:%lu] Login works: %s (rthread %lu)%s\n";
		break;
	case VALIDLOGMB:
		fmt = "%s[TS:%lu] Login works: %s  --  Account is Members (rthread %lu)%s\n";
		break;
	case LOCKEDLOG:
		fmt = "%s[TS:%lu] Login works, however it is locked: %s (rthread %lu)%s\n";
		break;
	case INVALIDLOG:
		fmt = "%s[TS:%lu] Login doesn\'t work: %s (rthread %lu)%s\n";
		break;
	case GENLOG:
		fmt = "%s[TS:%lu] %s (rthread %lu)%s\n";
		break;
	default:
		fmt = "\n"; //Clang warning
		break;
	}

	fdo_write(stderr, fmt, levtag(logtype), time(NULL), log, pthread_self(), COL_RES);
}

/*
	Output logins to stdout, without any debugging information.
	Format:    Works: account:password, Locked: account:password, Invalid: account:password
*/
void logsout(int logtype, const char *login) {

	const char *fmt;

	switch(logtype) {
	case VALIDLOG:
		fmt = "Works: %s\n";
		break;
	case VALIDLOGMB:
		fmt = "Works: %s  --  Is Members\n";
		break;
	case LOCKEDLOG:
		fmt = "Locked: %s\n";
		break;
	case INVALIDLOG:
		fmt = "Invalid: %s\n";
		break;
	case GENLOG:
		return;
	default:
		fmt = "\n"; //Clang warning
		break;
	}

	fdo_write(stdout, fmt, login);

}
/*
	Write to 'O.basename_xx' file.
	This relies on using the O. struct, with all the files, which have  ~~~ALREADY BEEN OPENED~~~.
*/
void logacctofile(int logtype, const char *login) {

	switch(logtype) {
	case VALIDLOG:
		fdo_write(O.Valid, "%s\n", login);
		break;
	case VALIDLOGMB:
		fdo_write(O.ValidMb, "%s\n", login);
		break;
	case LOCKEDLOG:
		fdo_write(O.Locked, "%s\n", login);
		break;
	case INVALIDLOG:
		fdo_write(O.Invalid, "%s\n", login);
		break;
	case GENLOG:
		break;
	}

}

/*
	Do logging. Always use this funtion.
	Inner functions will determine where things need to go.

*/
void do_log(int logtype, const char *log) {

	O.std ? logsout(logtype, log) : logserr(logtype, log);

	if(O.basename) logacctofile(logtype, log);
}

void fdo_log(int logtype, const char *fmt, ...) {

	va_list ap;
	va_start(ap, fmt);

	size_t len = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	len += 1;
	char *log = malloc(len);

	va_start(ap, fmt);
	vsnprintf(log, len, fmt, ap);
	va_end(ap);

	do_log(logtype, log);

	free(log);

}
