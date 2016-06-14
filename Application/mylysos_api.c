#include "mylysos_api.h"
#include "../string.c"

static int internel_syscall(SYSCALL_MSG *pSyscall);

//응용프로그램에서 프로그램을 종료하기 위해 사용하는 API함수
VOID API_ExitProgram(VOID)
{
	SYSCALL_MSG syscall;

	// SYSCALL_MSG구조체 커널에 요청할 정보 입력
	syscall.syscall_type = SYSCALL_TERMINATED;
	internel_syscall(&syscall);

	while(1);
}

//응용프로그램에서 화면에 문자열을 출력하기 위해 사용하는 API함수
VOID API_PrintText(BYTE *pText)
{
	SYSCALL_MSG syscall;

	if(pText == NULL) return;

	//SYSCALL_MSG구조체에 커널에 요청할 정보 입력
	syscall.syscall_type = SYSCALL_PRINT_TEXT;
	syscall.parameters.PRINT_TEXT.pt_text = pText;
	internel_syscall(&syscall);
}

//콜 게이트 호출하는 함수
//SYSCALL_MSG구조체 인자로 시스템 콜 호출
static int internel_syscall(SYSCALL_MSG *pSyscall)
{
#define SYSCALL_GATE		0x0048
	WORD syscall_ptr[3];

	memset(syscall_ptr, 0, sizeof(WORD)*3);
	syscall_ptr[2] = SYSCALL_GATE;

	_asm{
		push		pSyscall
		call		fword ptr syscall_ptr
		add			esp, 4
	}
}