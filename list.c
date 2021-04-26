#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "list.h"

#define BUFFER_LENGTH 4096

List *newList()
{
    List *list = (List *)malloc(sizeof(List));
    list->top = NULL;
    return list;
}

NodeOfList *makeListNode(int indx, int pg, int frm)
{
    NodeOfList *temp = (NodeOfList *)malloc(sizeof(NodeOfList));
    temp->indx = indx;
    temp->pg = pg;
    temp->frm = frm;
    temp->nxt = NULL;
    return temp;
}

void append(List *list, int indx, int pg, int frm)
{
    NodeOfList *temp = makeListNode(indx, pg, frm);

    if (list->top == NULL)
    {
        list->top = temp;
        return;
    }

    NodeOfList *nextHead = list->top;
    while (nextHead->nxt != NULL)
        nextHead = nextHead->nxt;
    nextHead->nxt = temp;
}

char *listString(const List *list)
{
    char buf[BUFFER_LENGTH];
    NodeOfList *nxtNode = list->top;

    if (nxtNode == NULL)
        return strdup(buf);

    sprintf(buf, "List:");
    while (nxtNode != NULL)
    {
        sprintf(buf, "%s (%d | %d | %d)", buf, nxtNode->indx, nxtNode->pg, nxtNode->frm);
        nxtNode = (nxtNode->nxt != NULL) ? nxtNode->nxt : NULL;
        if (nxtNode != NULL)
            sprintf(buf, "%s, ", buf);
    }
    sprintf(buf, "%s\n", buf);

    return strdup(buf);
}

int removeFrmList(List *list, int indx, int pg, int frm)
{
    NodeOfList *currHead = list->top;
    NodeOfList *prevTop = NULL;

    if (currHead == NULL)
        return -1;

    while (currHead->indx != indx || currHead->pg != pg || currHead->frm != frm)
    {
        if (currHead->nxt == NULL)
            return -1;
        else
        {
            prevTop = currHead;
            currHead = currHead->nxt;
        }
    }

    if (currHead == list->top)
    {
        int n = currHead->frm;
        free(currHead);
        list->top = list->top->nxt;
        return n;
    }
    else
    {
        int n = prevTop->nxt->frm;
        free(prevTop->nxt);
        prevTop->nxt = currHead->nxt;
        return n;
    }
}

void pop(List *list)
{
    if (list->top == NULL)
        return;

    NodeOfList *nodeTop = list->top;
    list->top = list->top->nxt;
    free(nodeTop);
}

bool isContains(List *list, int key)
{
    NodeOfList tempNode;
    tempNode.nxt = list->top;

    if (tempNode.nxt == NULL)
        return false;

    while (tempNode.nxt->frm != key)
    {
        if (tempNode.nxt->nxt == NULL)
            return false;
        else
            tempNode.nxt = tempNode.nxt->nxt;
    }

    return true;
}

char *strdup(const char *src)
{
    size_t length = strlen(src) + 1;
    char *dest = malloc(length);
    if (dest == NULL)
    {
        return NULL;
    }
    memcpy(dest, src, length);
    return dest;
}
