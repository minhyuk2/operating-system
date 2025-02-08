#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"
#include "traps.h"
#include "spinlock.h"

// Interrupt descriptor table (shared by all CPUs).
struct gatedesc idt[256]; //256개의 인터럽트 게이트를 저장하는 IDT 인터럽트 디시크립터 테이블을 의미한다.
extern uint vectors[];  // in vectors.S: array of 256 entry pointers
struct spinlock tickslock;
uint ticks;

//ptable 가져오기
extern struct {
    struct spinlock lock;
    struct proc proc[NPROC];
    struct proc *mlfq[NUM_QUEUES];  // 4개의 큐 레벨을 위한 배열
} ptable;


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

//trap함수는 운영체제에서 인터럽트, 예외, 시스템 콜 등을 처리하는 핵심함수다.
//CPU가 발생시키는 다양한 이벤트를 처리한다.
//스케줄러와도 밀접하게 연관되어 있으며, 타이머 인터럽트가 발생할 때 프로세스를 선점하여 문맥 전환을 일으킨다.
//T_SYSCALL == 64
void
trap(struct trapframe *tf)
{
  //시스템 콜이 발생한 경우
  if(tf->trapno == T_SYSCALL){
    //프로세스가 killed 요청 상태인지 확인한다.
    if(myproc()->killed)
      exit();
    //시스템 콜이 실행되면서 프로세스의 상태가 변경되는 것을 기록하기 위해서 프로세스의 트랩 프레임 포인터에 저장시킨다.
    myproc()->tf = tf;
    syscall(); //시스템 콜을 실제로 처리하는 함수다.
    if(myproc()->killed) //시스템 콜을 처리하고 나서 다시 killed 상태인지 확인하고 프로세스를 종료한다.
      exit();
    return;
  }
  // 다양한 인터럽트와 예외를 처리하는 코드는 아래의 블록에서 이루어진다.
  switch(tf->trapno){
  case T_IRQ0 + IRQ_TIMER: //ticks는 시스템시간을 의미하는 시간이다. 타이머 인터럽트가 발생되었을 때를 처리하는 것이다.
  //타이머 인터럽트는 cpu 타이머(APIC 타이머가)가 주기적을 발생시켜서 일어나는 것이다.
    if(cpuid() == 0){ //cpu 0에서만 ticks를 관리할 수 있기 때문에 확인하는 것이다.
      //cpu의 id가 0인 경우
      acquire(&tickslock);
      ticks++;
      wakeup(&ticks); //타이머 틱을 기다리고 있는 프로세스들이 다시 실행될 수 있도록 하는 것이다. 이렇게 해서 tick의 주소를 주는 것이다.
      release(&tickslock);
    }
    int lock_ok = 1;
    if(!holding(&ptable.lock)){
        acquire(&ptable.lock);
        lock_ok = 0;
    }
    //여기에 aging적용
    struct proc *current = 0;

    for(int i = 0; i< NUM_QUEUES;i++){
      
      current = ptable.mlfq[i];
      if(current == 0 ){
        continue;
      }
       
      //current에 대한 조건들을 증가시켜준다.
      while(current != 0){
        if (current->state == RUNNABLE) {
          current->cpu_wait++;  // CPU 대기 시간 증가
        } else if (current->state == SLEEPING) {
          current->io_wait_time++;  // I/O 대기 시간 증가
        } else if (current->state == RUNNING){
          current->cpu_burst++;
        }

  

        //shell idle init은 aging하지 않는다.
        if((current->pid != 0) && (current-> pid != 1) && (current->pid != 2)){ 
          if(current->cpu_wait>=250){
            if(current->q_level > 0){
              #ifdef DEBUG
                if(current->pid > 3){
                cprintf("PID: %d Aging\n",current->pid);
                }
              #endif
              remove_proc_from_mlfq(current);
              current->q_level --;
              current->io_wait_time = 0;
              current->cpu_burst = 0;
              current->cpu_wait= 0;
              add_proc_to_mlfq(current,current->q_level);
            }
          }
      }
      current = current ->next;
    }
    }
    if(lock_ok == 0){
      release(&ptable.lock);
      lock_ok = 1;
    }

    lapiceoi();  // 로컬 apic에게 인터럽트 처리가 끝났음을 알리는 신호를 보낸다.
    break;
  case T_IRQ0 + IRQ_IDE:
    ideintr(); //디스크와 관련된 I/O 작업을 처리한다.
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_IDE+1: //하드디스크와 같은 저장장치에 대한 인터럽트
    // Bochs generates spurious IDE1 interrupts.
    break;
  case T_IRQ0 + IRQ_KBD:
    kbdintr(); //키보드 인터럽트로서 키보드로부터 입력된 데이터를 처리하는 함수다.
    lapiceoi();
    break;
  case T_IRQ0 + IRQ_COM1:
    uartintr(); //직렬 포트로 들어오는 데이터를 처리한다.
    lapiceoi();
    break;
  case T_IRQ0 + 7:
  case T_IRQ0 + IRQ_SPURIOUS:  //의도치 않게 발생한 인터럽트로 특별한 이유 없이 발생한 인터럽트를 처리하는 것이다.
    cprintf("cpu%d: spurious interrupt at %x:%x\n",
            cpuid(), tf->cs, tf->eip);
    lapiceoi();
    break;

  //PAGEBREAK: 13
  default:
    if(myproc() == 0 || (tf->cs&3) == 0){
      // In kernel, it must be our mistake.
      // 알 수 없는 트랩이 커널 모드에서 발생하였으면 panic을 호출한다.
      cprintf("unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
              tf->trapno, cpuid(), tf->eip, rcr2());
      panic("trap");
    }
    // In user space, assume process misbehaved.
    // 사용자 모드인 경우는 killed 상태로 바꾼다.
    cprintf("pid %d %s: trap %d err %d on cpu %d "
            "eip 0x%x addr 0x%x--kill proc\n",
            myproc()->pid, myproc()->name, tf->trapno,
            tf->err, cpuid(), tf->eip, rcr2());
    myproc()->killed = 1;
  }

  // killed 상태인 프로세스가 사용자 모드에 있는지 확인하고 exit을 호출해 프로세스를 종료한다.
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();

  // Force process to give up CPU on clock tick.
  // If interrupts were on while locks held, would need to check nlock.
  if(myproc() && myproc()->state == RUNNING &&
     tf->trapno == T_IRQ0+IRQ_TIMER){
    //필요한 시간만큼 있다가 yield되어서 다음 프로세스로 이동될 수 있도록 한다.
    //시간이 지남에 따라 이동
    

    //큐의 레벨이 0인 경우
    if(myproc()->q_level == 0){
          if(myproc()->end_time > 0){ //set_proc_info 시스템콜을 사용하였을 때만 수행될 수 있게 한다.
        
        if(myproc()->cpu_burst <= 10){
        if((myproc()->end_time - myproc()->stack_cpu_burst) <=10){
            if((myproc()->end_time-myproc()->stack_cpu_burst) <= myproc()->cpu_burst){
              myproc()->stack_cpu_burst += myproc()->cpu_burst;
            }
            //지금까지 모인 cpu_burst의 시간이 end_time보다 더 커지면 종료하게 한다.
        if(myproc()->stack_cpu_burst >= myproc()->end_time){
          #ifdef DEBUG
            cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->cpu_burst,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
            cprintf("PID: %d, used %d ticks. terminated\n",myproc()->pid,myproc()->end_time);
          #endif
          exit();
        }
      }
      } 
    }
      if(myproc()->cpu_burst >= 10){
          //일반적으로 0큐에서는 10만큼 tick이 지나면 yield를 호출되게끔 한다.
          if(myproc()->state == RUNNING){
            myproc()->stack_cpu_burst += myproc()->cpu_burst;
          }
          #ifdef DEBUG
            cprintf("PID: %d uses 10 ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
          #endif
           yield();
      }
    } else if(myproc()->q_level == 1){
      if(myproc()->end_time > 0){
      
        if(myproc()->cpu_burst <= 20){
        if((myproc()->end_time - myproc()->stack_cpu_burst) <=20){
            if((myproc()->end_time-myproc()->stack_cpu_burst) <= myproc()->cpu_burst){
              myproc()->stack_cpu_burst += myproc()->cpu_burst;
            }
        if(myproc()->stack_cpu_burst >= myproc()->end_time){
            #ifdef DEBUG
            cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->cpu_burst,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
            cprintf("PID: %d, used %d ticks. terminated\n",myproc()->pid,myproc()->end_time);
            #endif
          exit();
        }
      }
      } 
      }
          
          if(myproc()->cpu_burst >= 20){
              if(myproc()->state == RUNNING){
                myproc()->stack_cpu_burst += myproc()->cpu_burst;
              }
              #ifdef DEBUG
                cprintf("PID: %d uses 20 ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
              //시간이 다 되어 끝난 경우 DEBUG 모드 일때만 출력을 하고 yield된다.
              #endif
              yield();
          }
    } else if(myproc()->q_level == 2){
      //큐 레벨이 2일 경우도 큐레벨이 1일때와 0일때와 동일하게 동작한다.
      if(myproc()->end_time > 0){
  
        if(myproc()->cpu_burst <= 40){
        if((myproc()->end_time - myproc()->stack_cpu_burst) <=40){
            if((myproc()->end_time-myproc()->stack_cpu_burst) <= myproc()->cpu_burst){
              myproc()->stack_cpu_burst += myproc()->cpu_burst;
            }
        if(myproc()->stack_cpu_burst >= myproc()->end_time){
          #ifdef DEBUG  
            cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->cpu_burst,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
            cprintf("PID: %d, used %d ticks. terminated\n",myproc()->pid,myproc()->end_time);
          #endif
          exit();
        }
      }
      } 

    }
      if(myproc()->cpu_burst >= 40){
        if(myproc()->state == RUNNING){
            myproc()->stack_cpu_burst +=myproc()->cpu_burst;
          }
          #ifdef DEBUG
          cprintf("PID: %d uses 40 ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
          #endif
          yield(); 
      }
    } else if(myproc()->q_level == 3){
      //큐레벨이 3인 경우
      int reamaining = 0;
      if(myproc()->end_time > 0){
          reamaining = (myproc()->end_time - myproc()->stack_cpu_burst);

          if(reamaining < 80){
            if(myproc()->cpu_burst%80 >= reamaining){
                myproc()->stack_cpu_burst += myproc()->cpu_burst%80;
                //남은 시간보다 80이 더 크면 해당 로직을 실행하여 종료시킨다.
              if(myproc()->stack_cpu_burst >= myproc()->end_time ){
                #ifdef DEBUG
                cprintf("PID: %d uses %d ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->cpu_burst%80,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
                cprintf("PID: %d, used %d ticks. terminated\n",myproc()->pid,myproc()->end_time);
                #endif
                exit();
              }
       }
      }
      }

      if(myproc()->cpu_burst >= 80){
              //큐레벨이 3일때는 cpu_burst가 종료되기 전까지 계속 증가할 수 있는데 80보다 큰 값을 계속 넣어주면 문제가되기때문에
              //80으로 나눴을 때 나머지가 0인 경우에 증가하고 yield되도록 설정한다.
              int divi = myproc()->cpu_burst%80;
              if(divi==0){
                myproc()->stack_cpu_burst += 80;
                #ifdef DEBUG
                  cprintf("PID: %d uses 80 ticks in mlfq[%d], total(%d/%d)\n",myproc()->pid,myproc()->q_level,myproc()->stack_cpu_burst,myproc()->end_time);
                #endif
                //시간이 다 되어 끝난 경우
       
                yield(); 
               }
      }
    }
    }
  
  // Check if the process has been killed since we yielded
  if(myproc() && myproc()->killed && (tf->cs&3) == DPL_USER)
    exit();
}
