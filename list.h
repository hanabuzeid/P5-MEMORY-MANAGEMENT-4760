#ifndef LIST_H
#define LIST_H

typedef struct NodeOfList {
	int indx;
	int pg;
	int frm;
	struct NodeOfList *nxt;
} NodeOfList;

typedef struct {
	NodeOfList *top;
} List;

List *newList();
void append(List*, int, int, int);
void pop(List*);
int removeFrmList(List*, int, int, int);
bool isContains(List*, int);
char *listString(const List*);

#endif
