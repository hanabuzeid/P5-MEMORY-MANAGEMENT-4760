#include <ctype.h>
#include <errno.h>
#include <libgen.h>
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
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "list.h"
#include "queue.h"
#include "shared.h"

#define log _log

/* Simulation functions */
void sysInit();
void simulation();
void processesHandler();
void tryToSpawnTheProcess();
void spawnTheProcess(int);
void init_PCB(pid_t, int);
int find_avail_PID();
int clckAvance(int);

/* Program lifecycle functions */
void init(int, char **);
void usage(int);
void registerSgHandler();
void sgHandler(int);
void timer(int);
void init_IPC();
void free_IPC();

/* Utility functions */
void error(char *, ...);
void crash(char *);
void log(char *, ...);
void flog(char *, ...);
void sem_lock(const int);
void sem_unlock(const int);
void showSummary();
void showMemoryMap();

static char *prgName;
static volatile bool quit = false;
static bool debug = false;

/* IPC variables */
static int shm_id = -1;
static int msq_id = -1;
static int semid = -1;
static System *sys = NULL;
static Message msg;

/* Simulation variables */
static int schm = RANDOM;
static Que *que;	/* Process que */
static List *rfrnce; /* Reference string */
static List *stack;		/* LRU stack */
static SysTime nxt_spawn;
static int act_count = 0;
static int spawn_count = 0;
static int exit_count = 0;
static pid_t pids[PROCESSES_MAX];
static int memory[MAX_FRAMES];
static int count_mem_acc = 0;
static int count_pg_fault = 0;
static unsigned int tot_acc_time = 0;

