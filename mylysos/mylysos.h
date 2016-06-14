#ifndef _MYLYSOS_H_
#define _MYLYSOS_H_

#include "types.h"
#include "debug.h"

#include "string.h"
#include "vsprintf.h"

#include "6845crt.h"
#include "kbddrv.h"
#include "fdddrv.h"

#include "hshell.h"

#define MYLYSOS_OS_VER               "1st released. 2016-05-27."


KERNELAPI BOOL PsShowTSWachdogClock(BOOL Show);

#define ENTER_CRITICAL_SECTION()     __asm  PUSHFD  __asm CLI

#define EXIT_CRITICAL_SECTION()      __asm  POPFD

extern KERNELAPI VOID WRITE_PORT_UCHAR(IN PUCHAR Port, IN UCHAR Value);

extern KERNELAPI VOID HalTaskSwitch(VOID);


typedef DWORD (*PKSTART_ROUTINE)(PVOID StartContext);

typedef enum _THREAD_STATUS {
   THREAD_STATUS_STOP,
   THREAD_STATUS_TERMINATED,
   THREAD_STATUS_READY,
   THREAD_STATUS_WAITING,
   THREAD_STATUS_RUNNING,
} THREAD_STATUS;

KERNELAPI BOOL   PsCreateProcess(OUT PHANDLE ProcessHandle);

KERNELAPI BOOL   PsDeleteProcess(IN HANDLE ProcessHandle);
KERNELAPI BOOL   PsCreateThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine,
                        IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete);
KERNELAPI BOOL   PsDeleteThread(IN HANDLE ThreadHandle);
KERNELAPI HANDLE PsGetCurrentThread(VOID);

KERNELAPI BOOL   PsSetThreadStatus(IN HANDLE ThreadHandle, IN THREAD_STATUS Status);


KERNELAPI VOID *MmAllocateNonCachedMemory(IN ULONG NumberOfBytes);
KERNELAPI VOID MmFreeNonCachedMemory(IN PVOID BaseAddress);

KERNELAPI BOOL PsCreateUserThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PVOID StartContext);
KERNELAPI THREAD_STATUS PsGetThreadStatus(IN HANDLE ThreadHandle);
KERNELAPI HANDLE PsGetParentProcess(IN HANDLE ThreadHandle);

// 과제2를 위한 코드추가
KERNELAPI DWORD GetTotalMemoryBytes(VOID);
KERNELAPI DWORD GetUsedMemoryBlocks(VOID);
KERNELAPI DWORD GetMemoryBlockSize(VOID);
KERNELAPI VOID ShellCommand_ps(VOID);
// !!!!!!!!

#endif