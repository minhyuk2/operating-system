#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

//프로세스들을 관리하기 위한 프로세스 테이블이다.
struct {
  struct spinlock lock;//lock은 다중 프로세스 시스템에서 프로세스 테이블에 대한 동시접근을 제어하기 위한 잠금 메커니즘이다.
  struct proc proc[NPROC]; //NPROC은 64로 최대로 가능한 프로세스 개수는 64개이다.
  struct proc *mlfq[NUM_QUEUES];  // 4개의 큐 레벨을 위한 배열
} ptable;

static struct proc *initproc;


//레벨을 입력하여 mlfq에 추가해주는 함수
//새로 들어온 큐가 앞에 위치할 수 있게끔 설정
void add_proc_to_mlfq(struct proc *p, int q_level) {
  p->q_level = q_level;
  p->next = 0;

  // 리스트가 비어있는 경우
  if (ptable.mlfq[q_level] == 0) {
    ptable.mlfq[q_level] = p;
    return;
  }

  // 현재 큐의 헤더를 가져옴
  struct proc *curr = ptable.mlfq[q_level];
  struct proc *prev = 0;

  // `io_wait_time` 기준으로 위치를 찾음
  while (curr != 0 && (curr->io_wait_time > p->io_wait_time || 
                       (curr->io_wait_time == p->io_wait_time))) {
    prev = curr;
    curr = curr->next;
  }

  // 맨 앞에 삽입하는 경우 (헤더 변경)
  if (prev == 0) {
    p->next = ptable.mlfq[q_level];
    ptable.mlfq[q_level] = p;
  } else {
    // 중간 또는 끝에 삽입하는 경우
    prev->next = p;
    p->next = curr;
  }
}

//mlfq에서 제거해주는 형태다.
void remove_proc_from_mlfq(struct proc *p) {
  int q_level = p->q_level;
  struct proc *curr = ptable.mlfq[q_level];
  struct proc *prev = 0;

  while (curr != 0) {
    if (curr == p) {
      if (prev == 0) {
        // 삭제할 프로세스가 헤더인 경우
        ptable.mlfq[q_level] = curr->next;
      } else {
        // 중간 또는 마지막 프로세스인 경우
        prev->next = curr->next;
      }
      return;
    }
    prev = curr;
    curr = curr->next;
  }
}


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


//현재 실행중인 cpu를 반환하는 함수다.
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
  //현재 cpu의 인터럽트를 비활성화하고 동시에 이전상태를 저장하여 나중에 복원할 수 있게 한다.
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//새로운 프로세스를 생성하는 함수다.
//allocproc과 userinit의 차이는 userinit은 첫번째 사용자 프로세스를 초기화하는 함수로서 부팅 시에 init프로세스가 생성되는 것을 만든다.
//allocproc은 새로운 프로세스를 할당하는 함수로서 프로세스 테이블에서 unused 상태를 찾고 이를 초기화하여 사용한다.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);
  
  //이미 만들어진 슬롯에 UNUSED로 사용되지 않고 있는 곳이 있는지 판단하여 넣어주는 것이다.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock); //다시 ptable에 대한 락을 해제한다.
  return 0; //0이 반환되면 실패한 것이고 더 이상 프로세스에 넣을 공간이없다는 뜻이다.

found:
  p->state = EMBRYO; //EMBRYO는 프로세스가 생성 중인 상태를 의미한다.
  p->pid = nextpid++; //pid를 설정해주는 것이다.

  // 추가: MLFQ 관련 변수 초기화
  p->q_level = 0;          // 최상위 큐에서 시작
  p->cpu_burst = 0;        // CPU 사용량 초기화
  p->cpu_wait = 0;         // 대기 시간 초기화
  p->io_wait_time = 0;     // I/O 대기 시간 초기화
  p->end_time = -1;         // cpu 총 사용할당량 초기화
  p->stack_cpu_burst = 0;


  release(&ptable.lock);

  //커널 스택 -> 커널 모드에서 사용할 수 있는 함수 호출 스택을 의미한다.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;


  //커널 모드로 들어갈 때 cpu 레지스터를 저장하는 구조체다.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  sp -= 4;
  *(uint*)sp = (uint)trapret; //나중에 커널에서 돌아올 때 해당 함수로 나올 수 있게 설정해준다.

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret; //새로운 프로세스는 커널에서 시작하게 만들어준다.

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
//첫번째 사용자 모드에서 실행되는 프로세스를 설정하는 것을 의미한다.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  // //0번에 있던 것을 제거해주고,3에 넣어줘야함.
  remove_proc_from_mlfq(p); //원래 있던 곳에세 제거  
  p->q_level = 3; //init프로세스는 q_level을 3으로 고정시켜야하기 때문이다.
  add_proc_to_mlfq(p,p->q_level); //3번째 레벨의 큐에 삽입 시켜준다.
 

  initproc = p;
  // 커널 가상 메모리 할당 및 설정
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


  acquire(&ptable.lock);

  p->state = RUNNABLE;   //프로세스의 상태를 runnable로 설정하여 실행가능 상태로 변경

  release(&ptable.lock);
}

