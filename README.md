(C) 2015-2016 Joshua Rogers <<honey@internot.info>><[@MegaManSec](https://twitter.com/MegaManSec)>

See COPYING for information regarding distribution.
Also see USAGE for the recommended usage of the program, as well as options.

#__This program is now "finished", and will not be updated any more. All bug reports will be ignored.__


System requirements:

   - libcurl
   - GNUTLS
   - pthreads
   - gcrypt


A simple "account checker" for the game [RuneScape](http://runescape.com/), with the ability to check over 50 accounts per second. 

__Features__
------------

   - stdin/stdout and file routing
   - colored output
   - SOCKS 4/5 support
   - "smart" filtering
   - multithreading


####__output__

By default, all output is written to stderr, with coloring.
For a valid login, the outputted line is green. For invalid, red. For valid but locked, yellow is used.
For debugging/error/general information, grey is used.

Passing the -s flag to the program disables all error logging, and writes checked accounts to stdout, in plaintext, in the format of username:password.

Passing the -i flag to the program disables invalid accounts from being outputted.

With the use of the -o flag, accounts can be written to files depending on their status as deemed by the checker. A 'basename' is used to create(or append to) various files:

   - basename_valid.txt - working logins(includes those from basename_locked.txt)
   - basename_members_valid.txt - working logins that are currently members
   - basename_invalid.txt - invalid logins
   - basename_locked.txt - working logins that are locked
   - basename_unchecked.txt - logins that weren't checked

With the exception of basename_unchecked.txt, each file is created when the program is started, or opened if it already exists and simply appended to.

basename_unchecked.txt is only created when either the program the program exits, or when the user prompts the program to exit(Ctrl-C). This only happens if at least one account has been tried(i.e., running the checker incorrectly will not create the file.)
The file is overwritten if it already exists, so be careful to save basename_unchecked.txt if it is created.

The program attempts to move basename_unchecked.txt to the account file specified, so the program can be re-run without re-checking old accounts. If this is unsuccessful, an error is outputted.

Obviously, it is always recommended to use the -o flag if there are many accounts to check, as you probably won't be able to see all the accounts that are checked, in your terminal history. Likewise, you may be re-checking a lot of accounts if something goes wrong.




####__smart filtering__

We want the best performance from our program, which means minimizing useless computations and connections. Likewise, we have to admit that both our connection and our proxies suck.

So, by default, each proxy is tried 4 times -- which can optionally be set with the -r flag.

Initially, each time a proxy is tried, it switches between a SOCKS4 proxy, and a SOCKS5 proxy. This is to test what type it is. On a proxy being successful, the type of proxy it is is saved, and that type is used then on. The calculation of this is defined as: 
``(currentpx->retries % 2) ? CURLPROXY_SOCKS5 : CURLPROXY_SOCKS4``
where 'retries' is how many times the proxy has failed, not been retried, despite the name. By default, it is 0. Thus, 0 % 2 = 0, which is false. Therefore, initially, SOCKS4 is tried. If it fails, it is retried: 1 % 2 = 1, which is true, thus SOCKS5 is tried. If the type is found before failure, it is stored in the variable 'type'. Since we want to initially try the proxy on both settings twice, the minimum value of the -r flag is 4(2 for SOCKS4, 2 for SOCKS5.)

Since proxies suck, a 'smart' system is in place to allow proxies to fail from time to time, without completely discarding them.
When a proxy fails somehow, whether it be a connection failure or otherwise, retries++. When a proxy succeeds, and the 'retries' variable is greater than 0, retries--.

Once 'retries' hits the value that is passed by -r (4 by default), it is declared a dead proxy and no longer used. This allows for our network to play up for a few times. It's a bit like a reputation system. If a captcha is hit, the proxy is automatically declared dead.


Various sanity checks are also performed on the input(read the source for all of them):
Proxies that are not formatted correctly, or are too long/short are removed.
Account names that are not possible to create(e.g. >12 char usernames) are removed.
Account passwords that are name possible(e.g. not alphanum, or len<5 , or that contain 'jagex') are removed.

Multithreading is also available via the -t flag.


__Info__
=======

If you encounter any problems, please report them.


There are certains things that I will not fix, such as:

   - Int overflow in the handling of the -r flag: ``O.retries = strtol(optarg, NULL, 10);``
        This is because it will only affect the user, and if they put a negative number, or a too high number, it is their own fault.

   - snprintf and malloc is not confirmed to return valid memory. This can be exhibited by the warnings that size_t(an unsigned value) is used instead of something that can handle a -1 return value from snprintf.
        If there are memory errors, the program will have more problems to worry about than snprintf messing up.


Previously, the code handled "logins" by CURLing a page on BA with an MD5 hash of an email, password, and 'HWID', and the php page would verify if you're actually allowed to use the checker or not. Since BA is dead and gone, the code has been commented out. It will remain for historial purposes, however. the CURLOPT_PINNEDPUBLICKEY flag for this operation was to be used, but was at the time of creation, only recently introduced to libcurl.

checker.php and HWID-Gen.c are added for archival purposes(HWID-Gen.c was the program that customers had to run, and checker.php had to me on a server linked to a database, with customers' HWIDs, for verification.)

