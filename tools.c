#include <string.h>
#include <stdlib.h>

#include "tools.h"
#include "debug.h"

void *xmalloc(size_t size)
{
	void *p;

	p = malloc(size);
	if (p == NULL)
		err_sys("malloc %zd bytes", size);

	return p;
}

void *xrealloc(void *ptr, size_t size)
{
	ptr = realloc(ptr, size);

	if (size && ptr == NULL)
		err_sys("realloc %zd bytes", size);

	return ptr;
}

char *xstrdup(const char *s)
{
	int len;

	if (s == NULL)
		return NULL;

	len = strlen(s) + 1;

	return memcpy(xmalloc(len), s, len);
}

char *xstrndup(const char *s, size_t n)
{
	size_t len;
	char *p;

	if (s == NULL)
		return NULL;

	len = strlen(s);
	if (n < len)
		len = n;

	p = memcpy(xmalloc(len + 1), s, len);
	p[len] = '\0';

	return p;
}

