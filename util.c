#include "util.h"

#include <assert.h>
#include <stdlib.h>

struct list *list_new() { //sen
	struct list *list = calloc(1, sizeof(struct list));
	assert(list);
	list->prev = list;
	return list;
}

struct list *list_appendi(struct list *list, int data_int) {
	struct list *n_list = list_new();
	n_list->data_int = data_int;
	n_list->prev = list->prev;
	list->prev->next = n_list;
	list->prev = n_list;
	return n_list;
}

struct list *list_append(struct list *list, void *data) {
	struct list *n_list = list_new();
	n_list->data = data;
	n_list->prev = list->prev;
	list->prev->next = n_list;
	list->prev = n_list;
	return n_list;
}

void list_remove(struct list *list, struct list *item) {
	item->prev->next = item->next;
	if (item->next) {
		item->next->prev = item->prev;
	} else {
		list->prev = item->prev;
	}
	free(item);
}

struct list *list_copyi(struct list *list) {
	if (list == NULL) {
		return NULL;
	}
	struct list *ptr = list, *nlist = list_new();
	while (ptr->next) {
		ptr = ptr->next;
		list_appendi(nlist, ptr->data_int);
	}
	return nlist;
}

void list_destroy(struct list *list) {
	if (list == NULL) {
		return;
	}
	struct list *ptr = list->prev, *optr;
	while (ptr != list) {
		optr = ptr;
		ptr = ptr->prev;
		free(optr);
	}
	free(list);
}
