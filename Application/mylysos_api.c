#include "mylysos_api.h"
#include "../string.c"

static int internel_syscall(SYSCALL_MSG *pSyscall);

//�������α׷����� ���α׷��� �����ϱ� ���� ����ϴ� API�Լ�
VOID API_ExitProgram(VOID)
{
	SYSCALL_MSG syscall;

	// SYSCALL_MSG����ü Ŀ�ο� ��û�� ���� �Է�
	syscall.syscall_type = SYSCALL_TERMINATED;
	internel_syscall(&syscall);

	while(1);
}

//�������α׷����� ȭ�鿡 ���ڿ��� ����ϱ� ���� ����ϴ� API�Լ�
VOID API_PrintText(BYTE *pText)
{
	SYSCALL_MSG syscall;

	if(pText == NULL) return;

	//SYSCALL_MSG����ü�� Ŀ�ο� ��û�� ���� �Է�
	syscall.syscall_type = SYSCALL_PRINT_TEXT;
	syscall.parameters.PRINT_TEXT.pt_text = pText;
	internel_syscall(&syscall);
}

//�� ����Ʈ ȣ���ϴ� �Լ�
//SYSCALL_MSG����ü ���ڷ� �ý��� �� ȣ��
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