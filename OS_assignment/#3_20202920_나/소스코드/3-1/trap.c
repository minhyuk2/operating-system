#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"
#include "date.h"

//ptable 가져오기
extern struct {
    struct spinlock lock;
    struct proc proc[NPROC];
} ptable;


// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256];
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

void
tvinit(void)
{
  int i;

  for(i = 0; i < 256; i++)
    SETGATE(idt[i], 0, SEG_KCODE<<3, vectors[i], 0);
  SETGATE(idt[T_SYSCALL], 1, SEG_KCODE<<3, vectors[T_SYSCALL], DPL_USER);

  initlock(&tickslock, "time");
}

void
idtinit(void)
{
  lidt(idt, sizeof(idt));
}

//PAGEBREAK: 41
void
trap(struct trapframe *tf)
{
  if(tf->trapno == T_PGFLT){
        uint va = rcr2(); // 잘못된 부분의 가상 주소를 가져옴
        struct proc *curproc = myproc();

        if(curproc == 0){
            // 프로세스가 없는 경우에 대한 처리
            panic("No process");
        }

        // 유효한 주소인지 확인
        //현재 프로세스의 크기보다 큰 것은 아닌지 커널베이스를 넘어가는 것은 아닌지 확인
        if(va >= curproc->sz || va >= KERNBASE){
            cprintf("Memory is out of bound\n");
            curproc->killed = 1;
            return;
        }
        
        // 물리 메모리 할당 및 매핑
        char *phyMem = kalloc();
        //물리 메모리가 부족하여 할당되지 않은 경우에 대한 예외처리
        if(phyMem == 0){
            cprintf("Out of physical memory.\n");
            curproc->killed = 1;
            return;
        }
        //물리메모리 영역을 0으로 초기화
        memset(phyMem, 0, PGSIZE);
        //주어진 가상 주소를 페이지 경계로 정렬
        va = PGROUNDDOWN(va);
        //가상주소와 새로운 물리주소를 매핑시켜줍니다.
        if(mappages(curproc->pgdir, (char*)va, PGSIZE, V2P(phyMem), PTE_W|PTE_U|PTE_P) < 0){
            kfree(phyMem);
            cprintf("Mappages ERROR\n");
            curproc->killed = 1;
            return;
        }
        // 페이지 폴트 처리 완료
        return;
    }

  if(tf->trapno == T_SYSCALL){
    if(myproc()->killed)
      exit();
    myproc()->tf = tf;
    syscall();
    if(myproc()->killed)
      exit();
    return;
  }

  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER:
    if(cpuid() == 0){
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks);
      release(&tickslock);
    }
    lapiceoi();
 // 지연 메모리 해제 처리 추가
    struct proc *curproc = myproc();
    if(curproc){
        acquire(&ptable.lock);
        if(curproc->pending_free_ticks > 0){
            curproc->pending_free_ticks--;
            if(curproc->pending_free_ticks == 0){
                // 메모리 해제 수행
                uint oldsz = curproc->sz;
                uint newsz = curproc->pending_free_addr;

                if(deallocuvm(curproc->pgdir, oldsz, newsz) == 0){
                    cprintf("Memory Deallocation fault\n");
                    curproc->killed = 1;
                } else {
                    curproc->sz = newsz;

                    // 현재 시간 가져오기
                    struct rtcdate curDateTime;
                    cmostime(&curDateTime);
                    //메모리 해제 시간에 대한 정보 출력
                    cprintf("Memory deallocation execute: %d-%d-%d %d:%d:%d\n",
                            curDateTime.year, curDateTime.month, curDateTime.day,
                            curDateTime.hour, curDateTime.minute, curDateTime.second);

                    // memstat() 호출
                    procMemstat();

                    // 메모리 해제 후 프로세스 종료
                    curproc->killed = 1;
                }

                // 지연 해제 정보 초기화
                curproc->pending_free_pages = 0;
                curproc->pending_free_addr = 0;
                curproc->allowDelayTicks = 0;       
                memset(&curproc->ssusbrk_call_time, 0, sizeof(curproc->ssusbrk_call_time));
            }
        }
        release(&ptable.lock);
    }
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1:
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr();
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr();
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // Force process exit if it has been killed and is in user space.
  // (If it is still executing in the kernel, let it keep running
  // until it gets to the regular system call return.)
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER)
    yield();

  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
