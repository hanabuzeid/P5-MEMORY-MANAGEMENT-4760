#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "queue.h"

Que *newQueue() {
	Que *que = (Que*) malloc(sizeof(Que));
	que->frnt = NULL;
	que->tail = NULL;
	que->count = 0;
	return que;
}

QueNode *makeQueueNode(int indx) {
	QueNode *temp = (QueNode*) malloc(sizeof(QueNode));
	temp->indx = indx;
	temp->nxt = NULL;
	return temp;
}

void enqueue(Que *que, int indx) {
	QueNode *temp = makeQueueNode(indx);
	que->count++;
	if (que->tail == NULL) {
		que->frnt = que->tail = temp;
		return;
	}
	que->tail->nxt = temp;
	que->tail = temp;
}

QueNode *dequeue(Que *que) {
	if (que->frnt == NULL) return NULL;
	QueNode *temp = que->frnt;
	free(temp);
	que->frnt = que->frnt->nxt;
	if (que->frnt == NULL) que->tail = NULL;
	que->count--;
	return temp;
}

void removeFromQueue(Que *que, int indx) {
	Que *temp = newQueue();
	QueNode *current = que->frnt;
	while (current != NULL) {
		if (current->indx != indx) enqueue(temp, current->indx);
		current = (current->nxt != NULL) ? current->nxt : NULL;
	}
	while (!isQueueEmpty(que))
		dequeue(que);
	while (!isQueueEmpty(temp)) {
		enqueue(que, temp->frnt->indx);
		dequeue(temp);
	}
	free(temp);
}

bool isQueueEmpty(Que *que) {
	if (que->tail == NULL) return true;
	return false;
}

int sizeOfQueue(Que *que) {
	return que->count;
}
