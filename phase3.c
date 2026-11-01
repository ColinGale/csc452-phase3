/*
 * File: phase3.c
 * Author: Colin Gale
 * Description: Simulates phase 3 of an operating system kernel. Phase 3 implements a bunch of 
 * syscall handlers that allows users to call request kernel functions. Since user code is dangerous,
 * these handlers must verify argumentation and NEVER crash the os, before calling the actual kernel
 * mode functions. The program also implements semaphores for process resource management. All mutual
 * exclusion in phase 3 is utilized with mailboxes. Blocking and unblocking processes also utilizes
 * the mailbox code from phase 2.
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "phase3.h"
#include "phase2.h"
#include "phase1.h"
#include "phase3_kernelInterfaces.h"

typedef struct Semaphore{
	int inUse;
	int val; // current count of semaphore
	int waiters; // processes sleeping in SemP()
	int mbox; // mailbox that the waiters sleep on
} Semaphore;

static Semaphore semTable[MAXSEMS];

/*
 * This struct allows us to pass the actual main function the user code wants to run
 * and its argument to the child created in spork. This is needed because the function passed
 * into spork is the trampoline function that sets code into user mode and terminates instead of
 * quits.
 */

typedef struct SporkData{
	int inUse;
	int (*func)(void *); // the user-main function to be passed to child after spork
	void *arg; // the user argument to the user-main function to child after spork
} SporkData;

static SporkData sporkTable[MAXPROC];

// (1 message slot, message size 0) ids of the mailboxes used as mutexes
static int semLockMbox;
static int spawnLockMbox;

// handler declarations
static void spawnHandler(USLOSS_Sysargs *args);
static void waitHandler(USLOSS_Sysargs *args);
static void terminateHandler(USLOSS_Sysargs *args);
static void semPHandler(USLOSS_Sysargs *args);
static void semVHandler(USLOSS_Sysargs *args);
static void semCreateHandler(USLOSS_Sysargs *args);
static void getTimeOfDayHandler(USLOSS_Sysargs *args);
static void getPidHandler(USLOSS_Sysargs *args);
static void dumpProcessesHandler(USLOSS_Sysargs *args);

// Forward declaration
static int userTrampoline(void *arg);
int Terminate(int rv);

void phase3_start_service_processes(void){

}
/*
 * Functions: lock() and unlock()
 * Purpose: These two functions allow mutual exclusion through mailboxes.
 * Before entering a critical section, "lock" by sending to a mailbox (indicating in use)
 * then recieve to "release"/"unlock" the section. 
 */
static void lock(int mbox){
	MboxSend(mbox, NULL, 0);
}

static void unlock(int mbox){
	MboxRecv(mbox, NULL, 0);
}

static SporkData *getSporkData(void){
	/*
	 * Function getSporkData()
	 * Purpose: This function retrieves an unused SporkData entry from the pool
	 * of available SporkData's in sporkTable. It utilizes a mailbox as a lock
	 * while it searches and modifies the table.
	 */
	SporkData *data = NULL;
	// Make sure we can not get interrupted as we modify the sporkDataTable
	lock(spawnLockMbox);

	for (int i = 0; i < MAXPROC; i++){
		if (sporkTable[i].inUse == 0){
			data = &sporkTable[i];
			sporkTable[i].inUse = 1;
			
			unlock(spawnLockMbox);
			return data;
		}
	}
	
	// no available slots found
	unlock(spawnLockMbox);
	return NULL;
}

static void freeSporkData(SporkData *data){
	/*
	 * Function: freeSporkData()
	 * Purpose: This function releases a SporkData entry back into the pool
	 * of available SporkDatas in sporkTable. It also utilizes a mailbox as a lock.
	 */
	lock(spawnLockMbox);
	data->inUse = 0;
	unlock(spawnLockMbox);
}


void phase3_init(void){
	/*
	 * Function: phase3_init()
	 * Purpose: This function initilizes the phase 3 tools by creating a mutex mailbox for
	 * both the semaphore table and the SporkData table, and then it fills out the syscall
	 * vector with the respective handler so user code can make syscalls.
	 */
	if ((USLOSS_PsrGet() & USLOSS_PSR_CURRENT_MODE) == 0){
		USLOSS_Console("ERROR: someone attempted to call phase3_init while in user mode!\n");
		USLOSS_Halt(1);
	}
	
	// Create mutex mailbox for semaphore table and SporkData table
	semLockMbox = MboxCreate(1, 0);
	spawnLockMbox = MboxCreate(1, 0);
	
	// Fill out the systemCallVec of phase 2 with the kernel functions to handle the user's syscall request
	systemCallVec[SYS_SPAWN] = spawnHandler;
	systemCallVec[SYS_WAIT] = waitHandler;
	systemCallVec[SYS_TERMINATE] = terminateHandler;
	systemCallVec[SYS_SEMCREATE] = semCreateHandler;
	systemCallVec[SYS_SEMP] = semPHandler;
	systemCallVec[SYS_SEMV] = semVHandler;
	systemCallVec[SYS_GETTIMEOFDAY] = getTimeOfDayHandler;
	systemCallVec[SYS_GETPID] = getPidHandler;
	systemCallVec[SYS_DUMPPROCESSES] = dumpProcessesHandler;

}

