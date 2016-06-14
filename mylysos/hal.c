#include "mylysos.h"
#include "sys_desc.h"
//#include "syscall_type.h"

#pragma pack(push, 1)
//INTERRUPT HANDLER 구조체 선언
typedef struct _INT_HANDLER {
	BYTE	number;
	int		offset;
	WORD	type;
} INT_HANDLER, *PINT_HANDLER;
#pragma pack(pop)


BOOL HalInitializeHal(VOID);
BOOL HalSetupTSS(TSS_32 *pTss32, BOOL IsKernelTSS, int EntryPoint, int *pStackBase, DWORD StackSize);
BOOL HalSetupTaskLink(TSS_32 *pTss32, WORD TaskLink);
BOOL HalSetupTaskSwitchingEnv(TSS_32 *pTss32);
BOOL HalWriteTssIntoGdt(TSS_32 *pTss32, DWORD TssSize, DWORD TssNumber, BOOL SetBusy);

extern SEGMENT_DESC			m_GdtTable[NUMBERS_OF_GDT_ENTRIES];
extern VOID *SysGetSyscallStackPtr(VOID);
extern DWORD *SysGetSyscallStackSize(VOID);
extern VOID Kbd_IRQ_Handler(VOID);
extern VOID Fdd_IRQ_Handler(VOID);

VOID HalChangeTssBusyBit(WORD TssSeg, BOOL SetBit);

static BOOL HalpInitializeProcessor(void);
static BOOL HalpEnableA20(void);
static BOOL HalpInitPIC(void);
static BOOL HalpInitSysTimer(BYTE timeoutPerSecond);
static BOOL HalpStartIntService(void);



#define MAX_IDT					0x40	/* 0x00 ~ 0x3f */

//하드웨어 관련 기능을 초기화 함수 호출
BOOL HalInitializeHal(VOID)
{
	//실질적인 하드웨어 초기화 함수 호출
	if(!HalpInitializeProcessor()) {
		DbgPrint("HalpInitializeProcessor() returned an error.\r\n");
		return FALSE;
	}

	return TRUE;
}

BOOL HalSetupTSS(IN OUT TSS_32		*pTss32,
				 IN		BOOL		IsKernelTSS,
				 IN		int			EntryPoint,
				 IN		int			*pStackBase,
				 IN		DWORD		StackSize)
{
	DWORD dwEFLAGS;
	int stack = (int)pStackBase + StackSize - 1;

	memset(pTss32, NULL, sizeof(TSS_32));

	_asm {
		push		eax
		
		pushfd
		pop			eax
		or			ah, 02h		; IF
		mov			dwEFLAGS, eax

		pop			eax
	}
	if(IsKernelTSS) {
		pTss32->cs				= KERNEL_CS;
		pTss32->ds				= KERNEL_DS;
		pTss32->ss				= KERNEL_SS;
	} else {
		pTss32->cs				= USER_CS;
		pTss32->ds				= USER_DS;
		pTss32->ss				= USER_SS;
	}
	pTss32->es					= pTss32->ds;
	pTss32->fs					= pTss32->ds;
	pTss32->gs					= pTss32->ds;

	pTss32->eflags				= dwEFLAGS;
	pTss32->eip					= EntryPoint;
	pTss32->esp					= (DWORD)stack;

	pTss32->ss0					= KERNEL_SS;
	pTss32->esp0				= ((DWORD)((BYTE *)SysGetSyscallStackPtr)+SysGetSyscallStackSize()-1);

	HalSetupTaskSwitchingEnv(pTss32);

	return TRUE;
}

BOOL HalSetupTaskSwitchingEnv(TSS_32 *pTss32)
{
	int stack;

ENTER_CRITICAL_SECTION();
	stack = pTss32->esp;
	stack -= sizeof(int);
	*((int *)stack) = pTss32->eflags;
	stack -= sizeof(int);
	*((int *)stack) = pTss32->cs;
	stack -= sizeof(int);
	*((int *)stack) = pTss32->eip;
	pTss32->esp = stack;
EXIT_CRITICAL_SECTION();

	HalChangeTssBusyBit(TASK_SW_SEG, TRUE);

	return TRUE;
}

