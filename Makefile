CC=clang

all: build

build:
	$(CC) ${CFLAGS}  \
	-std=c11 -O2 -Wall -Wpedantic -Wextra main.c curl.c hwid-check.c log.c ssl.c -lcurl -lpthread -lgcrypt -lcrypto -o checker.bin -I/usr/local/include/ -L/usr/local/libs/ -L/usr/local/lib/
	strip --strip-all checker.bin
	strip --remove-section=.comment --remove-section=.note checker.bin
