#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// defined constants
#define SUCCESS 0
#define NOT_VALID -1
#define NOT_ACCESSIBLE -2
#define VALID_MASK 0b01
#define ACCESS_MASK 0b10
#define PFN_SHIFT 12

// globl variables for MMU
unsigned int *page_table = NULL;
unsigned int vpn_mask = 0;
unsigned int shift = 0;
unsigned int offset_mask = 0;

// function declaration
void alloc_page_table(int address_space_bits, int page_bytes);
void init_page_table(int address_space_bits, int page_bytes);
void init_mmu_variables(int address_space_bits, int page_bytes);
int mmu_address_translation(unsigned int virtual_address, unsigned int *physical_address);

/* 
   alloc_page_table();
   전역 변수인 page_table을 위한 메모리 공간을 할당.
   페이지 테이블의 크기를 계산 -> 페이지 테이블의 크기와 PTE의 크기를 곱한 만큼 malloc()을 사용하여 동적 메모리를 할당
  -> 세 번째로, malloc()의 반환값을 page_table에 할당 -> 마지막으로, 할당된 메모리를 0으로 초기화
*/
void alloc_page_table(int address_space_bits, int page_bytes)
{
    /* 프로그램 직접 작성*/
    //페이지 테이블의 크기 계산
    int page_table_size = pow(2, address_space_bits - (int)log2(page_bytes));
    //PTE = 32bits => 4byte
    //동적 메모리를 할당함 
    unsigned int *temp =  malloc(sizeof(unsigned int)*page_table_size);
    //page_table에 반환값을 할당
    page_table = temp;

    if(page_table!=NULL){
        //페이지를 0으로 초기화시켜줍니다.
        memset(page_table,0,sizeof(unsigned int) * page_table_size);
    }else{
        printf("malloc error\n");
        exit(1);
    }
}

/* 
   init_page_table();
   초기 PTE들을 page_table에 삽입. 페이지 테이블의 절반만 채워짐. 
   VPN n은 PFN n*2에 매핑되며, 페이지 테이블의 인덱스가 4로 나누어떨어질 때, 해당 PTE는 접근 불가능하게 됨. 
   수정 금지 
*/
void init_page_table(int address_space_bits, int page_bytes)
{
    unsigned int i;
    int page_table_size = pow(2, address_space_bits - (int)log2(page_bytes));

    /* fill the page table only half */
    for (i = 0; i < page_table_size / 2; i++)
    {
        unsigned int *pte = page_table + i;
        *pte = (i * 2) << PFN_SHIFT;
        if (i % 4 == 0)
            *pte = *pte | VALID_MASK; // make this pte as valid and inaccessible
        else
            *pte = *pte | VALID_MASK | ACCESS_MASK; // make this pte as valid and accessible
    }
}

/* 
    init_mmu_variable();
    mmu_address_translation() 함수에 사용할 전역 변수를 초기화.
*/
void init_mmu_variables(int address_space_bits, int page_bytes)
{
    vpn_mask = (0xffffffff >> (int)log2(page_bytes)) << (int)log2(page_bytes);
    shift = (int)log2(page_bytes);
    offset_mask = 0xffffffff >> (sizeof(offset_mask) * 8 - (int)log2(page_bytes));
}

/* 
    mmu_address_translatio();
    가상 주소를 물리적 주소로 변환. 변환에 성공하면, 변환된 주소를 physical_address 변수에 복사하고 SUCCESS를 반환.
    PTE가 유효하지 않으면, 이 함수는 NOT_VALID를 반환. 
    PTE에 접근할 수 없으면, 이 함수는 NOT_ACCESSIBLE을 반환.
*/
int mmu_address_translation(unsigned int virtual_address, unsigned int *physical_address)
{
    /* 프로그램 직접 작성하되 아래 변수 가능하면 사용*/

    unsigned int vpn;
    unsigned int pfn;
    int valid;
    int access;
    unsigned int offset;
    unsigned int *pteArr; //pte의 주소를 나타냅니다.
    unsigned int pte; //pte를 담기 위함입니다.
    //extract the VPN from the Virtual Address
    //shift는 4096 기준 12비트입니다.
    vpn = (virtual_address & vpn_mask) >> 12;
    //form the address of the Page Table Entry (PTE) PTEAddr = PTBR + (VPN + sizeof(PTE)
    pteArr = page_table + vpn;
    //pte에 access합니다.
    pte = *pteArr;
    //valid와 access를 뽑아냅니다.
    valid = pte & VALID_MASK;
    access = (pte & ACCESS_MASK) >> 1;
    //pfn을 구해냅니다.
    pfn = pte >> PFN_SHIFT;
    //접근할 수 없는 경우에는 NOT_ACCESSIBLE
    //페이지 테이블의 인덱스가 4로 나눠 떨어지는 경우에는 접근할 수 없다고 나타내주면 된다.
    if(valid == 1){
        //valid 가능한 것
        if(access == 1){
            //access 가능
            //12비트를 이동하여 pfn 20비트 확보
            offset = virtual_address & offset_mask;
            *physical_address = (pfn << PFN_SHIFT) | offset;
            printf(" (vpn:%08x, pfn: %08x, valid: %d, access: %d) ", vpn, pfn, valid, access);
            return SUCCESS;

        }else{
            //access 가능하지 않은 경우
            printf(" (vpn:%08x, pfn: %08x, valid: %d, access: %d) ", vpn, pfn, valid, access);
            return NOT_ACCESSIBLE;
        }
    }else{
        printf(" (vpn:%08x, pfn: %08x, valid: %d, access: %d) ", vpn, pfn, valid, access);
        return NOT_VALID;
    }
}

/* 
    main() 절대 수정 금지
*/
int main(int argc, char *argv[])
{
    printf("SSU_MMU Simulator\n");

    if (argc != 3)
    {
        printf("Usage: ./mmu [address_space_size_in_bits] [page_size_in_bytes]\n");
        exit(1);
    }

    int address_space_bits = atoi(argv[1]);
    int page_bytes = atoi(argv[2]);

    if (address_space_bits < 1 || address_space_bits > 32)
    {
        printf("address_space_bits shoud be between 1 and 32\n");
        exit(1);
    }

    if (page_bytes < 1 || page_bytes > 4096)
    {
        printf("page_bytes shoud be between 1 and 4096\n");
        exit(1);
    }

    alloc_page_table(address_space_bits, page_bytes);

    if (page_table == NULL)
    {
        printf("Please allocate the page table with malloc()\n");
        exit(1);
    }

    init_page_table(address_space_bits, page_bytes);
    init_mmu_variables(address_space_bits, page_bytes);

    while (1)
    {
        unsigned int value;
        printf("Input a virtual address of hexadecimal value without \"0x\" (-1 to exit): ");
        scanf("%x", &value);

        if (value == -1)
            break;

        printf("Virtual address: %#x", value);

        unsigned int physical_address = 0;
        int result = mmu_address_translation(value, &physical_address);
        if (result == NOT_VALID)
        {
            printf(" -> Segmentation Fault.\n");
        }
        else if (result == NOT_ACCESSIBLE)
        {
            printf(" -> Protection Fault.\n");
        }
        else if (result == SUCCESS)
        {
            printf(" -> Physical address: %#x\n", physical_address);
        }
        else
        {
            printf(" -> Unknown error.\n");
        }
    }

    if (page_table != NULL)
        free(page_table);

    return 0;
}
