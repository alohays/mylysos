#include "mylysos.h"

BOOL KrnInitializeKernel(VOID);

extern BOOL HalInitializeHal(VOID);



BOOL KrnInitializeKernel(VOID)
{
	if(!HalInitializeHal()) {
		DbgPrint("HalInitializeHal() returned an error.\r\n");
		return FALSE;
	}

	if(!MmkInitializeMemoryManager()) {
		DbgPrint("MmkInitializeMemoryManager() returned an error.\r\n");
		return FALSE;
	}
	
	if(!PskInitializeProcessManager()) {
		DbgPrint("PskInitializeProcessManager() returned an error.\r\n");
		return FALSE;
	}

	if(!SysInitializeSyscall()) {
		DbgPrint("SysInitializeSyscall() returned an error. \r\n");
		return FALSE;
	}

	return TRUE;
}
