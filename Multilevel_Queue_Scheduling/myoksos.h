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
//PUSHFD�� ���� ���� EFLAGS �������͸� ���ÿ� �ӽ� ����
//EFLAGS�������ʹ� �ý��� ���� ������ �������
//CLI�� ���� ���ͷ�Ʈ ���� �÷��� �����Ͽ� �ٸ� �������� Context Switching ����
//�ٸ� OS������ ��Ÿ ���ͷ�Ʈ ����� ����ϵ��� �ϱ� ���� �ٸ� CPU ��ɾ� ���
#define ENTER_CRITICAL_SECTION()	__asm	PUSHFD	__asm	CLI

//POPFD��ɾ ���� ���� EFLAGS �������� ���� ����
#define EXIT_CRITICAL_SECTION()		__asm	POPFD

extern KERNELAPI VOID WRITE_PORT_UCHAR(IN PUCHAR Port, IN UCHAR Value);
extern KERNELAPI VOID HalTaskSwitch(VOID);

typedef DWORD (*PKSTART_ROUTINE)(PVOID StartContext);

typedef enum _THREAD_STATUS{
	THREAD_STATUS_STOP,				//���� ����
	THREAD_STATUS_TERMINATED,		//���� ����
	THREAD_STATUS_READY,			//���� �غ� �Ϸ� ����
	THREAD_STATUS_WAITING,			//��� ����
	THREAD_STATUS_RUNNING,			//���� ����
} THREAD_STATUS;

// ����1�� ���� �ڵ� �߰�!!!!!!!!
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
// ����1�� ���� �ڵ� �߰�!!!!!!!!
KERNELAPI BOOL		PsCreateThreadPriority(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete, SCHEDULE_PRIORITY Priority);
//!!!!!!!!
		
//�޸� �Ҵ�, ����
KERNELAPI VOID		*MmAllocateNonCachedMemory(IN ULONG NumberOfBytes);
KERNELAPI VOID		MmFreeNonCachedMemory(IN PVOID BaseAddress);

#endif /* #ifndef _MYOKSOS_H_ */