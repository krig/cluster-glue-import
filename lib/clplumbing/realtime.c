#include <sys/types.h>
#include <unistd.h>
#ifdef _POSIX_MEMLOCK
#	include <sys/mman.h>
#endif
#ifdef _POSIX_PRIORITY_SCHEDULING
#	include <sched.h>
#endif
#include <string.h>
#include <clplumbing/cl_log.h>
#include <clplumbing/realtime.h>

static gboolean	realtimepermitted = TRUE;

/*
 *	Make us behave like a soft real-time process.
 *	We need scheduling priority and being locked in memory.
 *	If you ask us nicely, we'll even grow the heap for you
 *	before locking you into memory ;-).
 */
void
make_realtime(int spolicy, int priority,  int heapgrowK)
{

#ifdef SCHED_RR
#	define DEFAULT_REALTIME	SCHED_RR
#endif

#ifdef DEFAULT_REALTIME
	struct sched_param	sp;
	int			staticp;

	if (!realtimepermitted) {
		cl_log(LOG_INFO
		,	"Request to set pid %ld to realtime ignored."
		,	(long)getpid());
		return;
	}

	if (spolicy <= 0) {
		spolicy = DEFAULT_REALTIME;
	}

	if (priority <= 0) {
		priority = sched_get_priority_min(spolicy);
	}

	if (priority > sched_get_priority_max(spolicy)) {
		priority = sched_get_priority_max(spolicy);
	}

	if (heapgrowK < 0) {
		heapgrowK = 0;
	}


	if ((staticp=sched_getscheduler(0)) < 0) {
		cl_perror("unable to get scheduler parameters.");
	}else{
		memset(&sp, 0, sizeof(sp));
		sp.sched_priority = priority;

		if (sched_setscheduler(0, spolicy, &sp) < 0) {
			cl_perror("Unable to set scheduler parameters.");
		}
	}
#endif

#ifdef MCL_FUTURE
#ifdef	HAVE_USABLE_BRK
	{
	/*
	 *	Try and pre-allocate a little memory before locking
	 *	ourselves into memory...
	 */
		long	currbrk;

		currbrk = brk(NULL);

		if (currbrk >= 0) {
			if (brk((void*)(currbrk+heapgrowK*1024)) < 0) {
				cl_log(LOG_ERR
				,	"Got bad return from brk(0x%x)"
				,	heapgrowK*1024);
			}
		}else{
			cl_log(LOG_INFO
			,	"Could not retrieve current brk value");
		}
	}
#endif
	if (mlockall(MCL_FUTURE) < 0) {
		cl_perror("Unable to lock pid %d in memory", (int) getpid());
	}else{
		cl_log(LOG_INFO, "pid %d locked in memory.", (int) getpid());
	}
#endif
}

void
make_normaltime()
{
#ifdef DEFAULT_REALTIME
	struct sched_param	sp;

	memset(&sp, 0, sizeof(sp));
	sp.sched_priority = 0;
	if (sched_setscheduler(0, SCHED_OTHER, &sp) < 0) {
		cl_log(LOG_ERR, "unable to (re)set scheduler parameters.");
	}
#endif
#ifdef _POSIX_MEMLOCK
	/* Not strictly necessary. */
	munlockall();
#endif
}

void
disable_realtime(void)
{
	realtimepermitted = FALSE;
}