BOOL HalSetupTaskLink(TSS_32 *pTss32, WORD TaskLink)
{
ENTER_CRITICAL_SECTION();
	pTss32->eflags |= 0x4000;
	pTss32->prev_task_link = TaskLink;
EXIT_CRITICAL_SECTION();

	return TRUE;
}

BOOL HalWriteTssIntoGdt(TSS_32 *pTss32, DWORD TssSize, DWORD TssNumber, BOOL SetBusy)
{
ENTER_CRITICAL_SECTION();
	m_GdtTable[TssNumber>>3].limit_1		=  (BYTE)(TssSize & 0x000000ff);
	m_GdtTable[TssNumber>>3].limit_2		= (BYTE)((TssSize & 0x0000ff00) >> 8);
	m_GdtTable[TssNumber>>3].glimit_3		= (BYTE)((TssSize & 0x000f0000) >> 16);
	m_GdtTable[TssNumber>>3].base_1			=  (BYTE)(((int)pTss32) & 0x000000ff);
	m_GdtTable[TssNumber>>3].base_2			= (BYTE)((((int)pTss32) & 0x0000ff00) >> 8);
	m_GdtTable[TssNumber>>3].base_3			= (BYTE)((((int)pTss32) & 0x00ff0000) >> 16);
	m_GdtTable[TssNumber>>3].base_4			= (BYTE)((((int)pTss32) & 0xff000000) >> 24);
	m_GdtTable[TssNumber>>3].type			= (SetBusy ? 0x8b : 0x89);
EXIT_CRITICAL_SECTION();

	return TRUE;
}

VOID HalChangeTssBusyBit(WORD TssSeg, BOOL SetBit)
{
ENTER_CRITICAL_SECTION();
	if(SetBit) {
		m_GdtTable[TssSeg>>3].type |= 0x02;
	} else {
		m_GdtTable[TssSeg>>3].type &= 0xfd;
	}
EXIT_CRITICAL_SECTION();
}

