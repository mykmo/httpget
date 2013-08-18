#ifndef __DEBUG_H
#define __DEBUG_H

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef NOCOLOR
#define GREEN	"[32m"
#define DEF	"[0m"
#define RED 	"[31m"
#else
#define GREEN
#define DEF
#define RED
#endif

#define PROMPT_FMT(fmt) GREEN "%s" DEF ":" GREEN "%d" DEF ": " fmt

#define msg_err(fmt, ...) \
	fprintf(stderr, PROMPT_FMT(fmt) ": " \
		RED "%s" DEF "\n", \
		__FILE__, __LINE__, ## __VA_ARGS__, strerror(errno))

#define msg_warn(fmt, ...) \
	fprintf(stderr, PROMPT_FMT(fmt) "\n", \
		__FILE__, __LINE__, ## __VA_ARGS__)

#define err_sys(...) \
do { \
	msg_err(__VA_ARGS__); \
	exit(1); \
} while (0)

#define err_quit(...) \
do { \
	msg_warn(__VA_ARGS__); \
 	exit(1); \
} while (0)

#endif