//가상 메모리의 크기를 확장하거나 축소하는 함수다.
//주로 힙 영역을 확장하거나 축소하는 sbrk() 시스템 콜을 통해서 호출된다.
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

//부모 프로세스를 복사하여 자식 프로세스를 만드는 함수로 두 프로세스가 동시에 실행될 수 있게 만들며, 스케줄러에 의해 실행될 준비를 하게 만드는 것이다.
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

  //pgdir은 페이지 테이블 디렉토리를 의미한다. 아래의 함수는 부모 프로세스의 페이지 디렉토리를 복사하여 자식 프로세스에게 할당하는 것을 의미하는 것이다.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    //실패한 경우는 커널 스택을 해제하고, UNUSED로 상태를 바꾼 다음에 -1을 리런시킨다.
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

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  //부모 프로세스의 이름을 자식프로세스에게 복사한다. 이름은 디버깅이나 추적 목저으로 사용된다.
  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  //pid를 설정한다.
  pid = np->pid;
  // // 프로세스의 pid로 판단하여 해당하는 이름을 가진 경우에는 q_level고정
  //mlfq에 넣어주는 것이다.
  add_proc_to_mlfq(np,0);

  if(np->pid == 2){
    //쉘 프로세스는 q_level 3으로 이동시키면 된다.
    remove_proc_from_mlfq(np);
    np->q_level = 3;
    add_proc_to_mlfq(np,np->q_level);
  }


  #ifdef DEBUG
    if(np->pid > 3){
      cprintf("PID: %d created\n",np->pid);
    }
  #endif

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
      //파일 디스크립터를 0으로 초기화하여 열려있는 파일들을 닫아준다.
      curproc->ofile[fd] = 0;
    }
  }

  //파일 시스템의 작업의 트랜젝션을 시작하고 끝내는 함수다.
  //트렌젝션을 사용하면 완전히 성공하거나 실패하면 롤백하는 식으로 작동한다.
  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

 //부모 프로세스가 wait으로 자식 프로세스를 기다릴 수 있기 때문에 부모 프로세스를 꺠운다.
  wakeup1(curproc->parent);

  //현재 프로세스가 종료가 되면 현재 프로세스가 갖고 있던 자식 프로세스는 고아 프로세스가 되기 때문에 해당 프로세스들을 init 프로세스에게 넘긴다.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p; //자식 프로세스를 담기 위한 것
  int havekids, pid;
  struct proc *curproc = myproc();//현재 실행 중인 부모 프로세스
  
  acquire(&ptable.lock);

  //종료된 자식프로세스가 있을 때까지 무한으로 수행되는 것이다.
  for(;;){

    //havekids는 현재 프로세스가 자식 프로세스를 갖고 있는지 판단하는 것이다.
    havekids = 0;

    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        //여기서 큐에 있는 것을  제거해주면 된다.
        remove_proc_from_mlfq(p);
        
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
        p->q_level = 0;
        p->priority=0;
        p->cpu_burst = 0;
        p->cpu_wait = 0;
        p->io_wait_time = 0;
        p->end_time = 0;
        p->next = 0;
        p->stack_cpu_burst=0;
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

//이 스케쥴러 함수를 통해서 스케쥴을 하는 것입니다.
/*
CPU가 실행할 프로세스를 선택하고 해당 프로세스를 실행하는 책임을 가지고 있습니다.
이 함수는 각 CPU별로 호출되며, 영원히 반환되지 않는 무한 루프로 작동합니다.
즉, 각 CPU는 자신에게 할당된 프로세스를 스케줄링하며, 프로세스의 상태를 관리하고 문맥 전환(context switch)을 수행합니다.
*/
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  for(;;){

    //인터럽트 플래그를 설정하여 인터럽트를 허용하며 , 외부 인터럽트를 받으며 이벤트가 발생하면 처리할 수 있게 됩니다.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    //네 개의 큐를 순환하면서 스케줄링을 할 수 있도록 설정

    for(int i = 0 ; i< NUM_QUEUES; i ++){
        //큐룰 초기화하여 생성.
        struct proc* current = 0;
        struct proc* biggest = 0;
        //돌면서 우선순위가 제일 높은 것을 선택.
   
        current = ptable.mlfq[i];
        //헤더가 null이면 바로 실행 종료
        if(current == 0){
          continue;
        }
    
        int biggestIo = -1;
        int  currWait = 99999999;
        // 첫 번째 RUNNABLE 상태의 프로세스를 찾기
          while (current != 0) {
              if (current->state == RUNNABLE) {
                  biggest = current;
                  biggestIo = current->io_wait_time;
                  currWait = current->cpu_wait;
                  break;
              }
              current = current->next;
          }
        //실행시킬 프로세스를 선택하는 과정
        while(current != 0 ){
            if(current->state != RUNNABLE){
              current = current->next;
              continue;
            }
            //가장 우선순위가 높은 프세스를 찾기 위해서 우선적으로 io_wait_time을 가장 큰 것을 선택하고
            //io_wait_time이 같은 경우에는 cpu_wait 을 비교하여 먼저 들어온 프로세스는 cpu_wait이 클 것이기에 cpu_wait이 작은 프로세스를 선택하고
            //io_wait_time, cpu_wait도 같은 경우에는 큐에서 가장 앞에 집어넣어줌으로써 해결을 하고 pid가 더 큰 경우가 나중에 들어온 것이기에 pid가 더 큰 것을 선택한다.
            if(current->io_wait_time > biggestIo || 
               (current->io_wait_time == biggestIo && current->cpu_wait < currWait)||
               (current->io_wait_time == biggestIo && current->cpu_wait == currWait && current->pid > biggest->pid)) {
                   biggestIo = current->io_wait_time;
                   biggest = current;
                   currWait = current->cpu_wait;
            }
            current = current->next;
        }
        
        if(biggest == 0 ){
          continue;
        }
        if(biggest->state != RUNNABLE){
          continue;
        }

        c->proc = biggest;
        switchuvm(biggest);//swtch를 통해 현재 프로세스의 문맥을 저장하고, 선택된 프로세스 p의 문맥을 복원한다.
        biggest->state = RUNNING;
        //만약 3인 경우에는 그냥 실행시켜주면 됨
        swtch(&(c->scheduler), biggest->context);
        switchkvm();
        if(biggest->q_level !=3){
          //3이 아니라면 내려 주어야 한다.
          remove_proc_from_mlfq(biggest);
          biggest->q_level++;
          biggest->cpu_burst = 0;
          biggest->cpu_wait = 0;
          biggest->io_wait_time = 0;
          //하위 큐에 삽입시켜주어야 한다.
          add_proc_to_mlfq(biggest,biggest->q_level);
        }else{
          //3번큐 cpu_wait는 없애야 한다.
          biggest->cpu_wait = 0;
          biggest->cpu_burst = 0;
        }
        c->proc = 0;
  
        if(ptable.mlfq[0]!=0||ptable.mlfq[1]!=0||ptable.mlfq[2]!=0){
            break;
        }
    }
    release(&ptable.lock);

  }
}