int main(int argc, char *argv[])
{
	init(argc, argv);

	srand(time(NULL) ^ getpid());

	bool ok = true;

	/* Get program arguments */
	while (true)
	{
		int c = getopt(argc, argv, "hm:d");
		if (c == -1)
			break;
		switch (c)
		{
		case 'h':
			usage(EXIT_SUCCESS);
		case 'm':
			schm = atoi(optarg) - 1;
			if (!isdigit(*optarg) || (schm < 0 || schm > 1))
			{
				error("invalid request scheme '%s'", optarg);
				ok = false;
			}
			break;
		case 'd':
			debug = true;
			break;
		default:
			ok = false;
		}
	}

	/* Check for unknown arguments */
	if (optind < argc)
	{
		char buf[BUFFER_LENGTH];
		snprintf(buf, BUFFER_LENGTH, "found non-option(s): ");
		while (optind < argc)
		{
			strncat(buf, argv[optind++], BUFFER_LENGTH);
			if (optind < argc)
				strncat(buf, ", ", BUFFER_LENGTH);
		}
		error(buf);
		ok = false;
	}

	if (!ok)
		usage(EXIT_FAILURE);

	registerSgHandler();

	/* Clear log file */
	FILE *fp;
	if ((fp = fopen(PATH_LOG, "w")) == NULL)
		crash("fopen");
	if (fclose(fp) == EOF)
		crash("fclose");

	/* Setup simulation */
	init_IPC();
	memset(pids, 0, sizeof(pids));
	sys->clock.s = 0;
	sys->clock.ns = 0;
	nxt_spawn.s = 0;
	nxt_spawn.ns = 0;
	sysInit();
	que = newQueue();
	rfrnce = newList();
	stack = newList();

	/* Start simulating */
	simulation();

	showSummary();

	/* Cleanup resources */
	free_IPC();

	return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Simulation driver */
void simulation()
{
	/* Simulate run loop */
	while (true)
	{
		tryToSpawnTheProcess();
		clckAvance(0);
		processesHandler();
		clckAvance(0);

		/* Catch an exited user process */
		int status;
		pid_t p_id = waitpid(-1, &status, WNOHANG);
		if (p_id > 0)
		{
			int sp_id = WEXITSTATUS(status);
			pids[sp_id] = 0;
			act_count--;
			exit_count++;
		}

		/* Stop simulating if the last user process has exited */
		if (quit)
		{
			if (exit_count == spawn_count)
				break;
		}
		else
		{
			if (exit_count == PROCESSES_TOTAL)
				break;
		}
	}
}
void spawnTheProcess(int sp_id)
{
	/* Fork a new user process */
	pid_t p_id = fork();

	/* Record its PID */
	pids[sp_id] = p_id;

	if (p_id == -1)
		crash("fork");
	else if (p_id == 0)
	{
		/* Since child, execute a new user process */
		char arg0[BUFFER_LENGTH];
		char arg1[BUFFER_LENGTH];
		sprintf(arg0, "%d", sp_id);
		sprintf(arg1, "%d", schm);
		execl("./user", "user", arg0, arg1, (char *)NULL);
		crash("execl");
	}

	/* Since parent, initialize the new user process for simulation */
	init_PCB(p_id, sp_id);
	enqueue(que, sp_id);
	act_count++;
	spawn_count++;

	flog("p%d created\n", sp_id);
}
void flog(char *fmt, ...)
{
	FILE *fp = fopen(PATH_LOG, "a+");
	if (fp == NULL)
		crash("fopen");

	char buf[BUFFER_LENGTH];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, BUFFER_LENGTH, fmt, args);
	va_end(args);

	char buff[BUFFER_LENGTH];
	snprintf(buff, BUFFER_LENGTH, "%s[%d.%d] %s", basename(prgName), sys->clock.s, sys->clock.ns, buf);

	fprintf(stderr, buff);
	fprintf(fp, buff);

	fclose(fp);
}
/* Sends and receives messages from user processes, and acts upon them */
void processesHandler()
{
	QueNode *nxt = que->frnt;
	Que *temp = newQueue();

	/* While we have user processes to simulate */
	while (nxt != NULL)
	{
		clckAvance(0);

		/* Send a msg to a user process saying it's your turn to "run" */
		int sp_id = nxt->indx;
		msg.type = sys->p_table[sp_id].p_id;
		msg.sp_id = sp_id;
		msg.p_id = sys->p_table[sp_id].p_id;
		msgsnd(msq_id, &msg, sizeof(Message), 0);

		/* Receive a response of what they're doing */
		msgrcv(msq_id, &msg, sizeof(Message), 1, 0);

		clckAvance(0);

		if (msg.terminate)
		{
			showMemoryMap();

			flog("P%d has terminated, freeing memory\n", sp_id);

			/* Free process' frames */
			int i;
			for (i = 0; i < MAX_PAGES; i++)
			{
				if (sys->p_table[sp_id].p_table[i].frm != -1)
				{
					int frm = sys->p_table[sp_id].p_table[i].frm;
					removeFrmList(rfrnce, sp_id, i, frm);
					memory[frm / 8] &= ~(1 << (frm % 8));
				}
			}
		}
		else
		{
			int currFrm;
			tot_acc_time += clckAvance(1000000);
			enqueue(temp, sp_id);

			// Frame allocation procedure

			unsigned int reqAddr = msg.addr;
			unsigned int reqPg = msg.pg;

			if (sys->p_table[sp_id].p_table[reqPg].protec == 0)
			{
				flog("Process:%d request reading from the address %d-%d\n", sp_id, reqAddr, reqPg);
			}
			else
			{
				flog("Process:%d request writing to the address %d-%d\n", sp_id, reqAddr, reqPg);
			}

			count_mem_acc++;

			if (sys->p_table[sp_id].p_table[reqPg].valid == 0)
			{
				flog("Address %d-%d not in the frame, PAGEFAULT Error\n", reqAddr, reqPg);

				count_pg_fault++;

				tot_acc_time += clckAvance(10 * 1000000);

				/* Find available frame */
				bool isMemoryOpen = false;
				int frmCount = 0;
				while (true)
				{
					currFrm = (currFrm + 1) % MAX_FRAMES;
					if ((memory[currFrm / 8] & (1 << (currFrm % 8))) == 0)
					{
						isMemoryOpen = true;
						break;
					}
					if (frmCount >= MAX_FRAMES - 1)
						break;
					frmCount++;
				}

				/* Check if there is still space in memory */
				if (isMemoryOpen)
				{
					sys->p_table[sp_id].p_table[reqPg].frm = currFrm;
					sys->p_table[sp_id].p_table[reqPg].valid = 1;

					memory[currFrm / 8] |= (1 << (currFrm % 8));

					append(rfrnce, sp_id, reqPg, currFrm);
					flog("Allocated frame %d to Process:%d\n", currFrm, sp_id);

					removeFrmList(stack, sp_id, reqPg, currFrm);
					append(stack, sp_id, reqPg, currFrm);

					if (sys->p_table[sp_id].p_table[reqPg].protec == 0)
					{
						flog("Address %d-%d in frame %d, giving data to Process:%d\n", reqAddr, reqPg, sys->p_table[sp_id].p_table[reqPg].frm, sp_id);
						sys->p_table[sp_id].p_table[reqPg].dirty = 0;
					}
					else
					{
						flog("Address %d-%d in frame %d, writing data to Process:%d\n", reqAddr, reqPg, sys->p_table[sp_id].p_table[reqPg].frm, sp_id);
						sys->p_table[sp_id].p_table[reqPg].dirty = 1;
					}
				}
				else
				{
					/* Handle when memory is full */

					flog("Address %d-%d not in frame, memory is full\n", reqAddr, reqPg);

					unsigned int indx = stack->top->indx;
					unsigned int pg = stack->top->pg;
					unsigned int addr = pg << 10;
					unsigned int frm = stack->top->frm;

					if (sys->p_table[indx].p_table[pg].dirty == 1)
					{
						flog("Address %d-%d was fixed, writing back to disk\n", addr, pg);
					}

					/* Page replacement */
					sys->p_table[indx].p_table[pg].frm = -1;
					sys->p_table[indx].p_table[pg].dirty = 0;
					sys->p_table[indx].p_table[pg].valid = 0;
					sys->p_table[sp_id].p_table[reqPg].frm = frm;
					sys->p_table[sp_id].p_table[reqPg].dirty = 0;
					sys->p_table[sp_id].p_table[reqPg].valid = 1;
					removeFrmList(stack, indx, pg, frm);
					removeFrmList(rfrnce, indx, pg, frm);
					append(stack, sp_id, reqPg, frm);
					append(rfrnce, sp_id, reqPg, frm);

					if (sys->p_table[sp_id].p_table[reqPg].protec == 1)
					{
						sys->p_table[sp_id].p_table[reqPg].dirty = 1;
						flog("Dirty bit of frame %d , adding more time to the clock\n", currFrm);
					}
				}
			}
			 else
			{
				// Update LRU stack
				int frm = sys->p_table[sp_id].p_table[reqPg].frm;
				removeFrmList(stack, sp_id, reqPg, frm);
				append(stack, sp_id, reqPg, frm);

				if (sys->p_table[sp_id].p_table[reqPg].protec == 0)
				{
					flog("Address %d-%d already in frame %d, giving data to Process:%d\n", reqAddr, reqPg, sys->p_table[sp_id].p_table[reqPg].frm, sp_id);
				}
				else
				{
					flog("Address %d-%d already in frame %d, writing data to Process:%d\n", reqAddr, reqPg, sys->p_table[sp_id].p_table[reqPg].frm, sp_id);
				}
			} 
		}

		showMemoryMap();

		/* Reset msg */
		msg.type = -1;
		msg.sp_id = -1;
		msg.p_id = -1;
		msg.terminate = false;
		msg.pg = -1;

		/* On to the nxt user process to simulate */
		nxt = (nxt->nxt != NULL) ? nxt->nxt : NULL;
	}

	/* Reset the current queue */
	while (!isQueueEmpty(que))
		dequeue(que);
	while (!isQueueEmpty(temp))
	{
		int i = temp->frnt->indx;
		enqueue(que, i);
		dequeue(temp);
	}

	free(temp);
}

