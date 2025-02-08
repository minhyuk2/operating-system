#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char *argv[]) {
    printf(1, "start scheduler_test\n");

    // 첫 번째 프로세스 생성
    int pid1 = fork();
    if (pid1 < 0) {
        exit();
    }

    if (pid1 == 0) {
        // 자식 프로세스 설정
        set_proc_info(2, 0, 0, 0, 300);  // 큐 2에서 시작, 300 ticks 수행

        // 작업 수행
        while (1) {
            // 무한 루프 
        }
    } else {
        // 두 번째 프로세스 생성
        int pid2 = fork();
        if (pid2 < 0) {
            exit();
        }

        if (pid2 == 0) {
            // 자식 프로세스 설정
            set_proc_info(2, 0, 0, 0, 300);  // 큐 2에서 시작, 300 ticks 수행

            // 작업 수행
            while (1) {
                // 무한 루프 
            }
        } else {
            // 세 번째 프로세스 생성
            int pid3 = fork();
            if (pid3 < 0) {
                exit();
            }

            if (pid3 == 0) {
                // 자식 프로세스 설정
                set_proc_info(2, 0, 0, 0, 300);  // 큐 2에서 시작, 300 ticks 수행

                // 작업 수행
                while (1) {
                    // 무한 루프
                }
            } else {
                // 부모 프로세스는 자식 프로세스들이 종료될 때까지 대기
                while (wait() != -1);
            }
        }
    }

    printf(1, "end of scheduler_test\n");
    exit();
}
