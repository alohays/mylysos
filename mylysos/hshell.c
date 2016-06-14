#include "hshell.h"

#define DEFAULT_STACK_SIZE				(64*1024) /* 64kbytes */
#define OS_SHELL_VER				"1st released. 2016-05-27."
#define SHELL_PROMPT					"A:\\>"
#define SHELL_PROMPT_LENGTH				strlen(SHELL_PROMPT)
#define MEMORY_POOL_START_ADDRESS	0x00200000


BOOL HshInitializeShell(VOID);

typedef BOOL (*CMD_PROCESS_ROUTINE)(BYTE *pParameters);

//내부명령어 구조체
typedef struct _INTERNAL_COMMAND {
	
	BYTE					*pt_cmd; //내부명령어의 이름
	CMD_PROCESS_ROUTINE		routine; //실제 내부명령어의 기능을 수행하는 함수

} INTERNEL_COMMAND, *PINTERNEL_COMMAND;

typedef struct _DIR_CMD_CONTEXT {
	WORD		file_cnt;		//현제 디렉토리에 있는 파일 개수
	WORD		dir_cnt;		//현재 디렉토리에 있는 하위 디렉토리 개수
	DWORD		total_file_size; //현재 디렉토리에 있는 총 파일 크기

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

//내부명령어 큐
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

//쉘 초기화 함수
BOOL HshInitializeShell(VOID)
{

	//m_CmdLine버퍼 초기화
	memset(m_CmdLine, NULL, MAX_CMD_LINE_CHAR);

	//웹에서 수행해야 할 동작들을 처리하기 위한 프로세스 생성
	if(!PsCreateProcess(&m_ProcessHandle))
		return FALSE;

	//쉘에서 수행해야 할 동작들을 처리하기 위한 쓰레드 생성
	if(!PsCreateThread(&m_ThreadHandle, m_ProcessHandle, HshpMainThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(m_ThreadHandle, THREAD_STATUS_READY);

	return TRUE;
}



//쉘 처리를 담당하는 함수
static DWORD HshpMainThread(PVOID StartContext)
{
	
	KBD_KEY_DATA KeyData;
	BYTE cursor_x, cursor_y;
	static int cmd_next_pos=0;

	
	//파일시스템 초기화 함수 호출
	if(!FsInitializeModule()) {
		DbgPrint("FsInitializeModule() returned an error.\r\n");
		return 0;
	}

	HshpPrintPrompt();

	//반복하면서 kbdGetKey함수를 이용하여 유저가 입렬하는 키를 받아온다.
	while(1) {
		if(!KbdGetKey(&KeyData)) {
			HalTaskSwitch();
			continue;
		}

		//특수문자일 경우 루프처음으로 건너뜀
		if(KeyData.type != KBD_KTYPE_GENERAL) {
			continue;
		}

		//백스페이스 키를 입력한경우 처리
		if(KeyData.key == '\b')
		{
			//초기 커서 위치 설정
			CrtGetCursorPos(&cursor_x, &cursor_y);
			//x커서의 값이 문자열 길이보다 작은 경우
			if(cursor_x <= SHELL_PROMPT_LENGTH)
				continue;

			m_CmdLine[--cmd_next_pos] = NULL;
		}
		CrtPrintf("%c", KeyData.key); //입력된 문자 출력

		//사용자가 입력한 명령어가 실행파일인지, 탭키인지에 대한 검사 수행
		if(KeyData.key == '\r') {
			m_CmdLine[cmd_next_pos] = NULL;
			HshpParser(); //실행 명령어 처리
			HshpPrintPrompt(); //다음 라인에 쉘 프롬프트 문자 출력
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

//쉘 프롬프트를 화면에 출력하는 함수 (A:\>_)
static VOID  HshpPrintPrompt(void)
{

	CrtPrintText(SHELL_PROMPT);

}



//사용자가 명령을 입력하고 <Enter>키를 쳤을 경우 명령어를 처리하는 함수
static BOOL  HshpParser(void)
{
		
	int i;
	CMD_PROCESS_ROUTINE	CmdProcessRoutine;
	BYTE *pt_cmd, *pt_parameters;

	//에외처리
	if(m_CmdLine[0] == NULL)
		return TRUE;

	for(i=0; m_CmdLine[i] == ' '; i++);

	//내부명령어 확인 후 처리
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


//바람개비 On/Off하는 명령어
static BOOL Hshp_CMD_toggle(BYTE *pParameters)
{

	static BOOL bShow = TRUE;

	bShow = (bShow ? FALSE : TRUE);
	PsShowTSWachdogClock(bShow);

	return TRUE;

}

//clear명령어 구현
static BOOL Hshp_CMD_cls(BYTE *pParameters)
{

	CrtClearScreen();

	return TRUE;

}

//help명령어 구현
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



//ver명령어 구현
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

//외부 실행 파일을 실행시키는 함수
static DWORD Hshp_CMD_excute(BYTE *pCmd, BYTE *pParameters)
{
	int length;
	BYTE buffer[8+3+1+1], *pAddr = (BYTE *)0x00100000, *pPos;
	HANDLE hFile, UserThread;

	//NULL 값 확인
	length = strlen(pCmd);
	if(!length) return 0;

	//buffer 초기화
	memset(buffer, 0, 8+3+1+1);
	strcpy(buffer, pCmd);
	strupr(buffer);

	//EXE 파일이 아닌 경우는 실행을 금지시킨다.
	if((pPos = strrchr(buffer, '.')) == NULL) {
		strcat(buffer, ".EXE");
	} else {
		if(strcmp(++pPos, "EXE")) {
			CrtPrintText("ERROR : this is not executable file. \r\n");
			return 0;
		}
	}

	//파일 오픈
	hFile = FsOpenFile(buffer, OF_READ_ONLY);
	if(!hFile) {
		CrtPrintText("ERROE: file open error! \r\n");
		return 0;
	}

	//오픈한 파일의 데이터를 읽어서 pAddr에 저장하는 함수
	while(FsReadFile(hFile, pAddr, 256) != 0) {
		pAddr += 256;
	}

	//해당 실행 파일을 실행 시킬 수 있도록 쓰레드 생성
	//유제 쓰레드 생성
	if(!PsCreateUserThread(&UserThread, PsGetParentProcess(PsGetCurrentThread()), NULL)) {
		FsCloseFile(hFile);
		return 0;
	}

	//쓰레드 상태 대기상태로 변경
	PsSetThreadStatus(UserThread, THREAD_STATUS_READY);
	while(PsGetThreadStatus(UserThread) != THREAD_STATUS_TERMINATED) {
		//테스크 스위칭
		HalTaskSwitch();
	}

	//쓰레드가 종료되면 유저쓰레드 삭제
	PsDeleteThread(UserThread);

	//파일 닫기
	FsCloseFile(hFile);

	return 0;
}



//개별 파일에 대한 정보를 화면에 출력하는 함수
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

	//파일 오픈
	hFile=FsOpenFile(pParameters, OF_READ_ONLY);
	if(!hFile) {
		CrtPrintf("ERROR:  '%s' is not exist! \r\n", pParameters);
		return FALSE;
	}

	//파일 내용을 읽어온다
	memset(buffer, 0, TYPE_BUFFER_SIZE);
	while(FsReadFile(hFile, buffer, TYPE_BUFFER_SIZE) != 0) {
		CrtPrintf("%s", buffer);
		memset(buffer, 0, TYPE_BUFFER_SIZE);
	}
	CrtPrintf("\r\n");

	FsCloseFile(hFile);

	return TRUE;
}
//DIR 내부 명령어
static BOOL Hshp_CMD_dir(BYTE *pParameters)
{
	DIR_CMD_CONTEXT dir_cmd_context;
	//초기화
	dir_cmd_context.dir_cnt =0;
	dir_cmd_context.file_cnt =0;
	dir_cmd_context.total_file_size =0;
	

	//현재 디렉토리의 파일 정보를 가지고 온다.
	FsGetFileList(Hshp_dir_callback, &dir_cmd_context);
	CrtPrintf("\t\t\tTotal File Size: %d bytes \t (%d Files) \r\n",
		dir_cmd_context.total_file_size, dir_cmd_context.file_cnt);
	CrtPrintf("\t\t\tTotal Directories: %d \r\n", dir_cmd_context.dir_cnt);

	return TRUE;
}


static BOOL Hshp_CMD_ps(BYTE *pParameters)
{
	CrtPrintf("Process State \r\n");
	ShellCommand_ps(); //process.c에 함수를 구현해 프로세스 블럭을 사용했다.

	return TRUE;
}

static BOOL Hshp_CMD_free(BYTE *pParameters)
{
	DWORD totalBytes, usedBlocks, blockSize, usedBytes, availableBytes;

	totalBytes = GetTotalMemoryBlocks();
	usedBlocks = GetUsedMemoryBlocks();
	blockSize = GetMemoryBlockSize();
	usedBytes = usedBlocks * blockSize;   // 사용한 메모리 크기 = 사용한 메모리 블럭 수 * 메모리 블럭 크기
	availableBytes = totalBytes - usedBytes; // 사용가능한 메모리 공간 = 총 메모리 - 사용한 메모리


	CrtPrintf("Total Memory Size: %d Bytes \r\n", totalBytes);
	CrtPrintf("In use: %d Bytes \r\n", usedBytes);
	CrtPrintf("Available: %d Bytes \r\n", availableBytes); 
	
	return TRUE;
}