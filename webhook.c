#include "webhook.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const char webhook_template[]="{\"text\":\"%s\",\"format\":\"plain\","
	"\"displayName\":\"%s\"}";

static const char *get_displayname() {
	const char *env = getenv("MNW_WEBHOOK_DISPLAYNAME");
	return env ? env : "Murmur Notif Webhook";
}

static char *json_string_escape(const char *str) {
	const char *ptr = str;
	int cnt = 1;
	while (*ptr != '\0') {
		if (*ptr == '\b' || *ptr == '\f' || *ptr == '\n' ||
				*ptr == '\r' || *ptr == '\t' || *ptr == '"' ||
				*ptr == '\\') {
			cnt++;
		}
		cnt++;
		ptr++;
	}
	char *cooked = calloc(cnt, sizeof(char));
	assert(cooked);
	char *nptr = cooked;
	ptr = str;
	while (*ptr != '\0') {
		switch (*ptr) {
		case '\b':
			*nptr = '\\';
			nptr++;
			*nptr = 'b';
			break;
		case '\f':
			*nptr = '\\';
			nptr++;
			*nptr = 'f';
			break;
		case '\n':
			*nptr = '\\';
			nptr++;
			*nptr = 'n';
			break;
		case '\r':
			*nptr = '\\';
			nptr++;
			*nptr = 'r';
			break;
		case '\t':
			*nptr = '\\';
			nptr++;
			*nptr = 't';
			break;
		case '"':
			*nptr = '\\';
			nptr++;
			*nptr = '"';
			break;
		case '\\':
			*nptr = '\\';
			nptr++;
			*nptr = '\\';
			break;
		default:
			*nptr = *ptr;
		}
		ptr++;
		nptr++;
	}
	return cooked;
}

struct webhook_state *webhook_setup() {
	struct webhook_state *state = calloc(1, sizeof(struct webhook_state));
	assert(state);
	assert((state->curl = curl_easy_init()));
	curl_easy_setopt(state->curl, CURLOPT_URL, getenv("MNW_WEBHOOK"));
	state->displayname_escaped = json_string_escape(get_displayname());
	return state;
}

static int curl_send(struct webhook_state *state, const char *msg, int len) {
	CURLcode res;
	curl_easy_setopt(state->curl, CURLOPT_POSTFIELDS, msg);
	curl_easy_setopt(state->curl, CURLOPT_POSTFIELDSIZE, len);
	struct curl_slist *list = NULL;
	list = curl_slist_append(list, "Content-Type: application/json");
	curl_easy_setopt(state->curl, CURLOPT_HTTPHEADER, list);
	res = curl_easy_perform(state->curl);
	curl_slist_free_all(list);
	return res;
}

int webhook_send(struct webhook_state *state, const char *msg) {
	char *escaped_msg = json_string_escape(msg);
	int payload_length = strlen(escaped_msg) + strlen(webhook_template) +
		strlen(state->displayname_escaped) - 4;
	char *payload = calloc(1, sizeof(char) * (payload_length + 1));
	assert(payload);
	sprintf(payload, webhook_template, escaped_msg,
		state->displayname_escaped);
	free(escaped_msg);
	int res = curl_send(state, payload, payload_length);
	free(payload);
	return res;
}