static void spawnHandler(USLOSS_Sysargs *args){
	/*
	 * Function: spawnHandler()
	 * Purpose: This serves as handler for the Spawn() function called by user code. Spawn()
	 * is the user's version of spork, so it calls spork to create a child process. This handler,
	 * like all other handlers, checks the user's passed arguments before calling the kernel
	 * function so that illegal arguments do not crash the os, but report error back to user.
	 */
	
	// Grab the user's arguments
	int (*func)(void *) =  (int (*)(void *)) args->arg1;
	void *userArg = args->arg2;
	int stackSize = (int)(long)args->arg3;
	int priority = (int)(long)args->arg4;
	char *name = (char *)args->arg5;

	// Check user code's arguments (we don't want to crash os if not valid)
	if (func == NULL || name == NULL || stackSize < USLOSS_MIN_STACK){
		// Tell user newly created process was not created
		args->arg1 = (void *)(long) -1;
		// Tell user that Spawn had bad argumentation
		args->arg4 = (void *)(long) -1;

		// Return instead of halting
		return;
	}
	
	// Fill out data structure to pass the actual function/argument after calling spork w/trampoline
	SporkData *data = getSporkData();
	if (data == NULL){
		args->arg1 = (void *)(long) -1;
		args->arg4 = (void *)(long) 0;
		return;
	}

	data->func = func;
	data->arg = userArg;

	// No lock is held here in case higher priority child runs in spork and needs to free its SporkData slot
	int pid = spork(name, userTrampoline, data, stackSize, priority);
	
	// Child was not created, free up SporkData (since child won't do it itself)
	if (pid < 0){
		freeSporkData(data);
		args->arg1 = (void *)(long) -1;
		args->arg4 = (void *)(long) 0;
		return;
	}
	
	// Report no issues back to user
	args->arg1 = (void *)(long) pid;
	args->arg4 = (void *)(long) 0;
}

static void terminateHandler(USLOSS_Sysargs *args){
	/*
	 * Function: terminateHandler()
	 * Purpose: This function handles a terminate call, which terminates the current process
	 * with a specified status. Unlike quit(), terminate can be called while process still
	 * has children, so terminates waits for all children to terminate before actually quiting.
	 */

	int status = (int)(long)args->arg1;
	int childStatus;
	
	// Wait for all children to terminate first
	while(join(&childStatus) != -2){}

	quit(status);
}

static void waitHandler(USLOSS_Sysargs *args){
	/*
	 * Function: waitHandler()
	 * Purpose: This function handles a wait call, which waits until a child terminates.
	 * Essentially a join wrapper.
	 *
	 */
	int status;
	int rc = join(&status);

	if (rc == -2){
		// current process has no children so join fails (pid and status undefined)
		args->arg4 = (void *)(long) rc;
		return;
	}
	
	// rc gives pid of joined child, return this to user code
	args->arg1 = (void *)(long) rc;
	args->arg2 = (void *)(long) status;
	// indicate a successful join
	args->arg4 = (void *)(long) 0;
}

/*
 * Functions: semCreateHandler(), semPHandler(), semVHandler()
 * Purpose: These functions handle the syscalls for creating a semaphore,
 * P()ing a semaphore (taking resource), and V()ing a semaphore (giving back a resource)
 *
 */

static void semCreateHandler(USLOSS_Sysargs *args){
	int value = (int)(long) args->arg1;
	int sem;
	int rc = kernSemCreate(value, &sem);
	
	// Report sem id or failure to caller
	args->arg1 = (void *)(long)(rc == 0 ? sem : 0);
	args->arg4 = (void *)(long) rc;
}

static void semPHandler(USLOSS_Sysargs *args){
	int sem = (int)(long) args->arg1;
	int rc = kernSemP(sem);

	args->arg4 = (void *)(long) rc;
}

static void semVHandler(USLOSS_Sysargs *args){
	int sem = (int)(long) args->arg1;
	int rc = kernSemV(sem);

	args->arg4 = (void *)(long) rc;
}