/* Attempts to spawn a new user process, but depends on the simulation's current state */
void tryToSpawnTheProcess()
{
	/* Guard statements checking if we can even attempt to spawn a user process */
	if (act_count >= PROCESSES_MAX)
		return;
	if (spawn_count >= PROCESSES_TOTAL)
		return;
	if (nxt_spawn.ns < (rand() % (500 + 1)) * (1000000 + 1))
		return;
	if (quit)
		return;

	/* Reset nxt spawn time */
	nxt_spawn.ns = 0;

	/* Try to find an available simulated PID */
	int sp_id = find_avail_PID();

	/* Guard statement checking if we did find a PID */
	if (sp_id == -1)
		return;

	/* Now we can spawn a simulated user process */
	spawnTheProcess(sp_id);
}
void log(char *fmt, ...)
{
	FILE *fp = fopen(PATH_LOG, "a+");
	if (fp == NULL)
		crash("fopen");

	char buf[BUFFER_LENGTH];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, BUFFER_LENGTH, fmt, args);
	va_end(args);

	fprintf(stderr, buf);
	fprintf(fp, buf);

	if (fclose(fp) == EOF)
		crash("fclose");
}
void sysInit()
{
	int i, j;

	/* Set default values in sys data structures */
	for (i = 0; i < PROCESSES_MAX; i++)
	{
		sys->p_table[i].p_id = -1;
		sys->p_table[i].sp_id = -1;
		for (j = 0; j < MAX_PAGES; j++)
		{
			sys->p_table[i].p_table[j].frm = -1;
			sys->p_table[i].p_table[j].protec = rand() % 2;
			sys->p_table[i].p_table[j].dirty = 0;
			sys->p_table[i].p_table[j].valid = 0;
		}
	}
}

