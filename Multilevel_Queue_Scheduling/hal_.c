#include "myoksos.h"
#include "sys_desc.h"

BOOL HalInitializeHal(VOID);

static BOOL HalpInitializeProcessor(void);
static BOOL HalpEnableA20(void);
static BOOL HalpInitPIC(void);
static BOOL HalpInitSysTimer(BYTE timeoutPerSecond);
static BOOL HalpStartIntService(void);

//하드웨어 관련 기능을 초기화 함수 호출
BOOL HalInitializeHal(VOID)
{
	//실질적인 하드웨어 초기화 함수 호출
	if(!HalpInitializeProcessor()){
		DbgPrint("HalpInitializeProcessor() returned an error.\r\n");
		return FALSE;
	}
	return TRUE;
}

//실질적으로 여러가지 하드웨어 초기화 함수
static BOOL HalpInitializeProcessor(void)
{
	//A20 line 활성화 함수
	if(!HalpEnableA20()){
		DbgPrint("HalpEnableA20() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("A20 line is success!!\r\n");

	//PIC 초기화 함수
	if(!HalpInitPIC()){
		DbgPrint("HalpInitPIC() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("PIC is success!!\r\n");

	//Timer 초기화 함수
	if(!HalpInitSysTimer(TIMEOUT_PER_SECOND)){
		DbgPrint("HalpInitSysTimer() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("SystemTimer is success!!\r\n");

	return TRUE;
}

static BOOL HalpEnableA20(void)
{
	int *test_1 = (int *)0x00000000, test_1_buf;
	int *test_2 = (int *)0x00100000, test_2_buf;
	UCHAR status, flag;
		

	do{status = READ_PORT_UCHAR((PUCHAR)0x64);}
	while(status & 0x02);
	WRITE_PORT_UCHAR((PUCHAR)0x64, 0xd0);

	do{status = READ_PORT_UCHAR((PUCHAR)0x64);}
	while(!(status & 0x01));
	flag = READ_PORT_UCHAR((PUCHAR)0x60);
	flag |= 0x02;

	do{status = READ_PORT_UCHAR((PUCHAR)0x64);}
	while(status & 0x02);
	WRITE_PORT_UCHAR((PUCHAR)0x64, 0xd1);

	do{status = READ_PORT_UCHAR((PUCHAR)0x64);}
	while(status & 0x02);
	WRITE_PORT_UCHAR((PUCHAR)0x60, flag);

	do{status = READ_PORT_UCHAR((PUCHAR)0x64);}
	while(status & 0x02);

	test_1_buf = *test_1;
	test_2_buf = *test_2;
	*test_1 = 0xff00ccaa;
	*test_2 = 0x22cc11dd;
	if(*test_1 == *test_2){
		*test_1 = test_1_buf;
		return FALSE;
	}

	*test_1 = test_1_buf;
	*test_2 = test_2_buf;

	return TRUE;
}


static BOOL HalpInitPIC(void)
{
	WRITE_PORT_UCHAR((PUCHAR)0x20, 0x11);
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x20);
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x04);
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x01);
	WRITE_PORT_UCHAR((PUCHAR)0x21, 0x00);

	WRITE_PORT_UCHAR((PUCHAR)0xa0, 0x11);
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x28);
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x02);
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x01);
	WRITE_PORT_UCHAR((PUCHAR)0xa1, 0x00);

	return TRUE;
	
}

static BOOL HalpInitSysTimer(BYTE timeoutPerSecond)
{
	WORD timeout = (WORD)(1193180/timeoutPerSecond);

	WRITE_PORT_UCHAR((PUCHAR)0x43, 0x34);
	
	WRITE_PORT_UCHAR((PUCHAR)0x40, (UCHAR)(timeout & 0xff));
	WRITE_PORT_UCHAR((PUCHAR)0x40, (UCHAR)(timeout >> 8));

	return TRUE;
}