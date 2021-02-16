#ifndef UTIL_H
#define UTIL_H

struct list {
	struct list *next;
	struct list *prev;
	union {
		void *data;
		int data_int;
	};
};

struct list *list_new();
struct list *list_append(struct list *list, void *data);
struct list *list_appendi(struct list *list, int data_int);
void list_remove(struct list *list, struct list *item);
struct list *list_copyi(struct list *list);
void list_destroy(struct list *list);

#endif