void init_PCB(pid_t p_id, int sp_id)
{
	int i;

	/* Set default values in a user process' data structure */
	PCB *pcb = &sys->p_table[sp_id];
	pcb->p_id = p_id;
	pcb->sp_id = sp_id;
	for (i = 0; i < MAX_PAGES; i++)
	{
		pcb->p_table[i].frm = -1;
		pcb->p_table[i].protec = rand() % 2;
		pcb->p_table[i].dirty = 0;
		pcb->p_table[i].valid = 0;
	}
}

/* Returns values [0-PROCESSES_MAX] for a found available PID, otherwise -1 for not found */
int find_avail_PID()
{
	int i;
	for (i = 0; i < PROCESSES_MAX; i++)
		if (pids[i] == 0)
			return i;
	return -1;
}

void init(int argc, char **argv)
{
	//prgName ="Master";
	prgName = argv[0];

	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
}

void usage(int status)
{
	if (status != EXIT_SUCCESS)
		fprintf(stderr, "Try '%s -h' for more information\n", prgName);
	else
	{
		printf("Usage: %s [-m x] [-d]\n", prgName);
		printf("     -m x     : Request scheme (1 = RANDOM, 2 = WEIGHTED) (default 1)\n");
		printf("     -d       : Debug mode (default off)\n");
	}
	exit(status);
}

void error(char *fmt, ...)
{
	char buf[BUFFER_LENGTH];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, BUFFER_LENGTH, fmt, args);
	va_end(args);

	fprintf(stderr, "%s: %s\n", prgName, buf);

	free_IPC();
}

void registerSgHandler()
{
	struct sigaction sa;

	/* Set up SIGINT handler */
	if (sigemptyset(&sa.sa_mask) == -1)
		crash("sigemptyset");
	sa.sa_handler = &sgHandler;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGINT, &sa, NULL) == -1)
		crash("sigaction");

	/* Set up SIGALRM handler */
	if (sigemptyset(&sa.sa_mask) == -1)
		crash("sigemptyset");
	sa.sa_handler = &sgHandler;
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGALRM, &sa, NULL) == -1)
		crash("sigaction");

	/* Initialize timout timer */
	timer(TIMEOUT);

	signal(SIGSEGV, sgHandler);
}

void sgHandler(int sig)
{
	if (sig == SIGALRM)
		quit = true;
	else
	{
		showSummary();

		/* Kill all running user processes */
		int i;
		for (i = 0; i < PROCESSES_MAX; i++)
			if (pids[i] > 0)
				kill(pids[i], SIGTERM);
		while (wait(NULL) > 0)
			;

		free_IPC();
		exit(EXIT_SUCCESS);
	}
}

