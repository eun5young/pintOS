#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#include "threads/fixed_point.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

static struct list sleep_list; /* Declaring a list variable consisting of only threads in the sleep state*/

/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */

// bool cmp_priority(const struct list_elem *, const struct list_elem *, void *);
void refresh_priority(void);
void remove_with_lock(struct lock *lock);
// bool wakeup_time_cmp(const struct list_elem *, const struct list_elem *, void *);
bool thread_compare_priority (const struct list_elem *, const struct list_elem *, void *);
void thread_test_preemption (void);
bool thread_compare_donate_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED);

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&all_list);
  list_init (&sleep_list); /* Initialize the inner pointer of the list (set to an empty list) */

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
//과제 1-3
int load_avg;

void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);

  //과제 1-3
  load_avg = LOAD_AVG_DEFAULT;
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();

  // 1-3 고쳐보는중
  // if (thread_mlfqs) {
  //   if (t != idle_thread)
  //     t->recent_cpu = add_mixed(t->recent_cpu, 1);

  //   if (timer_ticks() % TIMER_FREQ == 0) {
  //     mlfqs_calculate_load_avg();
  //     mlfqs_recalculate_recent_cpu();
  //   }

  //   if (timer_ticks() % 4 == 0) {
  //     mlfqs_recalculate_priority();
  //   }
  // }

}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();

  /* Prepare thread for first run by initializing its stack.
     Do this atomically so intermediate values for the 'stack' 
     member cannot be observed. */
  old_level = intr_disable ();

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  /* Add to run queue. */
  thread_unblock (t);
  thread_test_preemption();//이거 추가..

  // if (!intr_context() && t->priority > thread_get_priority()) {
  //   thread_yield();
  // }

  return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_insert_ordered (&ready_list, &t->elem, thread_compare_priority, 0); //Insert to ready_list in order of priority
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
  }

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_insert_ordered(&ready_list, &cur->elem, thread_compare_priority, 0); //여기 바꿔줌
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  //바로 밑 부분: 과제 1-3: 비활성화
  if (thread_mlfqs)
    return;
  
  thread_current ()->priority = new_priority;

  // struct thread *cur = thread_current();	

  // cur->init_priority = new_priority;

  refresh_priority();
  thread_test_preemption ();

  // if (!list_empty(&ready_list)) {
  //   struct thread *front = list_entry(list_front(&ready_list), struct thread, elem);
  //   if (front->priority > cur->priority) {
  //     thread_yield();
  //   }
  // }
}

void refresh_priority(void) {
  struct thread *cur = thread_current();

  // Restore to the original (initial) priority first
  cur->priority = cur->init_priority;

  // If there are any remaining donations,
  // update to the highest donated priority
  if (!list_empty(&cur->donations)) {
    // struct list_elem *e;
    // for (e = list_begin(&cur->donations);
    //      e != list_end(&cur->donations);
    //      e = list_next(e)) {
    //   struct thread *t = list_entry(e, struct thread, donation_elem);
    //   if (t->priority > cur->priority) {
    //     cur->priority = t->priority;
    //   }
    // }
    list_sort (&cur->donations, thread_compare_donate_priority, 0);

    struct thread *front = list_entry (list_front (&cur->donations), struct thread, donation_elem);
    if (front->priority > cur->priority)
      cur->priority = front->priority;
  }
}


/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
  // 현재 스레드의 nice 값을 새 값으로 설정
  enum intr_level old_level = intr_disable ();
  thread_current ()->nice = nice;
  mlfqs_calculate_priority (thread_current ());
  thread_test_preemption ();
  intr_set_level (old_level);
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  // 현재 스레드의 nice 값을 반환
  enum intr_level old_level = intr_disable ();
  int nice = thread_current ()-> nice;
  intr_set_level (old_level);
  return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  // 현재 시스템의 load_avg * 100 값을 반환
  enum intr_level old_level = intr_disable ();
  int load_avg_value = fp_to_int_round (mult_mixed (load_avg, 100));
  intr_set_level (old_level);
  return load_avg_value;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  // 현재 스레드의 recent_cpu * 100 값을 반환
  enum intr_level old_level = intr_disable ();
  int recent_cpu= fp_to_int_round (mult_mixed (thread_current ()->recent_cpu, 100));
  intr_set_level (old_level);
  return recent_cpu;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;

  t->init_priority = priority; //Save Initial Priority
  list_init(&t->donations); //Initialize donation list
  t->wait_on_lock = NULL; //At first, there's no waiting lock

  t->magic = THREAD_MAGIC;
  list_push_back (&all_list, &t->allelem);

  //과제 1-3 
  t->nice = NICE_DEFAULT;
  t->recent_cpu = RECENT_CPU_DEFAULT;
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);

void
thread_sleep (int64_t ticks) // ✅ wakeup_tick으로 직접 받는다
{
  struct thread *cur;
  enum intr_level old_level;

  old_level = intr_disable ();  // interrupt off
  cur = thread_current ();

  ASSERT (cur != idle_thread);

  cur->wake_up_time = ticks; // 더 이상 timer_ticks() 더하지 않음
  list_push_back(&sleep_list, &cur->elem);     // sleep_list에 넣기
  thread_block();

  intr_set_level(old_level);
}

