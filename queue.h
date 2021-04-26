#ifndef QUEUE_H
#define QUEUE_H

#include <stdbool.h>

typedef struct QueNode {
	int indx;
	struct QueNode *nxt;
} QueNode;

typedef struct {
	QueNode *frnt;
	QueNode *tail;
	int count;
} Que;

Que *newQueue();
QueNode *makeQueueNode(int);
void enqueue(Que*, int);
QueNode *dequeue(Que*);
void removeFromQueue(Que*, int);
bool isQueueEmpty(Que*);
int sizeOfQueue(Que*);

#endif
