#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;
#ifdef MLFQ
struct proc* q1[64];
struct proc* q2[64];
struct proc* q3[64];
struct proc* q4[64];
struct proc* q5[64];
int c1=0;
int c2=0;
int c3=0;
int c4=0;
int c5=0;
int clicks_per_queue[5]={1, 2, 4, 8, 16};
#endif

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  #ifdef MLFQ 
  p->priority = 1;
  c1++;
  q1[c1-1] = p;
  #endif 
  #ifdef PBS
  p->priority = 60;
  #endif
  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  #ifdef MLFQ
  p->priority = 1;
  c1++;
  q1[c1-1] = p;
  #endif
  #ifdef PBS
  p->priority = 60;
  #endif
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->ctime = ticks; // TODO Might need to protect the read of ticks with a lock
  p->etime = 0;
  p->rtime = 0;
  p->iotime=0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;
  np->priority = curproc->priority;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  curproc->etime = ticks; // TODO Might need to protect the read of ticks with a lock
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int
waitx(int *wtime, int *rtime)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        *wtime= p->etime - p->ctime - p->rtime - p->iotime;
        *rtime=p->rtime;
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

int getpinfo(struct procstat* curproc){
  struct proc *p = myproc();
  curproc->pid = p->pid;
  curproc->current_queue = p->priority;
  curproc->num_run = p->num_run;
  for(int i = 0; i < 5; i++)
    curproc->ticks[i]=p->cq[i];
  curproc->runtime = p->rtime;
  return 25;
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    #ifdef DEFAULT
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    #else
    #ifdef FCFS
    struct proc *minP = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state == RUNNABLE){
        if (minP!=0  && p->ctime < minP->ctime)
          minP = p;
        else if(minP == 0)
          minP = p;
      }
    }
    if (minP!=0){
      p = minP;//the process with the smallest creation time
      c->proc = p;
      cprintf("state = %d ctime = %d pid = %d \t",p->state, p->ctime, p->pid);
      switchuvm(p);
      p->state = RUNNING;
      swtch(&c->scheduler, p->context);
      switchkvm();
      cprintf("state after ending = %d\n", p->state);
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    
    #else
    #ifdef PBS
    struct proc *p1;
    struct proc *highP =  0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;
      highP = p;
      for(p1=ptable.proc; p1<&ptable.proc[NPROC];p1++){
          if(p1->state != RUNNABLE)
            continue;
          if(highP->priority > p1->priority) // larger value, lower priority
            highP = p1;
      }
      p = highP;
      //cprintf("state = %d priority = %d pid = %d \t",p->state, p->priority, p->pid);
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&(c->scheduler), p->context);
      //cprintf("state after ending = %d\n", p->state);
      switchkvm();
      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    #else
    #ifdef MLFQ