void timer(int duration)
{
	struct itimerval val;
	val.it_value.tv_sec = duration;
	val.it_value.tv_usec = 0;
	val.it_interval.tv_sec = 0;
	val.it_interval.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &val, NULL) == -1)
		crash("setitimer");
}

void init_IPC()
{
	key_t key;

	if ((key = ftok(KEY_PATHNAME, KEY_ID_SYSTEM)) == -1)
		crash("ftok");
	if ((shm_id = shmget(key, sizeof(System), IPC_EXCL | IPC_CREAT | PERMS)) == -1)
		crash("shmget");
	if ((sys = (System *)shmat(shm_id, NULL, 0)) == (void *)-1)
		crash("shmat");

	if ((key = ftok(KEY_PATHNAME, KEY_ID_MESSAGE_QUEUE)) == -1)
		crash("ftok");
	if ((msq_id = msgget(key, IPC_EXCL | IPC_CREAT | PERMS)) == -1)
		crash("msgget");

	if ((key = ftok(KEY_PATHNAME, KEY_ID_SEMAPHORE)) == -1)
		crash("ftok");
	if ((semid = semget(key, 1, IPC_EXCL | IPC_CREAT | PERMS)) == -1)
		crash("semget");
	if (semctl(semid, 0, SETVAL, 1) == -1)
		crash("semctl");
}

void free_IPC()
{
	if (sys != NULL && shmdt(sys) == -1)
		crash("shmdt");
	if (shm_id > 0 && shmctl(shm_id, IPC_RMID, NULL) == -1)
		crash("shmdt");

	if (msq_id > 0 && msgctl(msq_id, IPC_RMID, NULL) == -1)
		crash("msgctl");

	if (semid > 0 && semctl(semid, 0, IPC_RMID) == -1)
		crash("semctl");
}

int clckAvance(int ns)
{
	int r = (ns > 0) ? ns : rand() % (1 * 1000) + 1;

	/* Increment sys clock by random nanoseconds */
	sem_lock(0);
	nxt_spawn.ns += r;
	sys->clock.ns += r;
	while (sys->clock.ns >= (1000 * 1000000))
	{
		sys->clock.s++;
		sys->clock.ns -= (1000 * 1000000);
	}
	sem_unlock(0);

	return r;
}

void crash(char *msg)
{
	char buf[BUFFER_LENGTH];
	snprintf(buf, BUFFER_LENGTH, "%s: %s", prgName, msg);
	perror(buf);

	free_IPC();

	exit(EXIT_FAILURE);
}

void sem_lock(const int indx)
{
	struct sembuf sop = {indx, -1, 0};
	if (semop(semid, &sop, 1) == -1)
		crash("semop");
}

void showSummary() {
	double mem_access_per_sec = (double) count_mem_acc / (double) sys->clock.s;
	double pg_faults_per_mem_acc = (double) count_pg_fault / (double) count_mem_acc;
	double avg_mem_acc_speed = ((double) tot_acc_time / (double) count_mem_acc) / (double) 1000000;

	log("\n <<< << STATISTICS >>  >>>\n");
	log(" ___________________________________________");

	log("/n Total memory accesses per second: %f\n", mem_access_per_sec);
	log("\n Total page faults per memory access: %f\n", pg_faults_per_mem_acc);
	log("\n Total page fault count: %d\n", count_pg_fault);
	log("\n Total memory access count: %d\n", count_mem_acc);
	log("\n Total processes executed: %d\n", spawn_count);
	log(" ___________________________________________");
	log(">>\n SYSTEM TIME << : %d.%d\n", sys->clock.s, sys->clock.ns);
	
	
	
	
	log("Total average memory access speed: %f milliseconds\n", avg_mem_acc_speed);
	log("Total memory access time: %f milliseconds\n", (double) tot_acc_time / (double) 1000000);
}

void showMemoryMap()
{
	if (!debug)
		return;
	log("\n");
	/* Log the reference list */
	log(listString(rfrnce));
	/* Log the stack list */
	log(listString(stack));
	log("\n");
}

void sem_unlock(const int indx)
{
	struct sembuf sop = {indx, 1, 0};
	if (semop(semid, &sop, 1) == -1)
		crash("semop");
}
