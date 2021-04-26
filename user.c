#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "shared.h"
void init(int, char**);

static char *prgName;

/* IPC variables */
static int shm_id = -1;
static int msq_id = -1;
static System *sys = NULL;
static Message msg;

void crash(char *msg) {
	char buf[BUFFER_LENGTH];
	snprintf(buf, BUFFER_LENGTH, "%s: %s", prgName, msg);
	perror(buf);
	
	exit(EXIT_FAILURE);
}

void init_IPC() {
	key_t key;

	if ((key = ftok(KEY_PATHNAME, KEY_ID_SYSTEM)) == -1) crash("ftok");
	if ((shm_id = shmget(key, sizeof(System), 0)) == -1) crash("shmget");
	if ((sys = (System*) shmat(shm_id, NULL, 0)) == (void*) -1) crash("shmat");

	if ((key = ftok(KEY_PATHNAME, KEY_ID_MESSAGE_QUEUE)) == -1) crash("ftok");
	if ((msq_id = msgget(key, 0)) == -1) crash("msgget");
}

int main(int argc, char *argv[]) {
	init(argc, argv);

	int sp_id = atoi(argv[1]);
	int schm = atoi(argv[2]);

	srand(time(NULL) ^ getpid());

	init_IPC();

	bool terminate = false;
	int referenceCount = 0;
	unsigned int addr = 0;
	unsigned int pg = 0;

	/* Decision loop */
	while (true) {
		/* Wait until we get a msg from OSS telling us it's our turn to "run" */
		msgrcv(msq_id, &msg, sizeof(Message), getpid(), 0);

		/* Continue getting addr if we haven't referenced to our limit (1000) */
		if (referenceCount <= 1000) {
			if (schm == RANDOM) {
				/* Execute simple schm algorithm */

				addr = rand() % 32768 + 0;
				pg = addr >> 10;
			} else if (schm == WEIGHTED) {
				/* Execute weighted schm algorithm */

				double weights[PAGE_COUNT];
				int i, j, p, r;
				double sum;
				i = 0;
				while(i < PAGE_COUNT)
				{
					weights[i] = 0;
					i++;
				}
				for (i = 0; i < PAGE_COUNT; i++) {
					sum = 0;
					for (j = 0; j <= i; j++)
						sum += 1 / (double) (j + 1);
					weights[i] = sum;
				}

				r = rand() % ((int) weights[PAGE_COUNT - 1] + 1);
				i =0;
				while(i < PAGE_COUNT)
				{
					if (weights[i] > r) {
						p = i;
						break;
					}
					i++;
				}
				
				addr = (p << 10) + (rand() % 1024);
				pg = p;
			} else crash("Unknown scheme!");

			referenceCount++;
		} else terminate = true;

		/* Send our decision to OSS */
		msg.type = 1;
		msg.terminate = terminate;
		msg.addr = addr;
		msg.pg = pg;
		msgsnd(msq_id, &msg, sizeof(Message), 0);

		if (terminate) break;
	}

	return sp_id;
}


void init(int argc, char **argv) {
	prgName ="Master";
	//prgName = argv[0];

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}