padhi:
    for(int i = 0; i < c2; i++){
      if(q2[i]->state==RUNNABLE && (ticks - (q2[i]->last_time))>100){
  //        cprintf("%s-%d getting promoted\n",q2[i]->name, q2[i]->pid);
          p = q2[i];
          p->priority=1;
          q2[i]=0;
          for(int j = i; j<c2-1; j++)
              q2[j]=q2[j+1];
          
          c2--;
          c1++;
          i--;
          q1[c1-1]=p;
          p->last_time=ticks;
      }
    }
    for(int i = 0; i < c3; i++){
      if(q3[i]->state==RUNNABLE && (ticks - (q3[i]->last_time))>100){
  //        cprintf("%s-%d getting promoted\n",q3[i]->name, q3[i]->pid);
          p = q3[i];
          p->priority=2;
          q3[i]=0;
          for(int j = i; j<c3-1; j++)
              q3[j]=q3[j+1];
          c3--;
          c2++;
          i--;
          q2[c2-1]=p;
          p->last_time=ticks;
      }
    }
    for(int i = 0; i < c4; i++){
      if(q4[i]->state==RUNNABLE && (ticks - (q4[i]->last_time))>100){
 //         cprintf("%s-%d getting promoted\n",q4[i]->name, q4[i]->pid);
          p = q4[i];
          p->priority=3;
          q4[i]=0;
          for(int j = i; j<c4-1; j++)
              q4[j]=q4[j+1];
          c4--;
          c3++;
          i--;
          q3[c3-1]=p;
          p->last_time=ticks;
      }
    }
    for(int i = 0; i < c5; i++){
      if(q5[i]->state==RUNNABLE && (ticks - (q5[i]->last_time))>100){
  //        cprintf("%s-%d getting promoted\n",q5[i]->name, q5[i]->pid);
          p = q5[i];
          p->priority=4;
          q5[i]=0;
          for(int j = i; j<c5-1; j++)
              q5[j]=q5[j+1];
          c5--;
          c4++;
          i--;
          q4[c4-1]=p;
          p->last_time=ticks;
      }
    }
    if(c1){
      //cprintf("xd\n");
      for(int i = 0; i < c1; i++){
        if(q1[i]->state != RUNNABLE)
          continue;
        //if(q1[i]){
          p = q1[i];
          //cprintf("from c0. id =%d name =%s\n",p->pid,p->name);
				  cprintf("executing this at time  = %d. id=%d , name =%s , state =%d, priority=%d\n",ticks,p->pid, p->name,p->state,p->priority);
				  c->proc=p;
          switchuvm(p);
//          cprintf("tatte daba dunga\n");
          p->state = RUNNING;
          p->last_time=ticks;
				  swtch(&c->scheduler, p->context);
//          cprintf("I will switch your mother\n");
				  switchkvm();
//          cprintf("proc exec\n");
          //if(p->clicks==clicks_per_queue[0]){
//            cprintf("yeeting %s\n", p->name);
//            release(&ptable.lock);
//            yield();
//            acquire(&ptable.lock);
          /*  c2++;
            p->priority++;
            q2[c2-1]=p;
            q1[i]=0;
            for(int j = i; j < c1; j++){
             q1[j] = q1[j+1];
            }
            c1--;*/
          //}
          c->proc = 0;
          goto padhi;
    //    }
      }
    }

    if(c2){
      for(int i = 0; i < c2; i++){
        if(q2[i]->state != RUNNABLE)
          continue;
       // if(q2[i]){
          p = q2[i];
          //cprintf("from c0. id =%d name =%s\n",p->pid,p->name);
				  cprintf("executing this  . id=%d , name =%s , state =%d, priority=%d\n",p->pid, p->name,p->state, p->priority);
				  c->proc = p;
          switchuvm(p);
				  p->state = RUNNING;
          p->last_time=ticks;
//          cprintf("ttttttttttttttttt\n");
				  swtch(&c->scheduler, p->context);
//          cprintf("I will switch you\n");
				  switchkvm();
//          cprintf("proc exec\n");
   //       if(p==clicks_per_queue[1]){
//            release(&ptable.lock);
//            yield();
//            acquire(&ptable.lock);
          /*  c3++;
            p->priority++;
            q2[c3-1]=p;
            q2[i]=0;
            for(int j = i; j < c2; j++){
              q2[j] = q2[j+1];
            } 
            c2--;*/
      //    }
          c->proc = 0;
          goto padhi;
        //}
      }
    }

    if(c3){
      for(int i = 0; i < c3; i++){
        if(q3[i]->state != RUNNABLE)
          continue;
        //if(q3[i]){
          p = q3[i];
          //cprintf("from c0. id =%d name =%s\n",p->pid,p->name);
				  cprintf("executing this  . id=%d , name =%s , state =%d, priority=%d\n",p->pid, p->name,p->state,p->priority);
				  c->proc = p;
          switchuvm(p);
//          cprintf("ttttttttttttttttttttt\n");
				  p->state = RUNNING;
          p->last_time=ticks;
				  swtch(&c->scheduler, p->context);
//          cprintf("I will switch you\n");
				  switchkvm();
//          cprintf("proc exec\n");
        //  if(p->clicks==clicks_per_queue[2]){
//            release(&ptable.lock);
//            yield();
//            acquire(&ptable.lock);
           /*  c4++;
            p->priority++;
            q4[c4-1]=p;
            q3[i]=0;
            for(int j = i; j < c3; j++){
              q3[j] = q3[j+1];
            }
            c3--;*/
  //        }
          c->proc = 0;
          goto padhi;
        //}
      }
    }

    if(c4){
      for(int i = 0; i < c4; i++){
        if(q4[i]->state != RUNNABLE)
          continue;
        //if(q4[i]){
          p = q4[i];
          //cprintf("from c0. id =%d name =%s\n",p->pid,p->name);
				  cprintf("executing this  . id=%d , name =%s , state =%d, priority=%d\n",p->pid, p->name,p->state, p->priority);
				  c->proc = p;
          switchuvm(p);
				  p->state = RUNNING;
          p->last_time=ticks;
				  swtch(&c->scheduler, p->context);
				  switchkvm();
    //      if(p->clicks==clicks_per_queue[3]){
//            release(&ptable.lock);
//            yield();
//            acquire(&ptable.lock);
          /*    c5++;
            p->priority++;
            q5[c5-1]=p;
            q4[i]=0;
            for(int j = i; j < c4; j++){
              q4[j] = q4[j+1];
            }
            c4--;*/
      //    }
          c->proc = 0;
          goto padhi;
        //}
      }
    }

    if(c5){
      for(int i = 0; i < c5; i++){
        if(q5[i]->state != RUNNABLE)
          continue;
        //if(q5[i]){
          p = q5[i];
          //cprintf("from c0. id =%d name =%s\n",p->pid,p->name);
				  cprintf("executing this  . id=%d , name =%s , state =%d, priority=%d\n",p->pid, p->name,p->state,p->priority);
				  c->proc = p;
          switchuvm(p);
				  p->state = RUNNING;
          p->last_time=ticks;
				  swtch(&c->scheduler, p->context);
				  switchkvm();
        //  if(p->clicks==clicks_per_queue[4]){
//            release(&ptable.lock);
//            yield();
//            acquire(&ptable.lock);
           /*   q5[i]=0;
            for(int j = i; j < c5; j++){
              q5[j] = q5[j+1];
            }
            c5--;
            q5[c5-1]=p;*/
          
         // c->proc = 0;
          goto padhi;
        //}
      }
    }

    #endif
    #endif
    #endif
    #endif

    release(&ptable.lock);
  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  myproc()->num_run++;
