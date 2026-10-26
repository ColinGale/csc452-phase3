
#include <usloss.h>
#include <phase1.h>
#include <phase2.h>
#include <phase3.h>

#include "phase3_usermode.h"

#include <stdio.h>
#include <assert.h>



void startup(int argc, char **argv)
{
    phase1_init();
    phase2_init();
    phase3_init();
    dispatcher();
}



/* force the testcase driver to priority 1, instead of the
 * normal priority for testcase_main
 */
int start3(void*);
static int start3_trampoline(void*);

int testcase_main()
{
    int pid_spork, pid_join;
    int status;

    pid_spork = spork("start3", start3_trampoline, "start3", 4*USLOSS_MIN_STACK, 3);
    pid_join = join(&status);

    if (pid_join != pid_spork)
    {
        USLOSS_Console("*** TESTCASE FAILURE *** - the join() pid doesn't match the spork() pid.  %d/%d\n", pid_spork,pid_join);
        USLOSS_Halt(1);
    }

    return status;
}

static int start3_trampoline(void *arg)
{
    if (USLOSS_PsrSet(USLOSS_PSR_CURRENT_INT) != USLOSS_DEV_OK)
    {
        USLOSS_Console("ERROR: Could not disable kernel mode.\n");
        USLOSS_Halt(1);
    }

    int rc = start3(arg);

    Terminate(rc);
    return 0;    // Terminate() should never return
}



USLOSS_PTE *phase5_mmu_pageTable_alloc(int pid)
{
    return NULL;
}

void phase5_mmu_pageTable_free(int pid, USLOSS_PTE *page_table)
{
    assert(page_table == NULL);
}



void phase4_start_service_processes()
{
    USLOSS_Console("%s() called -- currently a NOP\n", __func__);
}

void phase5_start_service_processes()
{
    USLOSS_Console("%s() called -- currently a NOP\n", __func__);
}



void finish(int argc, char **argv)
{
    USLOSS_Console("%s(): The simulation is now terminating.\n", __func__);
}

void test_setup  (int argc, char **argv) {}
void test_cleanup(int argc, char **argv) {}

