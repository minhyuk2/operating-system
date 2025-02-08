#include "types.h"
#include "stat.h"
#include "user.h"



int main(int argc, char *argv[]){
      // 스케줄러 테스트 시작
    printf(1, "start scheduler_test\n");
    // 자식 프로세스 생성
    struct proc *p;
    p = fork();
    if(p < 0)
      printf(1,"fork fail\n");

    if (p == 0) {
      //자식 프로세스 설정
      set_proc_info(0, 0, 0, 0, 500);
      // 자식 프로세스 작업
      while (1) {
      }
      // 스케줄러 테스트 종료
     
    }else{
      while(wait()!=-1);
      printf(1, "end of scheduler_test\n");
    }
    exit();
}