#ifdef MLFQ
//  cprintf("about to yeet %s\n",myproc()->name);
  if(myproc()->priority==1){
  if(myproc()->cq[0]>=clicks_per_queue[myproc()->priority-1]){
    c2++;
    myproc()->priority++;
    q2[c2-1]=myproc();
    int i=0;
    for(int a = 0; a < c1; a++){
      if(q1[a]->pid==myproc()->pid)
          i = a;
    }
    q1[i]=0;
    for(int j = i; j < c1-1; j++){
     q1[j] = q1[j+1];
    }
    c1--;
  }
  else{
    myproc()->cq[myproc()->priority-1]++;
/*    int i = 0;
    for(int a = 0; a < c1-1; a++){
      if(q1[a]->pid==myproc()->pid)
          i=a;
    }
    q1[i]=0;
    for(int j = i; j<c1-1; j++){
      q1[j]=q1[j-1];
    }
    q1[c1-1]=myproc();*/
  }
  }

  if(myproc()->priority==2){
  if(myproc()->cq[1]>=clicks_per_queue[myproc()->priority-1]){
    c3++;
    myproc()->priority++;
    q3[c3-1]=myproc();
    int i=0;
    for(int a = 0; a < c2; a++){
      if(q2[a]->pid==myproc()->pid)
          i = a;
    }
    q2[i]=0;
    for(int j = i; j < c2-1; j++){
     q2[j] = q2[j+1];
    }
    c2--;
  }
  else{
    myproc()->cq[myproc()->priority-1]++;
/*    int i = 0;
    for(int a = 0; a < c2-1; a++){
      if(q2[a]->pid==myproc()->pid)
          i=a;
    }
    q2[i]=0;
    for(int j = i; j<c2-1; j++){
      q2[j]=q2[j-1];
    }
    q2[c2-1]=myproc();*/
  }
  }

  if(myproc()->priority==3){
  if(myproc()->cq[2]>=clicks_per_queue[myproc()->priority-1]){
    c4++;
    myproc()->priority++;
    q4[c4-1]=myproc();
    int i=0;
    for(int a = 0; a < c3; a++){
      if(q3[a]->pid==myproc()->pid)
          i = a;
    }
    q3[i]=0;
    for(int j = i; j < c3-1; j++){
     q3[j] = q3[j+1];
    }
    c3--;
  }
  else{
    myproc()->cq[myproc()->priority-1]++;
/*    int i = 0;
    for(int a = 0; a < c3-1; a++){
      if(q3[a]->pid==myproc()->pid)
          i=a;
    }
    q3[i]=0;
    for(int j = i; j<c3-1; j++){
      q3[j]=q3[j-1];
    }
    q3[c3-1]=myproc();*/
  }
  }

  if(myproc()->priority==4){
  if(myproc()->cq[3]>=clicks_per_queue[myproc()->priority-1]){
    c5++;
    myproc()->priority++;
    q5[c5-1]=myproc();
    int i=0;
    for(int a = 0; a < c4; a++){
      if(q4[a]->pid==myproc()->pid)
          i = a;
    }
    q4[i]=0;
    for(int j = i; j < c4-1; j++){
     q4[j] = q4[j+1];
    }
    c4--;
  }
  else{
    myproc()->cq[myproc()->priority-1]++;
/*    int i = 0;
    for(int a = 0; a < c4-1; a++){
      if(q4[a]->pid==myproc()->pid)
          i=a;
    }
    q4[i]=0;
    for(int j = i; j<c4-1; j++){
      q4[j]=q4[j-1];
    }
    q4[c4-1]=myproc();*/
  }
  }

  else if(myproc()->priority == 5){
    if(myproc()->cq[4]<=clicks_per_queue[myproc()->priority-1])
        myproc()->cq[myproc()->priority-1]++;
    else{
    int i=0;
    for(int a = 0; a < c5; a++){
      if(q5[a]->pid == myproc()->pid)
        i = a;
    }
    q5[i]=0;
    for(int j = i; j < c5-1; j++){
      q5[j] = q5[j+1];
    }
    q5[c5-1] = myproc();
    }
  }
