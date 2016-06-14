#include "myoksos.h"

BOOL KrnInitializeKernel(VOID);

extern BOOL HalInitializeHal(VOID);

//커널 초기화를 하기 위한 함수 추가
BOOL KrnInitializeKernel(VOID)
{
	if(!HalInitializeHal()){
		DbgPrint("HalInitializeHal() returned an  error.\r\n");
		return FALSE;
	}

	if(!MmkInitializeMemoryManager()){
		DbgPrint("MmkInitializeMemoryManager() returned an error.\r\n");
		return FALSE;
	}

	if(!PskInitializeProcessManager()){
		DbgPrint("PskInitializeProcessManager() returned an error.\r\n");
		return FALSE;
	}

	if(!SysInitializeSyscall()){
		DbgPrint("SysInitializeSyscall() returned an error.\r\n");
		return FALSE;
	}

	return TRUE;
}