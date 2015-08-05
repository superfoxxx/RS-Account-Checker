#include <stdarg.h>
#include <stdio.h>
#include <pthread.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include <curl/curl.h>

#include "common.h"

struct node {
	char *username;
	char *password;
	bool checked;
	bool inprogress;
	struct node *next;
};

struct node *head = NULL;

pthread_mutex_t account; //Accessing our node_t.
pthread_mutex_t pthnum; //Accessing size_t thread_num
pthread_mutex_t chkaccount; //Accessing checked_accounts

size_t thread_num = 0; //How many threads are currently running
size_t checked_accounts = 0; //How many accounts have been checked (if this is the same as the amount as the imported amount, we can exit)

/*
	Push an account to the end of our list.
*/
void push_acc(char *username, char *password) {
	struct node *current = head;

	pthread_mutex_lock(&account);
	while(current->next)
		current = current->next;
	current->next = malloc(sizeof *current->next);
	current->next->username = username;
	current->next->password = password;
	current->next->checked = false;
	current->next->inprogress = false;
	current->next->next = NULL;
	pthread_mutex_unlock(&account);

}

void *do_threaded() {

	char out[300000]; //Max response we will handle. TODO: Determine appropriate amount

	struct node *current = head;

	char *username = malloc(100);
	char *password = malloc(100);

	pthread_mutex_lock(&account);
	while(current) {
		if(current->inprogress || current->checked) {
			fdo_log(GENLOG, "prog: %d checked: %d", current->inprogress, current->checked);
			current = current->next;
			continue;
		}

		strcpy(username, current->username);
		strcpy(password, current->password);
		current->inprogress = true;
		break;
	}
	pthread_mutex_unlock(&account);

	if(!username || !password) {
		free(username);
		free(password);
		pthread_mutex_unlock(&pthnum);
		thread_num--;
		pthread_mutex_unlock(&pthnum);
		return NULL;
	}


	CURLcode resp = check(out, 300000, username, password, "127.0.0.1:9000");
	if(resp != CURLE_OK) {
		pthread_mutex_lock(&account);
		current->inprogress = false;
		pthread_mutex_unlock(&account);
	} else if(strstr(out, "Change Password")) {
		fdo_log(VALIDLOG, "%s:%s", username, password);
		pthread_mutex_lock(&account);
		current->inprogress = false;
		current->checked = true;
		pthread_mutex_unlock(&account);

		pthread_mutex_lock(&chkaccount);
		checked_accounts++;
		pthread_mutex_unlock(&chkaccount);
	} else {
		//TODO: all cases. Test by just doing this for now :)
		pthread_mutex_lock(&chkaccount);
		checked_accounts++;
		pthread_mutex_unlock(&chkaccount);

		do_log(INVALIDLOG, username);

		pthread_mutex_lock(&account);
		current->inprogress = false;
		current->checked = true;
		pthread_mutex_unlock(&account);
	}

	pthread_mutex_lock(&pthnum);
	thread_num--;
	pthread_mutex_unlock(&pthnum);
	free(password);
	free(username);

	return NULL;

}

void usage(const char *p) {

	fprintf(stderr, "usage: %s -t <numthreads> -s <stdout> -o <outfile_base> -a <accountfile>\n"
			"-p <proxyfile>\n", p);
	exit(0);
}


int main() {

	//Initialize options.

	O.threads = 2;




	curl_global_init(CURL_GLOBAL_ALL);
	log_init();
	init_locks();

	pthread_mutex_init(&account, NULL);
	pthread_mutex_init(&pthnum, NULL);
	pthread_mutex_init(&chkaccount, NULL);

	head = malloc(sizeof *head);
	head->username = malloc(20);
	head->password = malloc(20);
	strcpy(head->username, "username0"); //TODO: filein
	strcpy(head->password, "password0");
	head->checked = false;
	head->inprogress = false;
	head->next = NULL;

	push_acc( "username1", "password1");
	push_acc( "username2", "password2");
	push_acc("username3", "password3");

	//head now contains 1 accounts.

	while(checked_accounts != 4/*how many accounted we have*/) {
		pthread_mutex_lock(&pthnum);
		if(thread_num == O.threads) {
			pthread_mutex_unlock(&pthnum);
			continue;
		}
		thread_num++;
		pthread_mutex_unlock(&pthnum);
		pthread_t thread;
		//let do_threaded() decide whether or not we can pull an account(processing all already?)
		pthread_create(&thread, NULL, &do_threaded, NULL);
	}

	free(head->username);
	free(head->password);
	free(head);

	kill_locks();
	curl_global_cleanup();
	return 0;
}