#endif
  sched();
  release(&ptable.lock);
}


// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
      #ifdef MLFQ
      if(p->priority==1){
        c1++;
        q1[c1-1]=p;
      }
      if(p->priority==2){
        c2++;
        q2[c2-1]=p;
      }
      if(p->priority==3){
        c3++;
        q3[c3-1]=p;
      }
      if(p->priority==4){
        c4++;
        q4[c4-1]=p;
      }
      if(p->priority==5){
        c5++;
        q5[c5-1]=p;
      }
      #endif
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
cps(){
    struct proc *p;
    sti();
    acquire(&ptable.lock);
    cprintf("name \t pid \t state \t \t priority \n");
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state == RUNNABLE)
            cprintf("%s \t %d \t RUNNABLE \t %d\n", p->name, p->pid, p->priority);
        else if(p->state == RUNNING)
            cprintf("%s \t %d \t RUNNING \t %d\n", p->name, p->pid, p->priority);
        else if(p->state == SLEEPING)
            cprintf("%s \t %d \t SLEEPING \t %d\n", p->name, p->pid, p->priority);
    }
    release(&ptable.lock);
    return 24;
}

// Change priority
int
cpr(int pid, int priority){
    struct proc *p;
    acquire(&ptable.lock);
    for(p=ptable.proc; p<&ptable.proc[NPROC]; p++){
        if(p->pid == pid){
            p->priority = priority;
            break;
        }
    }
    release(&ptable.lock);
    return pid;
}
