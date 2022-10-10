/*
 * Driver code for airballoon problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

#define N_LORD_FLOWERKILLER 8
#define NROPES 16
static int ropes_left = NROPES;
static struct lock *ropes_left_lock;

struct hook {
	int rope_index;
};

struct stake {
	int rope_index;
	struct lock *stake_lock;
}; 

struct rope {
	volatile bool severed;
	struct lock *rope_lock; 
};

/* List of ropes that will be indirectly operated */
static struct rope ropelist[NROPES];
/* List of stakes that Marigold and Lord Flowerkiller will operate on*/
static struct stake stakelist[NROPES];
/* List of hooks that only Dandelion will operate on */
static struct hook hooklist[NROPES];

enum sever_location_type {
    HOOK,
	STAKE
};

/* Semaphores for ensuring that the balloon and main thread run after the escape */
struct semaphore *balloon_sema;
struct semaphore *main_sema;


/* Synchronization primitives */

/* Implement this! */

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 */

static
bool
sever_rope_attempt(int rope_index, enum sever_location_type sever_method, int location_index) {
	lock_acquire(ropelist[rope_index].rope_lock);
	if (!ropelist[rope_index].severed) {
		ropelist[rope_index].severed = true;
		
		if (sever_method == HOOK) {
			kprintf("Dandelion severed rope %d\n", rope_index);
		} else if (sever_method == STAKE) {
			kprintf("Marigold severed rope %d from stake %d\n", rope_index, location_index);
		}
		//safely decrement rope counter
		lock_acquire(ropes_left_lock);
		ropes_left--;
		lock_release(ropes_left_lock);

		lock_release(ropelist[rope_index].rope_lock);

		return true;
	}
	lock_release(ropelist[rope_index].rope_lock);
	return false;
}

static
void
dandelion(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Dandelion thread starting\n");

	/* Implement this function */
	while (true) {
		lock_acquire(ropes_left_lock);
		if (ropes_left <= 0){
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);


		int hook_index = random() % NROPES;
		bool successful_sever = sever_rope_attempt(hooklist[hook_index].rope_index, HOOK, hook_index);
		if (successful_sever) {
			V(balloon_sema);
			thread_yield();
		}
	}
	
	kprintf("Dandelion thread done\n");
	V(main_sema);
	thread_exit();
}

static
void
marigold(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Marigold thread starting\n");

	/* Implement this function */
	while (true) {
		lock_acquire(ropes_left_lock);
		if (ropes_left <= 0){
			lock_release(ropes_left_lock);
			break;
		}
		lock_release(ropes_left_lock);


		int stake_index = random() % NROPES;

		lock_acquire(stakelist[stake_index].stake_lock);
		bool successful_sever = sever_rope_attempt(stakelist[stake_index].rope_index, STAKE, stake_index);
		if (successful_sever) {
			V(balloon_sema);
			lock_release(stakelist[stake_index].stake_lock);
			thread_yield();
		}
		lock_release(stakelist[stake_index].stake_lock);
	}
	
	kprintf("Marigold thread done\n");
	V(balloon_sema);
	V(main_sema);
	thread_exit();
}

// static
// void
// flowerkiller(void *p, unsigned long arg)
// {
// 	(void)p;
// 	(void)arg;

// 	kprintf("Lord FlowerKiller thread starting\n");

// 	/* Implement this function */
// 	while (true) {
// 		lock_acquire(ropes_left_lock);
// 		if (ropes_left <= 1){
// 			lock_release(ropes_left_lock);
// 			break;
// 		}
// 		lock_release(ropes_left_lock);

// 	}
// }

static
void
balloon(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;
	kprintf("Balloon thread starting\n");

	for (int i = 0; i < NROPES; i++) {
		P(balloon_sema);
	}
	
	kprintf("Balloon freed and Prince Dandelion escapes!\n");

	kprintf("Balloon thread done\n");

	V(main_sema);
	thread_exit();
}


// Change this function as necessary
int
airballoon(int nargs, char **args)
{

	int err = 0, i;
	(void)i;

	(void)nargs;
	(void)args;
	(void)ropes_left;

// Initialization
	ropes_left = NROPES;
	for (int j = 0; j < NROPES; j++) {
		//Initialize indices of each data structures to the default value
		hooklist[j].rope_index = j;
		stakelist[j].rope_index = j;
		ropelist[j].severed = false;
		// Allocate memory for all locks
		stakelist[j].stake_lock = lock_create("Stakelist_lock");
		ropelist[j].rope_lock = lock_create("Ropelist lock");
	}
	//Initialize other locks and semaphores
	ropes_left_lock = lock_create("Remaining_Ropes_Lock");
	balloon_sema = sem_create("Balloon_Semaphore", 0);
	main_sema = sem_create("Main_Semaphore", 0);

// End of initialization
	err = thread_fork("Marigold Thread",
			  NULL, marigold, NULL, 0);
	if(err)
		goto panic;

	err = thread_fork("Dandelion Thread",
			  NULL, dandelion, NULL, 0);
	if(err)
		goto panic;
/*
	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}
*/
	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	for (int j = 0; j < 3; j++){
		P(main_sema);
	}
	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	kprintf("Main thread done\n");
	//Cleanup all dynamically allocated memory
	for (int j = 0; j < NROPES; j++) {
		lock_destroy(stakelist[j].stake_lock);
		lock_destroy(ropelist[j].rope_lock);
	}
	lock_destroy(ropes_left_lock);
	sem_destroy(balloon_sema);
	sem_destroy(main_sema);

	return 0;
}