//실질적으로 여러가지 하드웨어 초기화 함수
static BOOL HalpInitializeProcessor(void)
{
	//A20 Line 활성화 함수
	if(!HalpEnableA20()) {
		DbgPrint("HalpEnableA20() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("A20 line is success!!\r\n");

	//PIC 초기화 함수
	if(!HalpInitPIC()) {
		DbgPrint("HalpInitPIC() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("PIC is success!!\r\n");

	//TIMER 초기화 함수
	if(!HalpInitSysTimer(TIMEOUT_PER_SECOND)) {
		DbgPrint("HalpInitSysTimer() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("SystemTimer is initialized!!\r\n");

	//INTERRUPT 초기화 함수 
	if(!HalpStartIntService()) {
		DbgPrint("HalpStartIntService() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("Interrupt Service is installed!!\r\n");


	return TRUE;
}
static BOOL HalpEnableA20(void)
{
	int *test_1 = (int *)0x00000000, test_1_buf;
	int *test_2 = (int *)0x00100000, test_2_buf;
	UCHAR status, flag;

	//Status Register(0x64)를 읽어서 입력 버퍼에 데이터가 없을 때까지 대기.
	do { status = READ_PORT_UCHAR((PUCHAR)0x64); }
	while( status & 0x02 );
	//Control Register에 데이터를 쓰는 과정
	//output port에서 데이터를 읽어서 출력 버퍼(0x60)에 데이터를 저장하라는 명령.
		WRITE_PORT_UCHAR((PUCHAR)0x64, 0xd0);

	//Status Register(Ox64)를 읽어서 출력 버퍼(Ox60)에 데이터가 들어올때 까지 대기.
	do { status = READ_PORT_UCHAR((PUCHAR)0x64); }
	while( !(status&0x01) );
	//출력 버퍼(Ox60)에서 데이터를 읽은 후에 A20 GATE(두 번째 비트)활성화 	
	flag = READ_PORT_UCHAR((PUCHAR)0x60);
	flag |= 0x02; // A20 line 활성화

	//flag값을 Register에 써넣어 A20 활성화
	do { status = READ_PORT_UCHAR((PUCHAR)0x64); }
	while( status & 0x02 );
	WRITE_PORT_UCHAR((PUCHAR)0x64, 0xd1);

	do { status = READ_PORT_UCHAR((PUCHAR)0x64); } 
	while( status & 0x02 );
	WRITE_PORT_UCHAR((PUCHAR)0x60, flag); 

	do { status = READ_PORT_UCHAR((PUCHAR)0x64); } 
	while( status & 0x02 );

	//Test A20 line
	test_1_buf = *test_1;
	test_2_buf = *test_2;
	*test_1 = 0xff00ccaa;
	*test_2 = 0x22cc11dd;
	if(*test_1 == *test_2) {
		*test_1 = test_1_buf;
		return FALSE;
	}
	//A20 line이 활성화 되었다면, test_1과 test_2의 값이 달라야 한다.
	*test_1 = test_1_buf;
	*test_2 = test_2_buf;

	return TRUE;
}
/*
 * INTERRUPT TABLE MAP
 *
 * 0x00 ~ 0x1f : Intel Reserved
 * 0x20 ~ 0x2f : IRQ relocated
 * 0x30 ~ 0x3f : For myoksOS
 * 0x40 ~ 0xff : Reserved, we dont use.
 */
static BOOL HalpInitPIC(void)
{
	// Master PIC
	WRITE_PORT_UCHAR((PUCHAR)0x20, 0x11); //ICW1 : Cascade mode, ICW4 
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x20); //ICW2 : 인터럽트 시작 값 INT vector 20h 
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x04); //ICW3  
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x01); //ICW4 
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x00);

	// Slave PIC 
	WRITE_PORT_UCHAR((PUCHAR)0xa0, 0x11); //ICW1 : Cascade mode, ICW4 
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x28); //ICW2 : 인터럽트 시작 값 INT vector 28h 
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x02); //ICW3  
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x01); //ICW4 
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x00);

	return TRUE;
}

static BOOL HalpInitSysTimer(BYTE timeoutPerSecond)
{
	//새로운 counter 값 입력
	WORD timeout = (WORD)(1193180/timeoutPerSecond);

	WRITE_PORT_UCHAR((PUCHAR)0x43, 0x34);

	//COUNTER 0 Register의 Low, High byte 값 설정
	WRITE_PORT_UCHAR((PUCHAR)0x40, (UCHAR)(timeout & 0xff));
	WRITE_PORT_UCHAR((PUCHAR)0x40, (UCHAR)(timeout >> 8));

	return TRUE;
}

