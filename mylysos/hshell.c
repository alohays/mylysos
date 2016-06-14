#include "hshell.h"

#define DEFAULT_STACK_SIZE				(64*1024) /* 64kbytes */
#define OS_SHELL_VER				"1st released. 2016-05-27."
#define SHELL_PROMPT					"A:\\>"
#define SHELL_PROMPT_LENGTH				strlen(SHELL_PROMPT)
#define MEMORY_POOL_START_ADDRESS	0x00200000


BOOL HshInitializeShell(VOID);

typedef BOOL (*CMD_PROCESS_ROUTINE)(BYTE *pParameters);

//���θ�ɾ� ����ü
typedef struct _INTERNAL_COMMAND {
	
	BYTE					*pt_cmd; //���θ�ɾ��� �̸�
	CMD_PROCESS_ROUTINE		routine; //���� ���θ�ɾ��� ����� �����ϴ� �Լ�

} INTERNEL_COMMAND, *PINTERNEL_COMMAND;

typedef struct _DIR_CMD_CONTEXT {
	WORD		file_cnt;		//���� ���丮�� �ִ� ���� ����
	WORD		dir_cnt;		//���� ���丮�� �ִ� ���� ���丮 ����
	DWORD		total_file_size; //���� ���丮�� �ִ� �� ���� ũ��

} DIR_CMD_CONTEXT, *PDIR_CMD_CONTEXT;

static DWORD				m_MemSize;
static DWORD msize_total;
static DWORD msize_inuse;
static DWORD msize_available;


static DWORD HshpMainThread(PVOID StartContext);
static VOID  HshpPrintPrompt(void);
static BOOL  HshpParser(void);

static BOOL  Hshp_CMD_help(BYTE *pParameters);
static BOOL  Hshp_CMD_cls(BYTE *pParameters);
static BOOL  Hshp_CMD_ver(BYTE *pParameters);

static BOOL  Hshp_CMD_toggle(BYTE *pParameters);
static BOOL  Hshp_CMD_ps(BYTE *pParameters);
static BOOL  Hshp_CMD_free(BYTE *pParameters);
static DWORD Hshp_CMD_excute(BYTE *pCMD, BYTE *pParameters);

static HANDLE m_ProcessHandle, m_ThreadHandle;

#define MAX_CMD_LINE_CHAR			128
static BYTE m_CmdLine[MAX_CMD_LINE_CHAR];

static BOOL Hshp_CMD_dir(BYTE *pParameters);
static BOOL Hshp_CMD_type(BYTE *pParameters);

//���θ�ɾ� ť
static INTERNEL_COMMAND m_InternalCmds[] = {

	"HELP",		Hshp_CMD_help,
	"CLS",		Hshp_CMD_cls,
	"VER",		Hshp_CMD_ver,
	"TOGGLE",	Hshp_CMD_toggle,
	"DIR",		Hshp_CMD_dir,
	"TYPE",		Hshp_CMD_type,
	"PS",		Hshp_CMD_ps,
	"FREE",		Hshp_CMD_free,


	NULL, NULL,

};

