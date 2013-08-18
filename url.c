#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <iconv.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

#include "macros.h"
#include "debug.h"
#include "tools.h"
#include "url.h"

char *url_error_table[] = {
	"success",
	"unknown scheme",
	"invalid url",
	"invalid username",
	"invalid host",
	"invalod port",
	"encoding failed",
};

const char *url_error(int ecode)
{
	if ((unsigned) ecode > NELEM(url_error_table))
		return "unknown error";

	return url_error_table[ecode];
}

static char *strlower(char *str)
{
	char *p = str;

	for (; *p; p++)
		*p = tolower(*p);

	return str;
}

static char *url_get_scheme(const char *string, const char **endpos)
{
	const char *p = string;
	char *scheme;

	if (endpos != NULL)
		*endpos = p;

	if (! isalpha(*p))
		return NULL;

	while (*++p) {
		if (isalnum(*p) || strchr("+-.", *p) != NULL)
			/* ok */;
		else
			break;
	}

	if (strncmp(p, "://", 3))
		return NULL;

	scheme = xstrndup(string, p - string);
	if (endpos != NULL)
		*endpos = p + 3;

	return strlower(scheme);
}

static const char *strfind(const char *string, const char *delim)
{
	for (; *string != '\0'; string++) {
		if (strchr(delim, *string) != NULL)
			return string;
	}

	return NULL;
}

char *url_unescape(char *str)
{
	char *orig = str;
	char *p = str;
	char ch;

	if (str == NULL)
		return NULL;

	while (*str != '\0') {
		ch = 0;

		if (*str == '%') {
			if (str[1] && str[2] && isxdigit(str[1]) && isxdigit(str[2])) {
				char buf[3] = { str[1], str[2], '\0' };

				if ((ch = strtol(buf, NULL, 16)) != 0)
					str += 3;
			}
		}

		if (! ch)
			ch = *str++;

		*p++ = ch;
	}

	*p = '\0';

	return orig;
}

static int url_parse_userinfo(struct url *url, char *authority, char **endpos)
{
	char *atsign = strchr(authority, '@');
	char *sep;

	if (endpos != NULL)
		*endpos = authority;

	if (atsign == NULL)
		return URL_SUCCESS;

	if (atsign[1] == '\0')
		return URL_ERROR_INVALID_URL;

	if (authority == atsign)
		return URL_ERROR_INVALID_USERNAME;

	*atsign = '\0';
	sep = strchr(authority, ':');
	if (sep == NULL)
		url->user = xstrdup(authority);
	else {
		*sep = '\0';

		if (*authority == '\0')
			return URL_ERROR_INVALID_USERNAME;

		url->user = xstrdup(authority);
		if (sep[1] != '\0')
			url->pass = xstrdup(sep + 1);
	}

	if (endpos != NULL)
		*endpos = atsign + 1;

	url_unescape(url->user);
	url_unescape(url->pass);

	return URL_SUCCESS;
}

static int url_parse_host(struct url *url, char *host)
{
	char *sep = strchrnul(host, ':');

	if (*sep != '\0') {
		char *p = NULL;
		long port;

		if (sep == host)
			return URL_ERROR_INVALID_HOST;

		if (sep[1] == '\0')
			return URL_ERROR_INVALID_PORT;

		errno = 0;
		port = strtol(sep + 1, &p, 10);
		if (errno || port < 1 || port > 0xffff)
			return URL_ERROR_INVALID_PORT;

		if (*p != '\0')
			return URL_ERROR_INVALID_PORT;

		url->port = port;
	}

	url->host = url_unescape(xstrndup(host, sep - host));

	return URL_SUCCESS;
}

static bool isunreserved(char ch)
{
	if (isalnum(ch) && isascii(ch))
		return true;

	if (strchr("-_.~", ch) != NULL)
		return true;

	return false;
}

static bool isreserved(char ch)
{
	static char reserved[] = "!*'();:@&=+$,/?%#[]";

	if (strchr(reserved, ch) != NULL)
		return true;

	return false;
}

