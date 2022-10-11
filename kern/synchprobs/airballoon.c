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
/* Lock protecting the above variable*/
static struct lock *ropes_left_lock; 

/* Hook data structure */
struct hook {
	volatile int rope_index;
};

/* Stake data structure */
struct stake {
	volatile int rope_index;
	struct lock *stake_lock;
}; 

/* Rope data structure */
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

/* Semaphore for ensuring that the balloon thread runs after all ropes have been severed */
struct semaphore *balloon_sema;
/* Semaphore for ensuring that the main thread runs after all other threads have exited */
struct semaphore *main_sema;

/*
 * Describe your design and any invariants or locking protocols
 * that must be maintained. Explain the exit conditions. How
 * do all threads know when they are done?
 * 
 * Only Dandelion accesses the hooks so no need to lock that
 * Marigold and Lord Flowerkiller both access stakes so a lock is needed so only one can modify a given item
 * in the stakelist at a time
 * 
 * ropelist is accessed indirectly via the "ropeindex" of the hook or stake. Only one thread may operate a given item 
 * in the ropelist at a time. 
 * 
 * Exit condition of Dandelion and Marigold is when all ropes are severed, which is when ropes_left = 0
 * 
 * Exit condition of Lord Flowerkiller is when ropes_left = 1. This is because when there is only 1 rope left there is nothing to switch
 * 
 * Whenever a rope is cut, the semaphore that the balloon thread uses is decrement. The balloon thread wakes up when the balloon_sema is decremented
 * 16 times, which is equal to the number of ropes
 * 
 * Every thread above that is not the main thread itself will decrement the main_sema. When all other threads are done then the main thread will reawaken
 * to do cleanup
 * 
 */

/* 
	Generic function for cutting the rope safely. We pass the parameters "sever_method" and "location_index"
	to ensure that the correct prints are done as per the requirements, 
	otherwise the function for cutting the rope works the same for both Dandelion and Marigold.

	Return true if the rope was severed, otherwise false.
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

	while (true) {
		//Check if there are any ropes left, if all ropes are severed there is nothing to do
		if (ropes_left <= 0){
			break;
		}


		int hook_index = random() % NROPES;
		//we try to sever the rope, if it exists
		bool successful_sever = sever_rope_attempt(hooklist[hook_index].rope_index, HOOK, hook_index);

		//if the rope was successfully severed, decrement the balloon semaphore and yield the thread
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

	while (true) {
		//Check if there are any ropes left, if all ropes are severed there is nothing to do
		if (ropes_left <= 0){
			break;
		}


		int stake_index = random() % NROPES;

		//Since both marigold and lord flowerkiller both access the stakelist, we need to lock it unlike Dandelion who has the hooks to himself
		lock_acquire(stakelist[stake_index].stake_lock);
		//we try to sever the rope, if it exists
		bool successful_sever = sever_rope_attempt(stakelist[stake_index].rope_index, STAKE, stake_index);

		//if the rope was successfully severed, decrement the balloon semaphore and yield the thread
		if (successful_sever) {
			V(balloon_sema);
			lock_release(stakelist[stake_index].stake_lock);
			thread_yield();
		}
		//Release the statelist
		lock_release(stakelist[stake_index].stake_lock);
	}
	
	kprintf("Marigold thread done\n");
	V(balloon_sema);
	V(main_sema);
	thread_exit();
}

static
void
flowerkiller(void *p, unsigned long arg)
{
	(void)p;
	(void)arg;

	kprintf("Lord FlowerKiller thread starting\n");

	/* Implement this function */
	while (true) {
		//Check if there are enough ropes left to switch.
		if (ropes_left <= 1){
			break;
		}
		
		int stake1;
		int stake2;

		int stake_index1;
		int stake_index2;
		stake1 = random() % NROPES;
		stake2 = random() % NROPES;
		
		// Apparently sometimes, 2 flowerkiller may grab the "inverse" of another flowerkiller's rope leading to a deadlock. 
		// The solution to this is to ensure that one stake index grabbed is consistently lower or higher than the 
		// other stake grabbed
		stake_index1 = stake1 < stake2 ? stake1 : stake2;
		stake_index2 = stake1 < stake2 ? stake2 : stake1;

		if (stake_index1 == stake_index2) {
			continue;
		}
		
		lock_acquire(stakelist[stake_index1].stake_lock);
		lock_acquire(ropelist[stakelist[stake_index1].rope_index].rope_lock);
		//If the first stake's rope is severed we release the locks and try again
		if (ropelist[stakelist[stake_index1].rope_index].severed) {
			lock_release(ropelist[stakelist[stake_index1].rope_index].rope_lock);
			lock_release(stakelist[stake_index1].stake_lock);
			continue;
		}

		lock_acquire(stakelist[stake_index2].stake_lock);
		lock_acquire(ropelist[stakelist[stake_index2].rope_index].rope_lock);

		//If the second stake's rope is severed we release the lock and try again
		if (ropelist[stakelist[stake_index2].rope_index].severed) {
			lock_release(ropelist[stakelist[stake_index2].rope_index].rope_lock);
			lock_release(stakelist[stake_index2].stake_lock);
			lock_release(ropelist[stakelist[stake_index1].rope_index].rope_lock);
			lock_release(stakelist[stake_index1].stake_lock);
			continue;
		} else {
			//If we get here it means that the conditions are set for this Flowerkiller thread to switch ropes and then yield
			int temp = stakelist[stake_index1].rope_index;
			stakelist[stake_index1].rope_index = stakelist[stake_index2].rope_index;
			stakelist[stake_index2].rope_index = temp;
			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n",stakelist[stake_index1].rope_index,stake_index2,stake_index1);
			kprintf("Lord FlowerKiller switched rope %d from stake %d to stake %d\n",stakelist[stake_index2].rope_index,stake_index1,stake_index2);
			lock_release(ropelist[stakelist[stake_index2].rope_index].rope_lock);
			lock_release(stakelist[stake_index2].stake_lock);
			lock_release(ropelist[stakelist[stake_index1].rope_index].rope_lock);
			lock_release(stakelist[stake_index1].stake_lock);
			thread_yield();
		}
	}

	kprintf("Lord FlowerKiller thread done\n");
	V(main_sema);
	thread_exit();
}

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

	for (i = 0; i < N_LORD_FLOWERKILLER; i++) {
		err = thread_fork("Lord FlowerKiller Thread",
				  NULL, flowerkiller, NULL, 0);
		if(err)
			goto panic;
	}

	err = thread_fork("Air Balloon",
			  NULL, balloon, NULL, 0);
	if(err)
		goto panic;

	// Put Main thread to sleep while all the others threads are doing their business
	for (int j = 0; j < N_LORD_FLOWERKILLER + 3; j++){
		P(main_sema);
	}
	goto done;
panic:
	panic("airballoon: thread_fork failed: %s)\n",
	      strerror(err));

done:
	//Cleanup all dynamically allocated memory
	for (int j = 0; j < NROPES; j++) {
		lock_destroy(stakelist[j].stake_lock);
		lock_destroy(ropelist[j].rope_lock);
	}
	lock_destroy(ropes_left_lock);
	sem_destroy(balloon_sema);
	sem_destroy(main_sema);

	kprintf("Main thread done\n");

	return 0;
}