//�� �ʱ�ȭ �Լ�
BOOL HshInitializeShell(VOID)
{

	//m_CmdLine���� �ʱ�ȭ
	memset(m_CmdLine, NULL, MAX_CMD_LINE_CHAR);

	//������ �����ؾ� �� ���۵��� ó���ϱ� ���� ���μ��� ����
	if(!PsCreateProcess(&m_ProcessHandle))
		return FALSE;

	//������ �����ؾ� �� ���۵��� ó���ϱ� ���� ������ ����
	if(!PsCreateThread(&m_ThreadHandle, m_ProcessHandle, HshpMainThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(m_ThreadHandle, THREAD_STATUS_READY);

	return TRUE;
}



//�� ó���� ����ϴ� �Լ�
static DWORD HshpMainThread(PVOID StartContext)
{
	
	KBD_KEY_DATA KeyData;
	BYTE cursor_x, cursor_y;
	static int cmd_next_pos=0;

	
	//���Ͻý��� �ʱ�ȭ �Լ� ȣ��
	if(!FsInitializeModule()) {
		DbgPrint("FsInitializeModule() returned an error.\r\n");
		return 0;
	}

	HshpPrintPrompt();

	//�ݺ��ϸ鼭 kbdGetKey�Լ��� �̿��Ͽ� ������ �Է��ϴ� Ű�� �޾ƿ´�.
	while(1) {
		if(!KbdGetKey(&KeyData)) {
			HalTaskSwitch();
			continue;
		}

		//Ư�������� ��� ����ó������ �ǳʶ�
		if(KeyData.type != KBD_KTYPE_GENERAL) {
			continue;
		}

		//�齺���̽� Ű�� �Է��Ѱ�� ó��
		if(KeyData.key == '\b')
		{
			//�ʱ� Ŀ�� ��ġ ����
			CrtGetCursorPos(&cursor_x, &cursor_y);
			//xĿ���� ���� ���ڿ� ���̺��� ���� ���
			if(cursor_x <= SHELL_PROMPT_LENGTH)
				continue;

			m_CmdLine[--cmd_next_pos] = NULL;
		}
		CrtPrintf("%c", KeyData.key); //�Էµ� ���� ���

		//����ڰ� �Է��� ��ɾ ������������, ��Ű������ ���� �˻� ����
		if(KeyData.key == '\r') {
			m_CmdLine[cmd_next_pos] = NULL;
			HshpParser(); //���� ��ɾ� ó��
			HshpPrintPrompt(); //���� ���ο� �� ������Ʈ ���� ���
			cmd_next_pos = 0;
			m_CmdLine[0] = NULL;
		}

		else if(KeyData.key == '\t') {
			m_CmdLine[cmd_next_pos++] = ' ';
		}

		else if(KeyData.key != '\b') {
			m_CmdLine[cmd_next_pos++] = KeyData.key;
		}
	}

	return 0;
}

//�� ������Ʈ�� ȭ�鿡 ����ϴ� �Լ� (A:\>_)
static VOID  HshpPrintPrompt(void)
{

	CrtPrintText(SHELL_PROMPT);

}



//����ڰ� ����� �Է��ϰ� <Enter>Ű�� ���� ��� ��ɾ ó���ϴ� �Լ�
static BOOL  HshpParser(void)
{
		
	int i;
	CMD_PROCESS_ROUTINE	CmdProcessRoutine;
	BYTE *pt_cmd, *pt_parameters;

	//����ó��
	if(m_CmdLine[0] == NULL)
		return TRUE;

	for(i=0; m_CmdLine[i] == ' '; i++);

	//���θ�ɾ� Ȯ�� �� ó��
	pt_cmd = &(m_CmdLine[i]);
	for(++i; m_CmdLine[i] != NULL && m_CmdLine[i] != ' '; i++);

	if(m_CmdLine[i] == NULL){
		pt_parameters = NULL;
		goto $find;
	}
	m_CmdLine[i] = NULL;

	for(++i; m_CmdLine[i] == ' '; i++);

	pt_parameters = &(m_CmdLine[i]);

$find:
	pt_cmd = strupr(pt_cmd);
	for(i=0; m_InternalCmds[i].pt_cmd != NULL; i++) {
		if(strcmp(pt_cmd, m_InternalCmds[i].pt_cmd) == 0 ){
			CmdProcessRoutine = m_InternalCmds[i].routine;
			CmdProcessRoutine(pt_parameters);
			return TRUE;
		}
	}
	Hshp_CMD_excute(pt_cmd, pt_parameters);


	return FALSE;
}


//�ٶ����� On/Off�ϴ� ��ɾ�
static BOOL Hshp_CMD_toggle(BYTE *pParameters)
{

	static BOOL bShow = TRUE;

	bShow = (bShow ? FALSE : TRUE);
	PsShowTSWachdogClock(bShow);

	return TRUE;

}

//clear��ɾ� ����
static BOOL Hshp_CMD_cls(BYTE *pParameters)
{

	CrtClearScreen();

	return TRUE;

}

//help��ɾ� ����
static BOOL Hshp_CMD_help(BYTE *pParameters)
{

	CrtPrintText(
		"help		:	View available commands and their description. \r\n"
		"ver		:	Show the version information of the OS and OS shell. \r\n"
		"cls		:	Clear screen. \r\n"
		"dir		:	Display all files in the current directory. \r\n"
		"toggle		:	Show/hide soft task-switching watchdog clock. \r\n"
		"ps			:	Display process state. \r\n"
		"free		:	Display memory using state. \r\n"


		"\r\n"
		);

	return TRUE;
}



//ver��ɾ� ����
static BOOL Hshp_CMD_ver(BYTE *pParameters)
{

	CrtPrintf(
		"OS Version		: mylysOS OS	(%s) \r\n"
		"Shell Version	: OS Shell (%s) \r\n"
		"Developer		: LeeYunSeong \r\n"
		"e-Mail			: swack9751@naver.com\r\n"
		"Homepage		: dcslab.hanyang.ac.kr/lecture \r\n"
		, MYLYSOS_OS_VER, OS_SHELL_VER
	);

	return TRUE;
}

//�ܺ� ���� ������ �����Ű�� �Լ�
static DWORD Hshp_CMD_excute(BYTE *pCmd, BYTE *pParameters)
{
	int length;
	BYTE buffer[8+3+1+1], *pAddr = (BYTE *)0x00100000, *pPos;
	HANDLE hFile, UserThread;

	//NULL �� Ȯ��
	length = strlen(pCmd);
	if(!length) return 0;

	//buffer �ʱ�ȭ
	memset(buffer, 0, 8+3+1+1);
	strcpy(buffer, pCmd);
	strupr(buffer);

	//EXE ������ �ƴ� ���� ������ ������Ų��.
	if((pPos = strrchr(buffer, '.')) == NULL) {
		strcat(buffer, ".EXE");
	} else {
		if(strcmp(++pPos, "EXE")) {
			CrtPrintText("ERROR : this is not executable file. \r\n");
			return 0;
		}
	}

	//���� ����
	hFile = FsOpenFile(buffer, OF_READ_ONLY);
	if(!hFile) {
		CrtPrintText("ERROE: file open error! \r\n");
		return 0;
	}

	//������ ������ �����͸� �о pAddr�� �����ϴ� �Լ�
	while(FsReadFile(hFile, pAddr, 256) != 0) {
		pAddr += 256;
	}

	//�ش� ���� ������ ���� ��ų �� �ֵ��� ������ ����
	//���� ������ ����
	if(!PsCreateUserThread(&UserThread, PsGetParentProcess(PsGetCurrentThread()), NULL)) {
		FsCloseFile(hFile);
		return 0;
	}

	//������ ���� �����·� ����
	PsSetThreadStatus(UserThread, THREAD_STATUS_READY);
	while(PsGetThreadStatus(UserThread) != THREAD_STATUS_TERMINATED) {
		//�׽�ũ ����Ī
		HalTaskSwitch();
	}

	//�����尡 ����Ǹ� ���������� ����
	PsDeleteThread(UserThread);

	//���� �ݱ�
	FsCloseFile(hFile);

	return 0;
}



//���� ���Ͽ� ���� ������ ȭ�鿡 ����ϴ� �Լ�
static BOOL Hshp_dir_callback(FILE_INFO *pFileInfo, PVOID Context)
{
	WORD year, month, day;
	WORD hour, minute, second;
	BYTE *pAmPm;
	BYTE *pDir;
	PDIR_CMD_CONTEXT pContext = (PDIR_CMD_CONTEXT)Context;

	second = (pFileInfo->time) & 0x001f;
	minute = (pFileInfo->time >> 5) & 0x003f;
	hour = (pFileInfo->time >> 11 ) & 0x001f;

	if(hour > 12) {
		hour -=12;
		pAmPm = "P.M.";
	} else {
		pAmPm = "A.M.";
	}

	day = (pFileInfo->date) & 0x001f;
	month = (pFileInfo->date >> 5 ) & 0x000f;
	year = (pFileInfo->date >> 9)+1980;

	if(pFileInfo->attribute & FILE_ATTR_DIRECTORY) {
		pDir = "<DIR>";
		pContext->dir_cnt++;
	} else {
		pDir = "     ";
		pContext->file_cnt++;
	}

	pContext->total_file_size += pFileInfo->filesize;

	CrtPrintf("%0d-%02d-%02d \t %02d:%02d %s %7d bytes %s %s \r\n",
		year, month, day, hour, minute, pAmPm, pFileInfo->filesize, pDir, pFileInfo->filename);

	return TRUE;

}
static BOOL Hshp_CMD_type(BYTE *pParameters)
{
#define TYPE_BUFFER_SIZE		256
	HANDLE hFile;
	BYTE buffer[TYPE_BUFFER_SIZE+1];
	buffer[TYPE_BUFFER_SIZE]='\0';
	
	if(pParameters == NULL || strlen(pParameters) == 0) {
		CrtPrintText("ERROR : No selected files. \r\n");
		return FALSE;
	}

	//���� ����
	hFile=FsOpenFile(pParameters, OF_READ_ONLY);
	if(!hFile) {
		CrtPrintf("ERROR:  '%s' is not exist! \r\n", pParameters);
		return FALSE;
	}

	//���� ������ �о�´�
	memset(buffer, 0, TYPE_BUFFER_SIZE);
	while(FsReadFile(hFile, buffer, TYPE_BUFFER_SIZE) != 0) {
		CrtPrintf("%s", buffer);
		memset(buffer, 0, TYPE_BUFFER_SIZE);
	}
	CrtPrintf("\r\n");

	FsCloseFile(hFile);

	return TRUE;
}
//DIR ���� ��ɾ�
static BOOL Hshp_CMD_dir(BYTE *pParameters)
{
	DIR_CMD_CONTEXT dir_cmd_context;
	//�ʱ�ȭ
	dir_cmd_context.dir_cnt =0;
	dir_cmd_context.file_cnt =0;
	dir_cmd_context.total_file_size =0;
	

	//���� ���丮�� ���� ������ ������ �´�.
	FsGetFileList(Hshp_dir_callback, &dir_cmd_context);
	CrtPrintf("\t\t\tTotal File Size: %d bytes \t (%d Files) \r\n",
		dir_cmd_context.total_file_size, dir_cmd_context.file_cnt);
	CrtPrintf("\t\t\tTotal Directories: %d \r\n", dir_cmd_context.dir_cnt);

	return TRUE;
}


static BOOL Hshp_CMD_ps(BYTE *pParameters)
{
	CrtPrintf("Process State \r\n");
	ShellCommand_ps(); //process.c�� �Լ��� ������ ���μ��� ���� ����ߴ�.

	return TRUE;
}

static BOOL Hshp_CMD_free(BYTE *pParameters)
{
	DWORD totalBytes, usedBlocks, blockSize, usedBytes, availableBytes;

	totalBytes = GetTotalMemoryBlocks();
	usedBlocks = GetUsedMemoryBlocks();
	blockSize = GetMemoryBlockSize();
	usedBytes = usedBlocks * blockSize;   // ����� �޸� ũ�� = ����� �޸� �� �� * �޸� �� ũ��
	availableBytes = totalBytes - usedBytes; // ��밡���� �޸� ���� = �� �޸� - ����� �޸�


	CrtPrintf("Total Memory Size: %d Bytes \r\n", totalBytes);
	CrtPrintf("In use: %d Bytes \r\n", usedBytes);
	CrtPrintf("Available: %d Bytes \r\n", availableBytes); 
	
	return TRUE;
}