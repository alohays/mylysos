#include "mylysos.h"

static void halt(char *pMsg);

extern BOOL KrnInitializeKernel(VOID);
extern BOOL CrtInitializeDriver(VOID);
extern BOOL KbdInitializeDriver(VOID);
extern BOOL FddInitializeDriver(VOID);
extern BOOL HshInitializeShell(VOID);
int mylysos_init(void)
{
	 //�ܼ� �ý��� �ʱ�ȭ �Լ� ȣ��
   if(!CrtInitializeDriver())
   {
      halt(NULL);
   }
   
   //Ŀ�� �ʱ�ȭ ��ƾ�� ȣ��
   if(!KrnInitializeKernel())
   {
	   halt("KrnInitializeKernel() returned an error.\r\n");
   }

   //Ű���� ����̽� ����̹� �ʱ�ȭ �Լ� ȣ��
   if(!KbdInitializeDriver())
   {
	   halt("KbdInitializeDriver() returned an error.\r\n");
   }

   CrtPrintf("Keyboard Driver is initialized successfully!!\r\n");

	//�÷��� ��ũ ����̽� ����̹� �ʱ�ȭ �Լ� ȣ��
   if(!FddInitializeDriver())
   {
	   halt("FddInitializeDriver() returned an error.\r\n");
   }
   CrtPrintf("Floppy Disk Driver is initialized successfully!!\r\n");
	
   //�� �ʱ�ȭ �Լ�
	if(!HshInitializeShell())
	{
		halt("HshInitializeShell() returned an error.\r\n");
	}

   //ù �½�ũ ����Ī ����
   _asm {
	   push   eax

	   pushfd
	   pop    eax
	   or     ah, 40h ; nested
	   push   eax
	   popfd

	   pop   eax
	   iretd
   }

   
   //���� �ʱ�ȭ ���н�, �� �κ� ���� (��ü �ý��� ����)
   halt("Booting Error!\r\n");
   return 0;
}

//Ŀ�� ���� �Լ�, ����� ����� ���� �޼��� ǥ��.
static void halt(char *pMsg)
{
   if(pMsg !=NULL){
      DbgPrint(pMsg);
      DbgPrint("Halting system.\r\n");
   }
   while(1);

}