void
thread_wakeup (int64_t ticks)
{
  struct list_elem *e = list_begin (&sleep_list);

  while (e != list_end (&sleep_list)){
    struct thread *t = list_entry (e, struct thread, elem);
    if (t->wake_up_time <= ticks){    // Make sure it's time for the thread to happen
      e = list_remove (e);      // sleep list
      thread_unblock (t);       // thread unblock
    }
    else
      e = list_next (e);
    }
}


// bool
// cmp_priority(const struct list_elem *a,
//              const struct list_elem *b,
//              void *aux UNUSED)
// {
//   const struct thread *t_a = list_entry(a, struct thread, elem);
//   const struct thread *t_b = list_entry(b, struct thread, elem);

//   return t_a->priority > t_b->priority;  
// }

// #define MAX_DONATION_DEPTH 8

void donate_priority(void) {
  // struct thread *cur = thread_current();
  // int depth = 0;

  // struct lock *lock = cur->wait_on_lock;

  // // Traverse donation chain up to a limit
  // while (lock && depth < MAX_DONATION_DEPTH) {
  //   struct thread *holder = lock->holder;

  //   if (!holder) break;

  //   if (holder->priority < cur->priority) {
  //     holder->priority = cur->priority;
  //   }

  //   // Move up to next level (nested donation)
  //   lock = holder->wait_on_lock;
  //   depth++;
  // }
  int depth;
  struct thread *cur = thread_current ();

  for (depth = 0; depth < 8; depth++){
    if (!cur->wait_on_lock) break;
      struct thread *holder = cur->wait_on_lock->holder;
      holder->priority = cur->priority;
      cur = holder;
  }
}


void remove_with_lock(struct lock *lock) {
  // struct thread *cur = thread_current();
  // struct list_elem *e = list_begin(&cur->donations);

  // while (e != list_end(&cur->donations)) {
  //   struct thread *t = list_entry(e, struct thread, donation_elem);
  //   struct list_elem *next = list_next(e);  // Store next before possibly removing

  //   if (t->wait_on_lock == lock) {
  //     list_remove(e);  // Remove donated thread from my donations
  //   }

  //   e = next;
  // }
  struct list_elem *e;
  struct thread *cur = thread_current ();

  for (e = list_begin (&cur->donations); e != list_end (&cur->donations); e = list_next (e)){
    struct thread *t = list_entry (e, struct thread, donation_elem);
    if (t->wait_on_lock == lock)
      list_remove (&t->donation_elem);
  }
}


// bool wakeup_time_cmp(const struct list_elem *a, const struct list_elem *b, void *aux UNUSED) {
//   struct thread *ta = list_entry(a, struct thread, elem);
//   struct thread *tb = list_entry(b, struct thread, elem);
//   return ta->wake_up_time < tb->wake_up_time;
// }

bool //이 아래 함수 새롭게 추가.
thread_compare_priority (const struct list_elem *l, const struct list_elem *s, void *aux UNUSED)
{
    return list_entry (l, struct thread, elem)->priority
         > list_entry (s, struct thread, elem)->priority;
}

void //이것도 새로 추가
thread_test_preemption (void)
{
    if (!list_empty (&ready_list) && 
    thread_current ()->priority < 
    list_entry (list_front (&ready_list), struct thread, elem)->priority)
        thread_yield ();
}

bool
thread_compare_donate_priority (const struct list_elem *l, 
				const struct list_elem *s, void *aux UNUSED)
{
	return list_entry (l, struct thread, donation_elem)->priority
		 > list_entry (s, struct thread, donation_elem)->priority;
}

//과제 1-3
void
mlfqs_calculate_priority (struct thread *t) //함수이름은 바꾸는게 좋을듯
{
  if (t == idle_thread) 
    return ;
  t->priority = fp_to_int (add_mixed (div_mixed (t->recent_cpu, -4), PRI_MAX - t->nice * 2));

}

void
mlfqs_calculate_recent_cpu (struct thread *t)
{
  if (t == idle_thread)
    return ;
  t->recent_cpu = add_mixed (mult_fp (div_fp (mult_mixed (load_avg, 2), add_mixed (mult_mixed (load_avg, 2), 1)), t->recent_cpu), t->nice);
}

void 
mlfqs_calculate_load_avg (void) 
{
  int ready_threads;
  
  if (thread_current () == idle_thread)
    ready_threads = list_size (&ready_list);
  else
    ready_threads = list_size (&ready_list) + 1;

  load_avg = add_fp (mult_fp (div_fp (int_to_fp (59), int_to_fp (60)), load_avg), 
                     mult_mixed (div_fp (int_to_fp (1), int_to_fp (60)), ready_threads));
}

//각 값들이 변하는 시점에 수행할 함수들
//맨 위부터 차례대로 현재 스레드의 recent_cpu 의 값을 1 증가시키는 함수, 
//모든 스레드의 recent_cpu 를 재계산 하는 함수, 
//모든 스레드의 priority 를 재계산 하는 함수
void
mlfqs_increment_recent_cpu (void)
{
  if (thread_current () != idle_thread)
    thread_current ()->recent_cpu = add_mixed (thread_current ()->recent_cpu, 1);
}

void
mlfqs_recalculate_recent_cpu (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calculate_recent_cpu (t);
  }
}

void
mlfqs_recalculate_priority (void)
{
  struct list_elem *e;

  for (e = list_begin (&all_list); e != list_end (&all_list); e = list_next (e)) {
    struct thread *t = list_entry (e, struct thread, allelem);
    mlfqs_calculate_priority (t);
  }
}