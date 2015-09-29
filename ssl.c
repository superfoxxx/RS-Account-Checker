#include <stdio.h>
#include <pthread.h>
//#include <curl/curl.h>
//#include "libcurl.h"

#include <gcrypt.h>
#include <errno.h>
 
GCRY_THREAD_OPTION_PTHREAD_IMPL;
 
void init_locks(void)
{
  gcry_control(GCRYCTL_SET_THREAD_CBS);
}
 
