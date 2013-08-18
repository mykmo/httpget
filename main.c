#include <langinfo.h>
#include <stdbool.h>
#include <assert.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>

#include "tools.h"
#include "debug.h"
#include "net.h"
#include "url.h"

#define CRLF "\r\n"

#define isequal(s1, s2) (! strcasecmp((s1), (s2)))

#if 0
#define debug(fmt, ...) fprintf(stderr, "-- " fmt "\n", ## __VA_ARGS__)
#else
#define debug(fmt, ...) do {} while(0)
#endif

static char *encoding;

struct http_client {
	struct url url;

	FILE *stream;

	bool iskeepalive;
	bool ischunked;
	bool basic_auth;

	char *location;

	int content_length;
};

static void http_client_clear(struct http_client *hc)
{
	if (hc->location != NULL) {
		free(hc->location);
		hc->location = NULL;
	}

	hc->iskeepalive = false;
	hc->ischunked = false;
	hc->basic_auth = false;

	hc->content_length = 0;
}

static inline void base64_encode_chunk(unsigned char *s, char *p)
{
	static char map[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	*p++ = map[s[0] >> 2];
	*p++ = map[(s[0] & 0x03) << 4 | s[1] >> 4];
	*p++ = map[(s[1] & 0x0f) << 2 | s[2] >> 6];
	*p++ = map[s[2] & 0x3f];
}

static char *base64_encode(char *string)
{
	size_t n = strlen(string);
	unsigned char *s = (unsigned char *) string;
	size_t buflen;
	char *buf;
	char *p;

	if (n % 3)
		buflen = 4 * (n / 3 + 1);
	else
		buflen = 4 * n / 3;

	p = buf = xmalloc(buflen + 1);
	for (; n > 2; n -= 3, s += 3, p += 4)
		base64_encode_chunk(s, p);

	if (n > 0) {
		unsigned char tmp[3] = { s[0], 0, 0 };

		if (n > 1)
			tmp[1] = s[1];

		base64_encode_chunk(tmp, p);

		p[3] = '=';
		if (n == 1)
			p[2] = '=';

		p += 4;
	}

	*p = '\0';

	return buf;
}

static char *http_basic_encode(char *user, char *pass)
{
	size_t len = strlen(user) + 2;
	char *encoded;
	char *buf;

	if (pass != NULL)
		len += strlen(pass);
	else
		pass = "";

	buf = xmalloc(len);
	snprintf(buf, len, "%s:%s", user, pass);
	encoded = base64_encode(buf);

	free(buf);

	return encoded;
}

static void http_client_request(struct http_client *hc, bool auth)
{
	FILE *fp = hc->stream;
	struct url *url = &hc->url;

	fputs("GET ", fp);

	if (url->path == NULL)
		fputc('/', fp);
	else {
		if (url->path[0] != '/')
			fputc('/', fp);
		fputs(url->path, fp);
	}

	fputs(" HTTP/1.1" CRLF, fp);

	fprintf(fp, "Host: %s" CRLF, url->host);
	fputs("Accept: */*" CRLF, fp);
	fputs("Connection: close" CRLF, fp);
	fputs("User-Agent: httpget" CRLF, fp);

	if (auth && hc->url.user != NULL) {
		char *encoded;

		encoded	= http_basic_encode(hc->url.user, hc->url.pass);
		fprintf(fp, "Authorization: Basic %s" CRLF, encoded);
		free(encoded);
	}

	fputs(CRLF, fp);

	fflush(fp);
}

static int http_parse_status(char *line, char **desc)
{
	char *p = strchr(line, ' ');
	int status;
	char *s;
	char *endptr;

	if (p == NULL)
		return -1;

	while (*++p == ' ');

	if (*p == '\0')
		return -1;

	s = strchrnul(p, ' ');

	errno = 0;
	status = strtol(p, &endptr, 10);
	if (errno == ERANGE || status < 100)
		return -1;
	if (endptr != s)
		return -1;

	while (*s == ' ')
		s++;

	if (*s == '\0')
		*desc = NULL;
	else
		*desc = s;

	return status;
}

static int http_parse_header(char *line, char **name, char **value)
{
	char *sep = strchr(line, ':');
	char *p, *s;

	if (sep == NULL || sep == line)
		return -1;

	for (p = sep + 1; *p == ' '; p++);

	if (*p == '\0')
		return -1;

	s = strrchr(p, ' ');
	if (s != NULL) {
		while (*--s == ' ');
		s[1] = '\0';
	}

	*sep = '\0';

	*name = line;
	*value = p;

	return 0;
}

static void http_client_connect(struct http_client *hc)
{
	struct url *url = &hc->url;
	int sock;

	assert(hc->stream == NULL);

	sock = tcp_client(url->host, url->port);
	if (sock < 0)
		err_quit("connect %s:%d failed", url->host, url->port);

	hc->stream = fdopen(sock, "r+");
	if (hc->stream == NULL)
		err_sys("%s", "fopen");
}

static void http_client_close(struct http_client *hc)
{
	if (hc->stream != NULL) {
		fclose(hc->stream);
		hc->stream = NULL;
	}
}

static void http_client_collect(struct http_client *hc, char *name, char *value)
{
	if (isequal(name, "connection")) {
		if (isequal(value, "keep-alive"))
			hc->iskeepalive = true;
		else
			hc->iskeepalive = false;
	} else if (isequal(name, "content-length")) {
		int length = atoi(value);

		if (length >= 0)
			hc->content_length = length;
	} else if (isequal(name, "transfer-encoding")) {
		if (isequal(value, "chunked"))
			hc->ischunked = true;
		else
			hc->ischunked = false;
	} else if (isequal(name, "location")) {
		if (hc->location != NULL)
			free(hc->location);
		hc->location = xstrdup(value);
	} else if (isequal(name, "www-authenticate")) {
		char *p = strchrnul(value, ' ');

		if (strncasecmp(value, "basic", p - value))
			warnx("auth type %.*s not supported", p - value, value);

		hc->basic_auth = true;
	}
}

static void strip_crlf(char *line, ssize_t *len)
{
	size_t n = *len;

	if (n && line[n - 1] == '\n') line[--n] = '\0';
	if (n && line[n - 1] == '\r') line[--n] = '\0';

	*len = n;
}

static int http_client_response(struct http_client *hc)
{
	char *line = NULL;
	size_t nbytes = 0;
	ssize_t n;

	int status = 0;
	char *desc = NULL;

	while ((n = getline(&line, &nbytes, hc->stream)) >= 0) {
		line[n] = '\0';
		strip_crlf(line, &n);

		if (! status) {
			status = http_parse_status(line, &desc);
			if (status < 0) {
				warnx("invalid status line received: %s", line);
				break;
			}

			if (desc == NULL)
				fprintf(stderr, "%d\n", status);
			else
				fprintf(stderr, "%d %s\n", status, desc);
		} else if (n > 0) {
			char *name, *value;

			if (http_parse_header(line, &name, &value) < 0)
				warnx("skip invalid header: %s", line);
			else
				debug("header: %s=%s", name, value);

			http_client_collect(hc, name, value);
		} else
			break;
	}

	free(line);

	return status;
}

static void http_client_store_simple(struct http_client *hc, size_t total, FILE *out)
{
	char buf[512];
	size_t nread;

	for (;;) {
		size_t nelem = sizeof(buf);

		if (total > 0) {
			if (total < nelem)
				nelem = total;
		}

		nread = fread(buf, 1, nelem, hc->stream);
		if (nread == 0)
			break;

		fwrite(buf, 1, nread, out);

		if (total > 0) {
			total -= nread;

			if (total <= 0)
				break;
		}
	}
}

static void http_client_store_chunked(struct http_client *hc, FILE *out)
{
	char *line = NULL;
	size_t nbytes = 0;
	long length;
	ssize_t n;

	for (;;) {
		if ((n = getline(&line, &nbytes, hc->stream)) <= 0)
			break;

		line[n] = '\0';
		strip_crlf(line, &n);

		if (n == 0)
			continue;

		errno = 0;
		length = strtol(line, NULL, 16);
		if (errno || length < 0) {
			warnx("invalid chunk size '%s', break", line);
			break;
		}

		if (length == 0)
			break;

		http_client_store_simple(hc, length, out);
	}
}

static void http_client_store(struct http_client *hc)
{
	char buf[FILENAME_MAX];
	char *name = NULL;
	FILE *fp = NULL;
	char *filename;
	int n;

	if (hc->url.path != NULL) {
		char *path = url_unescape(xstrdup(hc->url.path));
		char *s = strrchr(path, '/');

		if (s == NULL)
			s = hc->url.path;
		else
			s++;

		if (*s != '\0') {
			char *p = strchr(s, '?');

			if (p == NULL)
				name = xstrdup(s);
			else if (p != s)
				name = xstrndup(s, p - s);
		}

		free(path);
	}

	filename = name != NULL ? name : "index.html";

	for (n = 0; fp == NULL; n++) {
		char *realname;

		if (n == 0)
			realname = filename;
		else {
			if (snprintf(buf, sizeof(buf), "%s.%d", filename, n) >= FILENAME_MAX) {
				warnx("filename is too long: %s.%d", filename, n);
				break;
			}

			realname = buf;
		}

		fp = fopen(realname, "wbx");

		if (fp == NULL) {
			if (errno != EEXIST) {
				warn("fopen %s failed", realname);
				break;
			}
		} else
			fprintf(stderr, "save to %s ... ", realname);
	}

	if (fp != NULL) {
		if (hc->ischunked)
			http_client_store_chunked(hc, fp);
		else
			http_client_store_simple(hc, hc->content_length, fp);
		fprintf(stderr, "ok\n");
	}

	free(name);
}

static void http_client_free(struct http_client *hc)
{
	url_free(&hc->url);
	free(hc->location);
}

static void http_client_init(struct http_client *hc, char *url_string)
{
	int status;

	memset(hc, 0, sizeof(*hc));

	status = url_parse(&hc->url, url_string, encoding);
	if (status)
		errx(1, "url parse failed: %s", url_error(status));
}

static void http_client_start(char *url_string)
{
	struct http_client hc;
	bool auth_required = false;
	bool redirect;
	bool retry;
	int status;

	http_client_init(&hc, url_string);

	do {
		redirect = false;
		retry = false;

		http_client_clear(&hc);
		http_client_connect(&hc);
		http_client_request(&hc, auth_required);

		fprintf(stderr, "send request to %s:%d ... ", hc.url.host, hc.url.port);

		status = http_client_response(&hc);

		switch (status) {
		case 200:
			http_client_store(&hc);
			break;
		case 301:
		case 302:
		case 303:
		case 307:
			if (hc.location == NULL)
				warnx("location header is missed, stop");
			else {
				redirect = true;
				retry = true;
				fprintf(stderr, "redirect to %s\n", hc.location);
			}
			break;
		case 401:
			if (auth_required)
				warnx("authentication failed");
			else {
				warnx("authentication required");

				if (hc.basic_auth && hc.url.user != NULL) {
					auth_required = true;
					retry = true;
				}
			}
			break;
		default:
			warnx("skip status code %d", status);
		}

		http_client_close(&hc);

		if (redirect) {
			struct http_client new;

			http_client_init(&new, hc.location);
			http_client_free(&hc);
			hc = new;
		}
	} while (retry);

	http_client_free(&hc);
}

int main(int argc, char *argv[])
{
	setlocale(LC_CTYPE, "");
	encoding = nl_langinfo(CODESET);

	if (argc < 2) {
		fprintf(stderr, "Usage: httpget url\n");
		return 1;
	}

	http_client_start(argv[1]);

	return 0;
}