//현재 실행 중인 프로세스를 스케줄러로 문맥 전환하는 역할을 합니다.
// 즉, 현재 프로세스가 CPU에서 더 이상 실행되지 않도록 하고, 스케줄러가 다른 프로세스를 선택할 수 있도록 제어를 넘기는 함수입니다.
//sched는 yield 함수에서 현재 프로세스가 CPU를 스케쥴러에게 양보할 때 호출되고 , sleep함수에서 프로세스가 블록 상태로 들어갈 때 호출된다
void
sched(void)
{
  int intena;
  struct proc *p = myproc();//현새 실행 중인 프로세스를 가리킨다.


  if(!holding(&ptable.lock)) // 프로세스 테이블에 대한 락이 되어있는지 확인
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1) //인터럽트가 비활성화 된 상태에서만 호출해야함
    panic("sched locks");
  if(p->state == RUNNING) //현재 프로세스의 상태가 RUNNING이 아니어야 한다.
    panic("sched running");
  if(readeflags()&FL_IF) //인터럽트 플래그를 확인해서 인터럽트가 활성화된 상태인지 확인한다.
    panic("sched interruptible");
  intena = mycpu()->intena; //현재 인터럽트의 활성화 여부를 저장

  swtch(&p->context, mycpu()->scheduler); //스케줄러가 다음 프로세스를 선택할 수 있도록 cpu제어권을 넘긴다.
    //스케쥴러가 이전에 중단된 지점을 기억하여 중단된 지점부터 일을 다시 하게끔 만들어주는 것입니다.
  mycpu()->intena = intena; //인터럽트 상태 복원
}

