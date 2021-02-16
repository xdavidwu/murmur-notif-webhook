#ifndef WEBHOOK_H
#define WEBHOOK_H

#include <curl/curl.h>

struct webhook_state {
	CURL *curl;
	char *displayname_escaped;
};

struct webhook_state *webhook_setup();
int webhook_send(struct webhook_state *state, const char *msg);

#endif
