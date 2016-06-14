#ifndef _MYOKSOS_H_
#define _MYOKSOS_H_

/* common */
#include "types.h"
#include "debug.h"

/* libraries */
#include "string.h"
#include "vsprintf.h"

/* device drivers */
#include "6845crt.h"

/* x86 system specific */
//PUSHFD를 통해 현재 EFLAGS 레지스터를 스택에 임시 저장
//EFLAGS레지스터는 시스템 설정 사항이 들어있음
//CLI를 통해 인터럽트 금지 플래그 설정하여 다른 쓰레드의 Context Switching 방지
//다른 OS에서는 기타 인터럽트 사용은 계속하도록 하기 위해 다른 CPU 명령어 사용
#define ENTER_CRITICAL_SECTION()	__asm	PUSHFD	__asm	CLI

//POPFD명령어를 통해 원래 EFLAGS 레지스터 상태 복원
#define EXIT_CRITICAL_SECTION()		__asm	POPFD

extern KERNELAPI VOID WRITE_PORT_UCHAR(IN PUCHAR Port, IN UCHAR Value);
extern KERNELAPI VOID HalTaskSwitch(VOID);

typedef DWORD (*PKSTART_ROUTINE)(PVOID StartContext);

typedef enum _THREAD_STATUS{
	THREAD_STATUS_STOP,				//정지 상태
	THREAD_STATUS_TERMINATED,		//종료 상태
	THREAD_STATUS_READY,			//실행 준비 완료 상태
	THREAD_STATUS_WAITING,			//대기 상태
	THREAD_STATUS_RUNNING,			//실행 상태
} THREAD_STATUS;

// 과제1을 위한 코드 추가!!!!!!!!
typedef enum _SCHEDULE_PRIORITY{
	PRIORITY_HIGH,
	PRIORITY_NORMAL,
	PRIORITY_LOW
} SCHEDULE_PRIORITY;
//!!!!!!!!


KERNELAPI BOOL		PsCreateProcess(OUT PHANDLE ProcessHandle);

KERNELAPI BOOL		PsDeleteProcess(IN HANDLE ProcessHandle);
KERNELAPI BOOL		PsCreateThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete);

KERNELAPI BOOL		PsCreateIntThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize);

KERNELAPI BOOL		PsDeleteThread(IN HANDLE ThreadHandle);
KERNELAPI HANDLE	PsGetCurrentThread(VOID);

KERNELAPI BOOL		PsSetThreadStatus(IN HANDLE ThreadHandle, IN THREAD_STATUS Status);
// 과제1을 위한 코드 추가!!!!!!!!
KERNELAPI BOOL		PsCreateThreadPriority(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete, SCHEDULE_PRIORITY Priority);
//!!!!!!!!
		
//메모리 할당, 해제
KERNELAPI VOID		*MmAllocateNonCachedMemory(IN ULONG NumberOfBytes);
KERNELAPI VOID		MmFreeNonCachedMemory(IN PVOID BaseAddress);

#endif /* #ifndef _MYOKSOS_H_ */