// 현재 CPU를 사용하는 것을 넘기고 다음 프로세스에게 넘기는 함수
// 현재 프로세스를 멈추고 다음 프로세스에게 넘기는 함수다.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE; //현재 프로세스를 실행가능한 프로세스 바꾸고 스케줄링 시킨다.
  sched(); //이를 통해서 현재 프로세스를 멈추고 다음 프로세스를 스케쥴링 되어서 실행시키는 함수다.
  release(&ptable.lock);
}


//xv6 운영체제에서 자식 프로세스가 처음으로 스케줄링될 때 호출되는 함수입니다.
//이 함수는 자식 프로세스가 처음으로 사용자 공간(user space)에서 실행될 수 있도록 준비하는 역할을 합니다.
//allocproc()에서 사용된다.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);  //락을 해제한다.

  if (first) { //첫 번째 프로세스가 스케줄링될 때만 이 블록이 실행되게끔 만든다.
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);  //루트 파일 시스템을 초기화하는 함수다.
    initlog(ROOTDEV); //파일 시스템의 로그를 초기화하는 함수다.
  }

  // Return to "caller", actually trapret (see allocproc).
}

// 프로세스를 대기 상태로 만들고, 지정된 채널에서 다른 이벤트가 발생할 때까지 프로세스를 잠들게 하는 함수다.
// 락을 원자적으로 해제하고, 프로세스를 잠재운 후 다시 락을 획득하게 하는 것이 핵심이다.
//sleep은 chan을 매개변수로 받고 chan은 주로 메모리 주소를 값으로 갖는다.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc(); //현재 프로세스 포인터
  
  if(p == 0)  //프로세스가 종료하지 않는 경우에는 panic
    panic("sleep");

  if(lk == 0)  //프로세스가 잠들기 전에 가지고 있던 락
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
  p->chan = chan; //현재 프로세스가 대기할 채널을 설정/ 채널은 프로세스가 깨어날 때 어떤 이벤트나 신호가 발생했는지 구분하는 용도로 사용되어짐
  p->state = SLEEPING; //프로세스의 상태를 sleeping으로 설정하여 프로세스가 대기 상태임을 표시함. 스케줄러가 이 프로세스를 실행하지 않도록 하기 위함이다.

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

// 특정 chan에 잠들어 있는 프로세스를 깨우는 역할을 한다.
// 주어진 채널에서 대기 중인 모든 프로세스를 찾아서 RUNNABLE로 바꾼다 -> 즉, 그냥 진짜 프로세스를 깨우는 함수다.
// 락이 걸린 것을 전제조건으로 수행하는 함수다.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// 위의 wakeup1함수를 호출하기 위한 전제조건인 락을 설정하는 함수다.
// 해당 함수를 통해서 락을 설정하고 wakeup1을 호출하여 상태를 변경한다.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}


// 특정 프로세스의 pid를 이용해서 프로세스를 종료시키는 함수다.
// 프로세스가 즉시 종료되지 않으며, 사용자 모드로 돌아갈 때 종료 절차가 시작된다.
// 즉, 커널에서 사용자 모드로 돌아가는 시점에서 종료 상태를 확인하고, 종료 처리가 이루어지는 것이다.
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);  //프로세스 테이블에 대한 락을 획득함.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    //pid와 일치하는 프로세스를 찾음
    if(p->pid == pid){
      p->killed = 1; //killed 플래그를 1로 설정함 프로세스는 주기적으로 자신의 killed 상태를 확인하며 이 값이 1이면 종료 절차를 진행하게 됨.
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) //프로세스가 잠들어있는 상태면 RUNNABLE로 변경하여 프로세스가 깨어나도록 함.
        p->state = RUNNABLE;
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
// 디버깅용으로 프로세스의 상태를 출력하는 함수다.
// 콘솔에 현재 실행 중인 모든 프로세스의 상태를 출력하여 정보를 제공한다.
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