static void getTimeOfDayHandler(USLOSS_Sysargs *args){
	// Set arg1 of args to the value return by running currentTime(). This is returned to the user.
	args->arg1 = (void *)(long) currentTime();
}

static void getPidHandler(USLOSS_Sysargs *args){
	// Set arg1 of args to the value returned by running the function getpid()
	args->arg1 = (void *)(long) getpid();
}

static void dumpProcessesHandler(USLOSS_Sysargs *args){
	// dump processes does not return anything so we can just run it for the user
	dumpProcesses();
}

static int userTrampoline(void *arg){
	/*
	 * Function: userTrampoline()
	 * Purpose: This function is what is actually passed into the kernel's spork call, acting
	 * as the new processes main function. It is used to set the Psr into user mode (so user code
	 * can't do anything evil), retrieve both the actual function and actual argument the user code wants
	 * to run (since spork's function had to be userTrampoline), and lastly calls terminate after running
	 * instead of quit.
	 */

	// Grab the actual user-main function and argument we want to run
	SporkData *data = (SporkData *)arg;

	int (*func)(void *) = data->func;
	void *userArg = data->arg;
	freeSporkData(data);
	
	// Set the psr to user mode before calling the user's function (check failure)
	if (USLOSS_PsrSet(USLOSS_PsrGet() & ~USLOSS_PSR_CURRENT_MODE) != 0){
		USLOSS_Console("ERROR: USLOSS_PsrGet() failed in the user trampoline!\n");
		USLOSS_Halt(1);
	}
	
	// Calls user function
	int rv = func(userArg);
	// Cleans up the user's process and gets the return value to the user
	Terminate(rv);
	return 0; // unreachable
}

/*
 * Functions: kernSemCreate(), kernSemP(), kernSemV()
 * Purpose: These functions are the actual implementations that the kernel runs
 * to create, P(), and V() semaphores. A Semaphore object has a inUse flag, a
 * value (representing its resources), a number of processes waiting in P(),
 * and a mailbox id that the waiters actually wait at.

 */


int kernSemCreate(int value, int *semaphore){
	if (value < 0 || semaphore == NULL) return -1;
	
	// Lock everyone else from accessing semaphore table
	lock(semLockMbox);
	
	// Find id before grabbing/modifying table bc we need to get a mailbox too
	int id = -1;
	for (int i = 0; i < MAXSEMS; i++){
		if (semTable[i].inUse == 0){
			id = i;
			break;
		}
	}
	
	// No available semaphores
	if (id == -1){
		unlock(semLockMbox);
		return -1;
	}
	
	// This is used to block processes that are waiting for a semaphore's resources
	int mbox = MboxCreate(MAXPROC, 0);
	// No available mailboxes (don't need to release semTable bc we didn't modify yet)
	if (mbox < 0){
		unlock(semLockMbox);
		return -1;
	}
	
	semTable[id].inUse = 1;
	semTable[id].val = value;
	semTable[id].waiters = 0;
	semTable[id].mbox = mbox;

	unlock(semLockMbox);
	// Pass the semaphore table id back to whoever called, report no issues
	*semaphore = id;
	return 0;
}

int kernSemP(int semaphore){
	if (semaphore < 0 || semaphore >= MAXSEMS) return -1;

	lock(semLockMbox);

	Semaphore *s = &semTable[semaphore];

	if (s->inUse == 0){
		unlock(semLockMbox);
		return -1;
	}
	
	// If available resource, take resource
	if (s->val > 0){
		s->val--;
		unlock(semLockMbox);
		return 0;
	}
	
	// Record that we are waiting before giving up lock
	s->waiters++;
	int mbox = s->mbox;
	// Don't go to sleep holding a lock
	unlock(semLockMbox);
	
	// Sleep until someone V's this semaphore and we can take a resource
	MboxRecv(mbox, NULL, 0);
	return 0;
}

int kernSemV(int semaphore){
	if (semaphore < 0 || semaphore >= MAXSEMS) return -1;

	lock(semLockMbox);

	Semaphore *s = &semTable[semaphore];

	if (s->inUse == 0){
		unlock(semLockMbox);
		return -1;
	}
	
	// flag for waking so we can update semaphore and release lock
	int wake = 0;
	int mbox = s->mbox;
	
	// if any processes are waiting in P(), we wake them up instead of updating semaphore
	if (s->waiters > 0){
		s->waiters--;
		wake = 1;
	}
	else{
		s->val++;
	}

	unlock(semLockMbox);

	// Only send after unlocking so if switch to the waking process, we don't still hold lock to semTable
	if (wake) MboxSend(mbox, NULL, 0);
	return 0;
}

