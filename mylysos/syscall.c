#include "mylysos.h"
#include "sys_desc.h"
#include "syscall_type.h"
#include "key_def.h"

#define DEFAULT_STACK_SIZE			(64*1024) /* 64kbytes */

static BOOL SyspSetupSysCallgate(void);
static BYTE *m_pSyscallStack;

extern CALLGATE_DESC m_GdtTable[NUMBERS_OF_GDT_ENTRIES];

BOOL SysInitializeSyscall(VOID)
{
	if(!SyspSetupSysCallgate()) {
		DbgPrint("SysSetupSysCallgate() returned an error.\r\n");
		return FALSE;
	}

	//시스템 콜 호출시에 커널 영역에서 사용될 스택을 설정
	m_pSyscallStack = MmAllocateNonCachedMemory(DEFAULT_STACK_SIZE);
	if(m_pSyscallStack == NULL) return FALSE;

	return TRUE;
}

VOID *SysGetSyscallStackPtr(VOID)
{
	return (VOID *)m_pSyscallStack;
}

DWORD SysGetSyscallStackSize(VOID)
{
	return DEFAULT_STACK_SIZE;
}


//시스템 콜을 실제로 처리하는 함수
_declspec(naked)  static VOID Sysp_SERVICE_CALL_MANAGER(void)
{
	static PSYSCALL_MSG call_msg;
	static KBD_KEY_DATA key_data;
	static int result=0;

	_asm {
		mov		eax, [esp+8]
		mov		call_msg, eax

		pushad
	}

	switch(call_msg->syscall_type) {

		//시스템 api : 응용프로그램 종료되었을 때 처리
		case SYSCALL_TERMINATED:
			PsSetThreadStatus(PsGetCurrentThread(), THREAD_STATUS_TERMINATED);
			HalTaskSwitch();
			break;

			/// 스크린 api : 화면에 문자열을 출력하는 서비스
		case SYSCALL_PRINT_TEXT:
			CrtPrintText(call_msg->parameters.PRINT_TEXT.pt_text);
			break;

		default:
			break;
	}

	_asm {
		popad
		mov			eax, result
		ret			4
	}
}

//Call Gate Descriptor를 설정하는 함수
static BOOL SyspSetupSysCallgate(void)
{
ENTER_CRITICAL_SECTION();
	m_GdtTable[SYSCALL_GATE>>3].count		= 1;
	m_GdtTable[SYSCALL_GATE>>3].type		= 0xec;
	m_GdtTable[SYSCALL_GATE>>3].selector	= KERNEL_CS;
	
	m_GdtTable[SYSCALL_GATE>>3].offset_1	= (BYTE) (((int)Sysp_SERVICE_CALL_MANAGER) & 0x000000ff);
	m_GdtTable[SYSCALL_GATE>>3].offset_2	= (BYTE)((((int)Sysp_SERVICE_CALL_MANAGER) & 0x0000ff00) >> 8);
	m_GdtTable[SYSCALL_GATE>>3].offset_3	= (BYTE)((((int)Sysp_SERVICE_CALL_MANAGER) & 0x00ff0000) >> 16);
	m_GdtTable[SYSCALL_GATE>>3].offset_4	= (BYTE)((((int)Sysp_SERVICE_CALL_MANAGER) & 0xff000000) >> 24);
EXIT_CRITICAL_SECTION();

	return TRUE;
}