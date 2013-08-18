#ifndef __URL_H
#define __URL_H

enum {
	URL_SUCCESS,
	URL_ERROR_UNKNOWN_SCHEME,
	URL_ERROR_INVALID_URL,
	URL_ERROR_INVALID_USERNAME,
	URL_ERROR_INVALID_HOST,
	URL_ERROR_INVALID_PORT,
	URL_ERROR_ENCODING_FAILED,
};

typedef enum {
	URL_SCHEME_HTTP,
} url_scheme_t;

struct url {
	url_scheme_t scheme;

	char *host;
	int port;

	char *path;

	char *user;
	char *pass;
};

const char *url_error(int ecode);
int url_parse(struct url *url, const char *string, char *encoding);
char *url_unescape(char *str);
void url_free(struct url *url);

#endif
