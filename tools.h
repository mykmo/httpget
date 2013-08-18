#ifndef TOOLS_H
#define TOOLS_H

#include <sys/types.h>

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

#endif
