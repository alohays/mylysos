#include "myoksos.h"

static void halt(char *pMsg);

extern BOOL KrnInitializeKernel(VOID);
extern BOOL CrtInitializeDriver(VOID);
extern BOOL KbdInitializeDriver(VOID);
extern BOOL FddInitializeDriver(VOID);
#define DEFAULT_STACK_SIZE		(64*1024)


// 과제1을 위한 코드 추가!!!!!!!!
int highNum, normalNum, lowNum;

void HighCount()
{
	while(1){
ENTER_CRITICAL_SECTION();
		highNum++;
		CrtPrintTextXYWithAttr("", 0, 0, NULL);
		CrtPrintf("High Count : %15d\r\n", highNum);  //high 출력
EXIT_CRITICAL_SECTION();
	}
}
void NormalCount()
{
	while(1){
ENTER_CRITICAL_SECTION();
		normalNum++;
		CrtPrintTextXYWithAttr("", 0, 1, NULL);
		CrtPrintf("Normal Count : %15d\r\n", normalNum);  //normal 출력
EXIT_CRITICAL_SECTION();
	}
}
void LowCount()
{
	while(1){
ENTER_CRITICAL_SECTION();
		lowNum++;
		CrtPrintTextXYWithAttr("", 0, 2, NULL);
		CrtPrintf("Low Count : %15d\r\n", lowNum);  //low 출력
EXIT_CRITICAL_SECTION();
	}
}

// !!!!!!!!


int myoksos_init(void){
	
	HANDLE process, h_thread1, h_thread2, h_thread3, n_thread1, n_thread2, n_thread3, l_thread1, l_thread2, l_thread3;

	//콘솔 시스템 초기화 함수 호출
	if(!CrtInitializeDriver()){
		halt(NULL);
	}

	//커널 초기화 루틴의 호출
	if(!KrnInitializeKernel()){
		halt("KrnInitializeKernel() returned an error.\r\n");
	}

	if(!KbdInitializeDriver()){
		halt("KbdInitializeDriver() returned an error.\r\n");
	}

	CrtPrintf("Keyboard Driver is initialized successfully!!\r\n");

	if(!FddInitializeDriver())
	{
		halt("FddInitializeDriver() returned an error.\r\n");
	}
	CrtPrintf("Floppy Disk Driver is initialized successfully!!\r\n");

	// 과제1을 위한 코드 추가!!!!!!!!

	PsCreateProcess(&process);

	PsCreateThreadPriority(&h_thread1, process, HighCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_HIGH);
	PsCreateThreadPriority(&h_thread2, process, HighCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_HIGH);
	PsCreateThreadPriority(&h_thread3, process, HighCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_HIGH);
	PsCreateThreadPriority(&n_thread1, process, NormalCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_NORMAL);
	PsCreateThreadPriority(&n_thread2, process, NormalCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_NORMAL);
	PsCreateThreadPriority(&n_thread3, process, NormalCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_NORMAL);
	PsCreateThreadPriority(&l_thread1, process, LowCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_LOW);
	PsCreateThreadPriority(&l_thread2, process, LowCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_LOW);
	PsCreateThreadPriority(&l_thread3, process, LowCount, NULL, DEFAULT_STACK_SIZE, TRUE, PRIORITY_LOW);

	CrtClearScreen();

	PsSetThreadStatus(h_thread1, THREAD_STATUS_READY);
	//PsSetThreadStatus(h_thread2, THREAD_STATUS_READY);
	//PsSetThreadStatus(h_thread3, THREAD_STATUS_READY);
	PsSetThreadStatus(n_thread1, THREAD_STATUS_READY);
	//PsSetThreadStatus(n_thread2, THREAD_STATUS_READY);
	//PsSetThreadStatus(n_thread3, THREAD_STATUS_READY);
	PsSetThreadStatus(l_thread1, THREAD_STATUS_READY);
	//PsSetThreadStatus(l_thread2, THREAD_STATUS_READY);
	//PsSetThreadStatus(l_thread3, THREAD_STATUS_READY);

	// !!!!!!!!



	// init 쓰레드의 link 필드를 idle 쓰레드로 설정
	_asm {
		push	eax

		pushfd
		pop		eax
		or		ah, 40h ; nested
		push	eax
		popfd

		pop		eax
		iretd
	}
	
	//만약 초기화 실패시, 이 부분 실행(전체 시스템 정지)
	halt("Booting Error!\r\n");
	return 0;
}

//커널 정지 함수, 디버그 모드일 때는 메시지 표시.
static void halt(char *pMsg)
{
	if(pMsg != NULL)
	{
		DbgPrint(pMsg);
		DbgPrint("Halting system.\r\n");
	}
	while(1) ;
}