//EXCEPTION(예외상황) Handler 구현 함수
//0
_declspec(naked) static void Halp_ECT_DivideByZero(void)
{
	static char *msg = "Halp_ECT_DivideByZero() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//1
_declspec(naked) static void Halp_ECT_DebugException(void)
{
	static char *msg = "Halp_ECT_DebugException() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//2
_declspec(naked) static void Halp_ECT_NMI(void)
{
	static char *msg = "Halp_ECT_NMI() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//3
_declspec(naked) static void Halp_ECT_Breakpoint(void)
{
	static char *msg = "Halp_ECT_Breakpoint() \r\n";
	static int dwESP;
	_asm {
		cli
		mov	dwESP, esp
		pushad
	}

	DbgPrint("bp, #1:0x%x, #2:0x%x, #3:0x%x \r\n", *((int *)dwESP), *((int *)dwESP+1), *((int *)dwESP+2));
	while(1) ;

	_asm {
		popad
		iretd
	}
}
//4
_declspec(naked) static void Halp_ECT_INTO(void)
{
	static char *msg = "Halp_ECT_INTO() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//5
_declspec(naked) static void Halp_ECT_BOUNDS(void)
{
	static char *msg = "Halp_ECT_BOUNDS() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//6
_declspec(naked) static void Halp_ECT_InvaildOpcode(void)
{
	static char *msg = "Halp_ECT_InvaildOpcode() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

infinate:
		jmp		infinate

		popad
		iretd
	}
}
//7
_declspec(naked) static void Halp_ECT_DeviceNotAvailable(void)
{
	static char *msg = "Halp_ECT_DeviceNotAvailable() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//8
_declspec(naked) static void Halp_ECT_DoubleFault(void)
{
	_asm {
		mov		ebx, 0b8000h
		mov		ecx, 80*25

fault_loop:
		mov		byte ptr [ebx], '#'
		mov		byte ptr [ebx+1], 7
		add		ebx, 2
		loop	fault_loop

infinate:
		jmp		infinate
	}
}
//9
_declspec(naked) static void Halp_ECT_CoprocessorOverrun(void)
{
	static char *msg = "Halp_ECT_CoprocessorOverrun() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//10
_declspec(naked) static void Halp_ECT_InvalidTSS(void)
{
	static char *msg = "Halp_ECT_InvalidTSS() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//11
_declspec(naked) static void Halp_ECT_SegmentNotPresent(void)
{
	static char *msg = "Halp_ECT_SegmentNotPresent() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//12
_declspec(naked) static void Halp_ECT_StackException(void)
{
	static char *msg = "Halp_ECT_StackException() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//13
_declspec(naked) static void Halp_ECT_GeneralProtection(void)
{
	static char *msg = "Halp_ECT_GeneralProtection() \r\n";
	static int dwESP;
	_asm {
		mov		dwESP, esp
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4
	}

	DbgPrint("gp, #1:0x%x, #2:0x%x, #3:0x%x \r\n", *((int *)dwESP+0), *((int *)dwESP+1), *((int *)dwESP+2));

	_asm {
infinate:
		jmp		infinate

		popad
		iretd
	}
}
//14
_declspec(naked) static void Halp_ECT_PageFault(void)
{
	static char *msg = "Halp_ECT_PageFault() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//15
_declspec(naked) static void Halp_ECT_FloatingPointError(void)
{
	static char *msg = "Halp_ECT_FloatingPointError() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//16
_declspec(naked) static void Halp_ECT_AlignmentCheck(void)
{
	static char *msg = "Halp_ECT_AlignmentCheck() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}
//17
_declspec(naked) static void Halp_ECT_MachineCheck(void)
{
	static char *msg = "Halp_ECT_MachineCheck() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		popad
		iretd
	}
}


//IRQ Handler 구현 함수
//IRQ1
_declspec(naked) static void Halp_IRQ_Keyboard(void)
{
	_asm {
		pushad
		pushfd

		push	ds
		push	es
		push	fs
		push	gs

		mov		ax, KERNEL_DS		; change to kernel data segment from user-mode(sometimes) data segment
		mov		ds, ax
		mov		es, ax
		mov		fs, ax
		mov		gs, ax
	} 
	Kbd_IRQ_Handler();
	WRITE_PORT_UCHAR((PUCHAR)0x20, 0x20);

	_asm {
		pop		gs
		pop		fs
		pop		es
		pop		ds

		popfd
		popad
		iretd
	}
}
//IRQ2
_declspec(naked) static void Halp_IRQ_ReqFromSlave8259(void)
{
	static char *msg = "Halp_IRQ_ReqFromSlave8259() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al

		popad
		iretd
	}
}
//IRQ3
_declspec(naked) static void Halp_IRQ_COM2(void)
{
	static char *msg = "Halp_IRQ_COM2() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al

		popad
		iretd
	}
}
//IRQ4
_declspec(naked) static void Halp_IRQ_COM1(void)
{
	static char *msg = "Halp_IRQ_COM1() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al

		popad
		iretd
	}
}
//IRQ5
_declspec(naked) static void Halp_IRQ_LPT2(void)
{
	static char *msg = "Halp_IRQ_LPT2() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al

		popad
		iretd
	}
}
//IRQ6
_declspec(naked) static void Halp_IRQ_FloppyDisk(void)
{

	_asm {
		pushad
		pushfd

		push	ds
		push	es
		push	fs
		push	gs

		mov		ax, KERNEL_DS		; change to kernel data segment from user-mode(sometimes) data segment
		mov		ds, ax
		mov		es, ax
		mov		fs, ax
		mov		gs, ax
	}

	Fdd_IRQ_Handler();
	WRITE_PORT_UCHAR((PUCHAR)0x20, 0x20);


	_asm {
		pop		gs
		pop		fs
		pop		es
		pop		ds

		popfd
		popad
		iretd
	}
}
//IRQ7
_declspec(naked) static void Halp_IRQ_LPT1(void)
{
	static char *msg = "Halp_IRQ_LPT1() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al

		popad
		iretd
	}
}

_declspec(naked) static void Halp_IRQ_CMOSClock(void)
{
	static char *msg = "Halp_IRQ_CMOSClock() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al
		out		0a0h, al

		popad
		iretd
	}
}

_declspec(naked) static void Halp_IRQ_VGA(void)
{
	static char *msg = "Halp_IRQ_VGA() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al
		out		0a0h, al

		popad
		iretd
	}
}

_declspec(naked) static void Halp_IRQ_Mouse(void)
{
	static char *msg = "Halp_IRQ_Mouse() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al
		out		0a0h, al

		popad
		iretd
	}
}

_declspec(naked) static void Halp_IRQ_MathCoprocessor(void)
{
	static char *msg = "Halp_IRQ_MathCoprocessor() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al
		out		0a0h, al

		popad
		iretd
	}
}

_declspec(naked) static void Halp_IRQ_HardDrive(void)
{
	static char *msg = "Halp_IRQ_HardDrive() \r\n";
	_asm {
		pushad

		push	msg
		call	CrtPrintf
		add		esp, 4

		mov		al, 20h
		out		20h, al
		out		0a0h, al

		popad
		iretd
	}
}

/**********************************************************************************************************
 *                                       IDT ASSOCIATED FUNCTIONS                                         *
 **********************************************************************************************************/


#define IDTC_PRESENT			0x8000 //현재 Entry가 메모리에 존재 여부
#define IDTC_DPL0				0x0000 //IDT Descriptor의 접근가능 레벨 (0:커널 레벨, 3:유저레벨)
#define IDTC_DPL1				0x2000
#define IDTC_DPL2				0x4000
#define IDTC_DPL3				0x6000
#define IDTC_16BIT				0x0000 //16BIT 또는 32BIT Descriptor
#define IDTC_32BIT				0x0800
#define IDTC_INTGATE			0x0600 //INTERRUPT HANDLER
#define IDTC_TRAPGATE			0x0700 //EXCEPTION HANDLER
#define IDTC_TASKGATE			0x0500

static IDT_GATE			m_IdtGate[MAX_IDT];
static IDTR_DESC		m_IdtrDesc;

//IDT에 대한 초기화
//각각의 INTERRUPT에 대한 함수에 대한 주소, Handler, 속성값 설정
static INT_HANDLER		m_IntHandlers[] = {
	//예외(EXCEPTION)
	{ 0x00, (int)Halp_ECT_DivideByZero,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x01, (int)Halp_ECT_DebugException,		IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x02, (int)Halp_ECT_NMI,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x03, (int)Halp_ECT_Breakpoint,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x04, (int)Halp_ECT_INTO,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x05, (int)Halp_ECT_BOUNDS,				IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x06, (int)Halp_ECT_InvaildOpcode,		IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x07, (int)Halp_ECT_DeviceNotAvailable,	IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x08, (int)Halp_ECT_DoubleFault,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 }, 
	{ 0x09, (int)Halp_ECT_CoprocessorOverrun,	IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x0a, (int)Halp_ECT_InvalidTSS,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x0b, (int)Halp_ECT_SegmentNotPresent,	IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x0c, (int)Halp_ECT_StackException,		IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x0d, (int)Halp_ECT_GeneralProtection,	IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x0e, (int)Halp_ECT_PageFault,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x10, (int)Halp_ECT_FloatingPointError,	IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x11, (int)Halp_ECT_AlignmentCheck,		IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x12, (int)Halp_ECT_MachineCheck,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },

	//IRQ(INTERRUPT REQUEST)
	{ 0x21, (int)Halp_IRQ_Keyboard,				IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x22, (int)Halp_IRQ_ReqFromSlave8259,		IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x23, (int)Halp_IRQ_COM2,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x24, (int)Halp_IRQ_COM1,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x25, (int)Halp_IRQ_LPT2,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x26, (int)Halp_IRQ_FloppyDisk,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x27, (int)Halp_IRQ_LPT1,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x28, (int)Halp_IRQ_CMOSClock,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x29, (int)Halp_IRQ_VGA,					IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x2c, (int)Halp_IRQ_Mouse,				IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x2d, (int)Halp_IRQ_MathCoprocessor,		IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },
	{ 0x2e, (int)Halp_IRQ_HardDrive,			IDTC_PRESENT | IDTC_32BIT | IDTC_INTGATE | IDTC_DPL0 },

	{ SYSTEM_TMR_INT_NUMBER,	(int)TMR_TSS_SEG,		IDTC_PRESENT | IDTC_TASKGATE | IDTC_DPL0	},
	{ SOFT_TASK_SW_INT_NUMBER,	(int)SOFT_TS_TSS_SEG,	IDTC_PRESENT | IDTC_TASKGATE | IDTC_DPL0	},

	{0, 0, 0 }
};



static BOOL HalpStartIntService(void)
{
	int i;

	//메모리 초기화
	memset(m_IdtGate, 0, MAX_IDT*sizeof(IDT_GATE));

	//IDT Table 만들기
	for(i=0; i<MAX_IDT; i++) 
	{
		if(m_IntHandlers[i].offset == 0)
			break;
		
		if((m_IntHandlers[i].type & IDTC_TASKGATE) == IDTC_TASKGATE)
		{
			m_IdtGate[m_IntHandlers[i].number].selector		= (WORD)(m_IntHandlers[i].offset);
			m_IdtGate[m_IntHandlers[i].number].type			= m_IntHandlers[i].type;
		}
		else
		{
			m_IdtGate[m_IntHandlers[i].number].selector		= KERNEL_CS;
			m_IdtGate[m_IntHandlers[i].number].type			= m_IntHandlers[i].type;
			m_IdtGate[m_IntHandlers[i].number].offset_high	= (WORD)(m_IntHandlers[i].offset >> 16);
			m_IdtGate[m_IntHandlers[i].number].offset_low	= (WORD)(m_IntHandlers[i].offset & 0xffff);
		}
	}

	//IDTR 설정
	//IDT크기와 시작 Physical Address 설정 
	m_IdtrDesc.address	= (int)&m_IdtGate;
	m_IdtrDesc.size		= (WORD)(MAX_IDT*sizeof(IDT_GATE));
	//IDTR 포인터를 HalpEnableInterrupt로 전달 -> CPU가 IDT 테이블을 인식
	HalpEnableInterrupt((PIDTR_DESC)&m_IdtrDesc);

	return TRUE;
}