#define NUM_QUEUES 4  // 큐 레벨 개수

// Per-CPU state
struct cpu {
  uchar apicid;                // Local APIC ID
  struct context *scheduler;   // swtch() here to enter scheduler
  struct taskstate ts;         // Used by x86 to find stack for interrupt
  struct segdesc gdt[NSEGS];   // x86 global descriptor table
  volatile uint started;       // Has the CPU started?
  int ncli;                    // Depth of pushcli nesting.
  int intena;                  // Were interrupts enabled before pushcli?
  struct proc *proc;           // The process running on this cpu or null
};

extern struct cpu cpus[NCPU];
extern int ncpu;

//PAGEBREAK: 17
// Saved registers for kernel context switches.
// Don't need to save all the segment registers (%cs, etc),
// because they are constant across kernel contexts.
// Don't need to save %eax, %ecx, %edx, because the
// x86 convention is that the caller has saved them.
// Contexts are stored at the bottom of the stack they
// describe; the stack pointer is the address of the context.
// The layout of the context matches the layout of the stack in swtch.S
// at the "Switch stacks" comment. Switch doesn't save eip explicitly,
// but it is on the stack and allocproc() manipulates it.
//cpu가 현재 프로세스의 실행을 중단하고 다른 프로세스를 실행할 때 현재 프로세스의 실행 상태(레지스터 값)을 저장하고 나중에 복원하는데 사용하는 것이다.
struct context {
  uint edi;
  uint esi;
  uint ebx;
  uint ebp;
  uint eip; //현재 실행중인 명령어 주소를 의미한다.
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

// Per-process state
struct proc {
  uint sz;                     // Size of process memory (bytes) 프로세스의 사이즈
  pde_t* pgdir;                // Page table 페이지테이블의 주소
  char *kstack;                // Bottom of kernel stack for this process 커널 스택의 주소
  enum procstate state;        // Process state 현재 프로세스의 상태
  int pid;                     // Process ID 프로세스 ID
  struct proc *parent;         // Parent process 부모 프로세스
  struct trapframe *tf;        // Trap frame for current syscall  트랩프레임을 가리키는 포인터로 시스템 콜이나 인터럽트가 발생하였을 때 CPU의 상태를 저장한다.
  struct context *context;     // swtch() here to run process context구조체를 통해 문맥을 저장
  void *chan;                  // If non-zero, sleeping on chan 채널 주소를 의미
  int killed;                  // If non-zero, have been killed killed상태
  struct file *ofile[NOFILE];  // Open files 열린 파일들
  struct inode *cwd;           // Current directory 현재 디렉토리
  char name[16];               // Process name (debugging) 디버깅을 위한 프로세스의 이름
  int q_level;                 // 현재 프로세스가 속한 큐 레벨 -> 어떤 큐에 있는지 나타내는 것이다. ticks가 갱신될 때마다 ++해주면 되는 것이다.
  int cpu_burst;               // 프로세스의 cpu에서 사용한 시간
  int cpu_wait;                // 프로세스가 큐 내에서 대기한 시간
  int io_wait_time;            // 해당 큐에서 sleeping 상태 시간
  int end_time;                // cpu 총 사용 할당량을 의미
  int priority;
  struct proc *next;           // 다음 프로세스를 가리키는 포인터
  int stack_cpu_burst;
};




extern void add_proc_to_mlfq(struct proc *p, int q_level);
extern void remove_proc_from_mlfq(struct proc *p);


// Process memory is laid out contiguously, low addresses first:
//   text
//   original data and bss
//   fixed-size stack
//   expandable heap