static char *escape(char *buf, unsigned char ch)
{
	static char symbols[] = "0123456789ABCDEF";

	char *orig = buf;

	*buf++ = '%';
	*buf++ = symbols[ch >> 4];
	*buf++ = symbols[ch & 0x0F];
	*buf = '\0';

	return orig;
}

static char *url_escape(char *string)
{
	char *p = string;
	char buf[4];
	size_t size;
	char *ptr;
	FILE *out;

	out = open_memstream(&ptr, &size);
	if (out == NULL)
		err_sys("%s", "open_memstream failed");

	while (*p != '\0') {
		if (*p == '%') {
			if (p[1] && p[2] && isxdigit(p[1]) && isxdigit(p[2])) {
				fprintf(out, "%.3s", p);
				p += 3;
			} else
				fputs(escape(buf, *p++), out);
		} else if (isunreserved(*p) || isreserved(*p))
			fputc(*p++, out);
		else
			fputs(escape(buf, *p++), out);
	}

	fclose(out);

	return ptr;
}

static int url_parse_main(struct url *url, const char *p)
{
	int status = URL_SUCCESS;
	const char *sep;
	char *authority;
	char *s;

	sep = strfind(p, "/?#");
	if (sep == p)
		return URL_ERROR_INVALID_URL;

	if (sep == NULL)
		authority = xstrdup(p);
	else
		authority = xstrndup(p, sep - p);

	s = authority;

	if ((status = url_parse_userinfo(url, s, &s)) != URL_SUCCESS)
		goto out;

	if ((status = url_parse_host(url, s)) != URL_SUCCESS) {
		url_free(url);
		goto out;
	}

	if (sep != NULL && *sep != '#' && sep[1] != '\0') {
		char *hash = strchr(sep + 1, '#');
		char *tmp = NULL;

		if (hash == NULL)
			tmp = xstrdup(sep);
		else if (hash != sep)
			tmp = xstrndup(sep, hash - sep);

		if (tmp != NULL) {
			url->path = url_escape(tmp);
			free(tmp);
		}
	}

out:
	free(authority);

	return status;
}

static char *convert(iconv_t cd, char *buf)
{
	size_t buflen = strlen(buf);
	size_t len, outlen;
	char *out;
	char *s;

	len = outlen = buflen * 2;
	s = out = xmalloc(len + 1);

	for (;;) {
		if (iconv(cd, &buf, &buflen, &out, &outlen) != (size_t) -1) {
			*out = '\0';
			break;
		} else if (errno == E2BIG) {
			size_t nready = out - s;

			len += buflen * 2;
			s = xrealloc(s, len + 1);
			out = s + nready;
			outlen = len - nready;
		} else {
			msg_err("%s", "iconv failed");
			free(s);
			return NULL;
		}
	}

	return s;
}

int url_parse(struct url *orig_url, const char *string, char *encoding)
{
	int status = URL_SUCCESS;

	char *new = NULL;
	struct url url;
	char *scheme;

	if (*string == '\0')
		return URL_ERROR_INVALID_URL;

	memset(&url, 0, sizeof(url));

	scheme = url_get_scheme(string, &string);
	if (scheme == NULL)
		url.scheme = URL_SCHEME_HTTP;
	else {
		if (! strcmp(scheme, "http"))
			url.scheme = URL_SCHEME_HTTP;
		else {
			free(scheme);
			return URL_ERROR_UNKNOWN_SCHEME;
		}
	}

	free(scheme);

	if (encoding != NULL && strcasecmp(encoding, "utf-8")) {
		iconv_t cd;

		cd = iconv_open("UTF-8", encoding);
		if (cd == (iconv_t) -1)
			err_sys("iconv_open(\"UTF-8\", \"%s\") failed", encoding);

		new = convert(cd, (char *) string);
		iconv_close(cd);

		if (new == NULL)
			return URL_ERROR_ENCODING_FAILED;

		string = new;
	}

	status = url_parse_main(&url, string);

	free(new);

	if (status == URL_SUCCESS) {
		if (! url.port)
			url.port = 80;

		*orig_url = url;
	}

	return status;
}

void url_free(struct url *url)
{
	free(url->host);
	free(url->path);
	free(url->user);
	free(url->pass);
}
