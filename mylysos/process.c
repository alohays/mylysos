#include "mylysos.h"
#include "sys_desc.h"

#define DEFAULT_STACK_SIZE			(64*1024) 
#define TS_WATCHDOG_CLOCK_POS		(0xb8000+(80-1)*2)

#define PsGetProcessPtr(handle)		((PPROCESS_CONTROL_BLOCK)handle)
#define PsGetThreadPtr(handle)		((PTHREAD_CONTROL_BLOCK)handle)


#define MAX_CUTTING_ITEM				30

typedef struct _THREAD_CONTROL_BLOCK {
	HANDLE								parent_process_handle;		/* memory address */

	DWORD								thread_id;					/* thread id */
	HANDLE								thread_handle;				/* memory address */
	THREAD_STATUS						thread_status;				/* thread status */
	BOOL								auto_delete;

	struct _THREAD_CONTROL_BLOCK		*pt_next_thread;			/* next thread point */

	PKSTART_ROUTINE						start_routine;				/* program entry point */
	PVOID								start_context;				/* context to be passed into the entry routine */
	int									*pt_stack_base_address;		/* stack base address */
	DWORD								stack_size;					/* stack size */
	TSS_32								thread_tss32;				/* TSS32 BLOCK */
} THREAD_CONTROL_BLOCK, *PTHREAD_CONTROL_BLOCK;

typedef struct _PROCESS_CONTROL_BLOCK {
	DWORD								process_id;					/* process id */
	HANDLE								process_handle;				/* memory address */

	struct _PROCESS_CONTROL_BLOCK		*pt_next_process;			/* next process point used by RR-Scheduler */

	DWORD								thread_count;				/* number of threads */
    DWORD								next_thread_id;				/* next thread id used in this process */
	struct _THREAD_CONTROL_BLOCK		*pt_head_thread;			/* first thread point */
} PROCESS_CONTROL_BLOCK, *PPROCESS_CONTROL_BLOCK;


//Process 리스트를 관리하는 구조체
typedef struct _PROCESS_MANAGER_BLOCK {
	DWORD								process_count;				/* number of processes */
	DWORD								next_process_id;			/* next prodess id */
	struct _THREAD_CONTROL_BLOCK		*pt_current_thread;			/* running thread */
	struct _PROCESS_CONTROL_BLOCK		*pt_head_process;			/* first process point */
} PROCESS_MANAGER_BLOCK, *PPROCESS_MANAGER_BLOCK;


//시스템에서 제거해야하는 Process와 Thread의 리스트에 대한 정보를 관리하는 구조체
typedef struct _CUTTING_LIST {

	BYTE								count;
	BYTE								head;
	BYTE								tail;
	HANDLE								handle_list[MAX_CUTTING_ITEM];
} CUTTING_LIST, *PCUTTING_LIST;

 
static DWORD  PspGetNextProcessID(void);
static BOOL   PspAddNewProcess(HANDLE ProcessHandle);
static DWORD  PspGetNextThreadID(HANDLE ProcessHandle);
static BOOL   PspAddNewThread(HANDLE ProcessHandle, HANDLE ThreadHandle);

static BOOL PspPopCuttingItem(CUTTING_LIST *pCuttingList, HANDLE *pItem);
static BOOL PspPushCuttingItem(CUTTING_LIST *pCuttingList, HANDLE Item);


extern BOOL HalSetupTSS(TSS_32 *pTss32, BOOL IsKernelTSS, int EntryPoint, int *pStackBase, DWORD StackSize);
extern BOOL HalWriteTssIntoGdt(TSS_32 *pTss32, DWORD TssSize, DWORD TssNumber, BOOL SetBusy);

extern BOOL HalSetupTaskLink(TSS_32 *pTss32, WORD TaskLink);


static PROCESS_MANAGER_BLOCK m_ProcMgrBlk;
static CUTTING_LIST m_ProcessCuttingList;
static CUTTING_LIST m_ThreadCuttingList;

static BOOL m_bShowTSWatchdogClock; //바람개비 모양
static DWORD m_TickCount;	//시스템의 틱 값을 저장하는 변수

KERNELAPI BOOL PsShowTSWachdogClock(BOOL Show)
{
	m_bShowTSWatchdogClock = Show;
	if(!Show)
		*((BYTE *)TS_WATCHDOG_CLOCK_POS) = ' ';

	return TRUE;
}

BOOL PskInitializeProcessManager(VOID)
{
	//모든 변수 초기화
	m_ProcMgrBlk.process_count		= 0;
	m_ProcMgrBlk.next_process_id	= 0;
	m_ProcMgrBlk.pt_current_thread	= 0;
	m_ProcMgrBlk.pt_head_process	= NULL;

	m_ProcessCuttingList.count		= 0;
	m_ProcessCuttingList.head		= 0;
	m_ProcessCuttingList.tail		= 0;

	m_ThreadCuttingList.count		= 0;
	m_ThreadCuttingList.head		= 0;
	m_ThreadCuttingList.tail		= 0;

	m_bShowTSWatchdogClock			= TRUE;
	m_TickCount						= 0;

	if(!PspCreateSystemProcess()) {
		DbgPrint("PspCreateSystemProcess() returned an error.\r\n");
		return FALSE;
	}

	return TRUE;
}
//Init 쓰레드의 핸들러 함수
static void PspTaskEntryPoint(void)
{
	PKSTART_ROUTINE start_routine;
	HANDLE current_thread;
	DWORD ret_value;

	current_thread = PsGetCurrentThread();
	start_routine = PsGetThreadPtr(current_thread)->start_routine;
	ret_value = start_routine(PsGetThreadPtr(current_thread)->start_context);

	PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_TERMINATED;
	HalTaskSwitch();

	while(1) ;
}

//프로세스 생성 함수 (PID할당 등 관리)
KERNELAPI BOOL PsCreateProcess(OUT PHANDLE ProcessHandle)
{
	PPROCESS_CONTROL_BLOCK pProcess;

	pProcess = MmAllocateNonCachedMemory(sizeof(PROCESS_CONTROL_BLOCK));
	if(pProcess == NULL) return FALSE;

	pProcess->process_id		= PspGetNextProcessID();
	pProcess->process_handle	= (HANDLE)pProcess;
	pProcess->pt_next_process	= NULL;
	pProcess->thread_count		= 0;
	pProcess->next_thread_id	= 0;
	pProcess->pt_head_thread	= NULL;
	if(!PspAddNewProcess((HANDLE)pProcess)) return FALSE;

	*ProcessHandle = pProcess;

	return TRUE;
}


//쓰레드 생성 함수
KERNELAPI BOOL PsCreateThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete)
{
	PTHREAD_CONTROL_BLOCK pThread;
	int *pStack;
	
	//메모리할당
	pThread = MmAllocateNonCachedMemory(sizeof(THREAD_CONTROL_BLOCK));
	if(pThread == NULL) return FALSE;
	//쓰레드에서 사용할 스택 할당
	pStack  = MmAllocateNonCachedMemory(StackSize);
	if(pStack == NULL) return FALSE;

	//부모 프로세스의 핸들 설정
	pThread->parent_process_handle		= ProcessHandle;
	//Thread id 및 handle 할당
	pThread->thread_id					= PspGetNextThreadID(ProcessHandle);
	pThread->thread_handle				= (HANDLE)pThread;
	pThread->thread_status				= THREAD_STATUS_STOP; //Thread 상태를 STOP으로 설정
	pThread->auto_delete				= AutoDelete; 
	pThread->pt_next_thread				= NULL;
	//쓰레드가 실행해야 하는 함수(StartRoutine), 함수에 넘어가는 인자(StartContext), 스택 사이즈 설정
	pThread->start_routine				= StartRoutine;
	pThread->start_context				= StartContext;
	pThread->pt_stack_base_address		= pStack;
	pThread->stack_size					= StackSize;
	//PspAddNewThread 함수를 통해 Process에 생성된 쓰레드를 추가
	if(!PspAddNewThread(ProcessHandle, (HANDLE)pThread)) return FALSE;

	HalSetupTSS(&pThread->thread_tss32, TRUE, (int)PspTaskEntryPoint, pStack, StackSize);

	*ThreadHandle = pThread;

	return TRUE;
}

KERNELAPI BOOL PsCreateIntThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize)
{
	PTHREAD_CONTROL_BLOCK pThread;
	int *pStack;

	pThread = MmAllocateNonCachedMemory(sizeof(THREAD_CONTROL_BLOCK));
	if(pThread == NULL) return FALSE;
	pStack  = MmAllocateNonCachedMemory(StackSize);
	if(pStack == NULL) return FALSE;

	pThread->parent_process_handle		= ProcessHandle;
	pThread->thread_id					= PspGetNextThreadID(ProcessHandle);
	pThread->thread_handle				= (HANDLE)pThread;
	pThread->thread_status				= THREAD_STATUS_STOP;
	//PsCreateThread함수와 다르게 auto_delete가 false
	pThread->auto_delete				= FALSE;
	pThread->pt_next_thread				= NULL;
	//PsCreateThread함수와 다르게 argument의 StartRoutine을 바로 할당
	pThread->start_routine				= StartRoutine;
	pThread->start_context				= StartContext;
	pThread->pt_stack_base_address		= pStack;
	pThread->stack_size					= StackSize;
	if(!PspAddNewThread(ProcessHandle, (HANDLE)pThread)) return FALSE;

	HalSetupTSS(&pThread->thread_tss32, TRUE, (int)StartRoutine, pStack, StackSize);

	*ThreadHandle = pThread;

	return TRUE;
}
//쓰레드의 상태를 설정하는 함수
KERNELAPI BOOL PsSetThreadStatus(HANDLE ThreadHandle, THREAD_STATUS Status)
{
	PsGetThreadPtr(ThreadHandle)->thread_status = Status;

	return TRUE;
}

//현재 프로세스에서 실행되고 있는 쓰레드의 TCB를 반환하는 함수
KERNELAPI HANDLE PsGetCurrentThread(VOID)
{
	HANDLE thread;

ENTER_CRITICAL_SECTION();
	thread = (HANDLE)(m_ProcMgrBlk.pt_current_thread);
EXIT_CRITICAL_SECTION();

	return thread;
}

//더 이상 필요하지 않은 쓰레드를 삭제하기위한 함수
KERNELAPI BOOL PsDeleteThread(HANDLE ThreadHandle)
{
	return PspPushCuttingItem(&m_ThreadCuttingList, ThreadHandle);
}

//
static BOOL PspPopCuttingItem(CUTTING_LIST *pCuttingList, HANDLE *pItem)
{
	BOOL bResult = TRUE;

ENTER_CRITICAL_SECTION();
	{
		if(pCuttingList->count == 0) {
			bResult = FALSE;
			goto $exit;
		}

		pCuttingList->count--;
		*pItem = pCuttingList->handle_list[pCuttingList->head++];
		if(pCuttingList->head >= MAX_CUTTING_ITEM)
			pCuttingList->head = 0;
	}
$exit:
EXIT_CRITICAL_SECTION();
	return bResult;
}

static BOOL PspPushCuttingItem(CUTTING_LIST *pCuttingList, HANDLE Item)
{
	BOOL bResult = TRUE;

ENTER_CRITICAL_SECTION();
	{
		if(pCuttingList->count >= MAX_CUTTING_ITEM) {
			bResult = FALSE;
			goto $exit;
		}

		pCuttingList->count++;
		pCuttingList->handle_list[pCuttingList->tail++] = Item;
		if(pCuttingList->tail >= MAX_CUTTING_ITEM)
			pCuttingList->tail = 0;
	}
$exit:
EXIT_CRITICAL_SECTION();
	return bResult;
}


//Process ID 생성
static DWORD PspGetNextProcessID(void)
{
	DWORD process_id;

ENTER_CRITICAL_SECTION();
	process_id = m_ProcMgrBlk.next_process_id++;
EXIT_CRITICAL_SECTION();

	return process_id;
}

//Thread ID 생성
static DWORD PspGetNextThreadID(HANDLE ProcessHandle)
{
	DWORD thread_id;

ENTER_CRITICAL_SECTION();
	thread_id = PsGetProcessPtr(ProcessHandle)->next_thread_id++;
EXIT_CRITICAL_SECTION();

	return thread_id;
}

//새로운 프로세스를 넣을 공간을 찾아서 생성
static BOOL PspAddNewProcess(HANDLE ProcessHandle)
{
	PPROCESS_CONTROL_BLOCK *pt_next_process;

ENTER_CRITICAL_SECTION();
	pt_next_process = &m_ProcMgrBlk.pt_head_process;
	while(*pt_next_process)
		pt_next_process = &(*pt_next_process)->pt_next_process;
	*pt_next_process = PsGetProcessPtr(ProcessHandle);
	m_ProcMgrBlk.process_count++;
EXIT_CRITICAL_SECTION();

	return TRUE;
}

//새로운 쓰레드를 넣을 공간을 찾아서 생성
static BOOL PspAddNewThread(HANDLE ProcessHandle, HANDLE ThreadHandle)
{
	PTHREAD_CONTROL_BLOCK *pt_next_thread;

ENTER_CRITICAL_SECTION();
	pt_next_thread = &PsGetProcessPtr(ProcessHandle)->pt_head_thread;
	while(*pt_next_thread)
		pt_next_thread = &(*pt_next_thread)->pt_next_thread;
	*pt_next_thread = PsGetThreadPtr(ThreadHandle);
	PsGetProcessPtr(ProcessHandle)->thread_count++;
EXIT_CRITICAL_SECTION();

	return TRUE;
}
//다음 실행 가능한 쓰레드를 찾기 위한 함수
static HANDLE PspFindNextThreadScheduled(void)
{
	PTHREAD_CONTROL_BLOCK	pt_thread;
	PPROCESS_CONTROL_BLOCK	pt_process;

	if(m_ProcMgrBlk.process_count == 0 || m_ProcMgrBlk.pt_current_thread == NULL || m_ProcMgrBlk.pt_head_process == NULL) {
		return NULL;
	}
	pt_thread = m_ProcMgrBlk.pt_current_thread;

$find_thread:
		if(pt_thread->pt_next_thread != NULL) {
		pt_thread = pt_thread->pt_next_thread;
	} else {
		while(1) {
			pt_process = PsGetProcessPtr(pt_thread->parent_process_handle)->pt_next_process;
$find_process:
			if(pt_process == NULL)
				pt_process = m_ProcMgrBlk.pt_head_process;
			if(pt_process->pt_head_thread == NULL) {
				pt_process = pt_process->pt_next_process;
				goto $find_process;
			} else {
				pt_thread = pt_process->pt_head_thread;
				break;
			}
		}
	}	
		if(pt_thread->thread_status != THREAD_STATUS_READY && pt_thread->thread_status != THREAD_STATUS_RUNNING)
			goto $find_thread;
		m_ProcMgrBlk.pt_current_thread = pt_thread;

		return (HANDLE)pt_thread;
}

//태스크 스위칭 방법 중 하나
static void PspSetupTaskSWEnv(void)
{
	HANDLE current_thread, next_thread;

	current_thread = PsGetCurrentThread();

	next_thread = PspFindNextThreadScheduled();

	if(PsGetThreadPtr(current_thread)->thread_status == THREAD_STATUS_TERMINATED) {
		if(PsGetThreadPtr(current_thread)->auto_delete) {
			PsDeleteThread(current_thread);
		}
	} else if(PsGetThreadPtr(current_thread)->thread_status == THREAD_STATUS_RUNNING) {
		PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_READY;
	}

	if(current_thread != next_thread && next_thread != NULL) {
		HalWriteTssIntoGdt(&PsGetThreadPtr(next_thread)->thread_tss32, sizeof(TSS_32), TASK_SW_SEG, TRUE);
		PsGetThreadPtr(next_thread)->thread_status = THREAD_STATUS_RUNNING;
	}
}


//IDLE 쓰레드의 핸들러 함수
//계속해서 태스크 스위칭(HalTaskSwitch)을 시도
static DWORD PspIdleThread(PVOID StartContext)
{
	while(1) {
		HalTaskSwitch();
	}
	return 0;
}

//종료된 프로세스의 삭제
static DWORD PspProcessCutterThread(PVOID StartContext)
{
	HANDLE ProcessHandle;
	PPROCESS_CONTROL_BLOCK	*pt_prev_process, *pt_cur_process;
	PTHREAD_CONTROL_BLOCK	*pt_cur_thread;

	while(1) {
		if(!PspPopCuttingItem(&m_ProcessCuttingList, &ProcessHandle)) {
			HalTaskSwitch();
			continue;
		}
ENTER_CRITICAL_SECTION();
		if(ProcessHandle == PsGetThreadPtr(PsGetCurrentThread())->parent_process_handle) {
			goto $exit;
		}

		pt_prev_process = pt_cur_process = &(m_ProcMgrBlk.pt_head_process);
		while(*pt_cur_process != PsGetProcessPtr(ProcessHandle)) {
			if((*pt_cur_process)->pt_next_process == NULL) {
				goto $exit;
			}
			pt_prev_process = pt_cur_process;
			pt_cur_process = &((*pt_cur_process)->pt_next_process);
		}

		(*pt_prev_process)->pt_next_process = (*pt_cur_process)->pt_next_process;
		m_ProcMgrBlk.process_count--;

		pt_cur_thread = &(PsGetProcessPtr(ProcessHandle)->pt_head_thread);
		while(*pt_cur_thread != NULL) {
			MmFreeNonCachedMemory((PVOID)((*pt_cur_thread)->pt_stack_base_address));
			MmFreeNonCachedMemory((PVOID)(*pt_cur_thread));
			pt_cur_thread = &((*pt_cur_thread)->pt_next_thread);
		}

		MmFreeNonCachedMemory((PVOID)ProcessHandle);

$exit:
EXIT_CRITICAL_SECTION();
	}

	return 0;
}

//종료된 쓰레드의 삭제
static DWORD PspThreadCutterThread(PVOID StartContext)
{
	HANDLE ProcessHandle, ThreadHandle;
	PTHREAD_CONTROL_BLOCK *pt_prev_thread, *pt_cur_thread;

	while(1) {
		if(!PspPopCuttingItem(&m_ThreadCuttingList, &ThreadHandle)) {
			HalTaskSwitch();
			continue;
		}
ENTER_CRITICAL_SECTION();
		ProcessHandle = PsGetThreadPtr(ThreadHandle)->parent_process_handle;

		if(ProcessHandle == PsGetThreadPtr(PsGetCurrentThread())->parent_process_handle) {
			goto $exit;
		}

		if(PsGetProcessPtr(ProcessHandle)->thread_count == 0) {
			goto $exit;
		}

		else if(PsGetProcessPtr(ProcessHandle)->thread_count == 1) {
			PsGetProcessPtr(ProcessHandle)->pt_head_thread = NULL;
		}

		else {
			pt_prev_thread = pt_cur_thread = &(PsGetProcessPtr(ProcessHandle)->pt_head_thread);
			while(*pt_cur_thread != PsGetThreadPtr(ThreadHandle)) {
				if((*pt_cur_thread)->pt_next_thread == NULL) {
					goto $exit;
				}
				pt_prev_thread = pt_cur_thread;
				pt_cur_thread = &((*pt_cur_thread)->pt_next_thread);
			}
			(*pt_prev_thread)->pt_next_thread = (*pt_cur_thread)->pt_next_thread;
		}
		PsGetProcessPtr(ProcessHandle)->thread_count--;

		if(PsGetThreadPtr(ThreadHandle)->pt_stack_base_address >= (int *)0x00200000)
			MmFreeNonCachedMemory((PVOID)(PsGetThreadPtr(ThreadHandle)->pt_stack_base_address));

		MmFreeNonCachedMemory((PVOID)(PsGetThreadPtr(ThreadHandle)));

$exit:
EXIT_CRITICAL_SECTION();
	}
}

//소프트웨어 Interrupt 쓰레드의 핸들러 함수
static DWORD PspSoftTaskSW(PVOID StartContext)
{
	int cnt = 0, pos = 0;
	char *addr = (char *)TS_WATCHDOG_CLOCK_POS, status[] = {'-', '\\', '|', '/', '-', '\\', '|', '/'};

	while(1) {
		_asm cli

		if(cnt++ >= TIMEOUT_PER_SECOND) {

			if(++pos > 7) pos = 0;
			cnt = 0;
			if(m_bShowTSWatchdogClock)
				*addr = status[pos];
		}

		PspSetupTaskSWEnv();

		_asm iretd
	}

	return 0;
}


//타이머 인터럽트 핸들러 함수
static DWORD Psp_IRQ_SystemTimer(PVOID StartContext)
{
	while(1) {
		_asm cli

		m_TickCount++;
		PspSetupTaskSWEnv();
		WRITE_PORT_UCHAR((PUCHAR)0x20, 0x20);

		_asm iretd
	}

	return 0;
}

//초기 프로세스와 쓰레드의 생성과 설정
static BOOL PspCreateSystemProcess(void)
{
	HANDLE process_handle;
	HANDLE init_thread_handle, idle_thread_handle, process_cutter_handle, thread_cutter_handle;
	HANDLE tmr_thread_handle, sw_task_sw_handle;

	//메인 프로세스를 생성해주는 PSCreateProcess 함수 호출
	if(!PsCreateProcess(&process_handle)) 
		return FALSE;

	//프로세스를 생성하기 위해 베이스가 될 메인 쓰레드(init 쓰레드) 생성 
	if(!PsCreateThread(&init_thread_handle, process_handle, NULL, NULL, DEFAULT_STACK_SIZE, FALSE)) 
		return FALSE;

	//초기 쓰레드의 백링크(Prev-Link) 설정
	HalSetupTaskLink(&PsGetThreadPtr(init_thread_handle)->thread_tss32, TASK_SW_SEG);
	//초기 쓰레드의 TSS를 GDT내에 설정
	HalWriteTssIntoGdt(&PsGetThreadPtr(init_thread_handle)->thread_tss32, sizeof(TSS_32), INIT_TSS_SEG, FALSE);
	_asm {
		push	ax
		mov		ax, INIT_TSS_SEG
		ltr		ax
		pop		ax
	}

	if(!PsCreateIntThread(&tmr_thread_handle, process_handle, Psp_IRQ_SystemTimer, NULL, DEFAULT_STACK_SIZE))
		return FALSE;
	HalWriteTssIntoGdt(&PsGetThreadPtr(tmr_thread_handle)->thread_tss32, sizeof(TSS_32), TMR_TSS_SEG, FALSE);

	if(!PsCreateIntThread(&sw_task_sw_handle, process_handle, PspSoftTaskSW, NULL, DEFAULT_STACK_SIZE))
		return FALSE;
	HalWriteTssIntoGdt(&PsGetThreadPtr(sw_task_sw_handle)->thread_tss32, sizeof(TSS_32), SOFT_TS_TSS_SEG, FALSE);

	if(!PsCreateThread(&idle_thread_handle, process_handle, PspIdleThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(idle_thread_handle, THREAD_STATUS_RUNNING);

	HalWriteTssIntoGdt(&PsGetThreadPtr(idle_thread_handle)->thread_tss32, sizeof(TSS_32), TASK_SW_SEG,
		TRUE);
	m_ProcMgrBlk.pt_current_thread = idle_thread_handle;

	if(!PsCreateThread(&process_cutter_handle, process_handle, PspProcessCutterThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(process_cutter_handle, THREAD_STATUS_READY);
	if(!PsCreateThread(&thread_cutter_handle, process_handle, PspThreadCutterThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(thread_cutter_handle, THREAD_STATUS_READY);
		
	return TRUE;
}

//유제 쓰레드 생성하는 함수
KERNELAPI BOOL PsCreateUserThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PVOID StartContext)
{
#define USER_APP_ENTRY_POINT		0x00101000 //유저 프로그램의 진입점의 주소
#define USER_APP_STACK_PTR			0x001f0000 //유저 프로그램의 스택 주소
#define USER_APP_STACK_SIZE			(1024*64)  //유저 프로그램의 스택의 크디
	PTHREAD_CONTROL_BLOCK pThread;

	pThread = MmAllocateNonCachedMemory(sizeof(THREAD_CONTROL_BLOCK));
	if(pThread == NULL) return FALSE;

	pThread->parent_process_handle		= ProcessHandle;
	pThread->thread_id					= PspGetNextThreadID(ProcessHandle);
	pThread->thread_handle				= (HANDLE)pThread;
	pThread->thread_status				= THREAD_STATUS_STOP;
	pThread->auto_delete				= FALSE;
	pThread->pt_next_thread				= NULL;
	pThread->start_routine				= (PKSTART_ROUTINE)USER_APP_ENTRY_POINT;
	pThread->start_context				= StartContext;
	pThread->pt_stack_base_address		= (int *)USER_APP_STACK_PTR;
	pThread->stack_size					= USER_APP_STACK_SIZE;
	if(!PspAddNewThread(ProcessHandle, (HANDLE)pThread)) return FALSE;

	HalSetupTSS(&pThread->thread_tss32, TRUE, USER_APP_ENTRY_POINT, (int *)USER_APP_STACK_PTR, USER_APP_STACK_SIZE);

	*ThreadHandle = pThread;

	return TRUE;
}

//쓰레드 핸들을 가지고 쓰레드의 상태를 얻는 함수
KERNELAPI THREAD_STATUS PsGetThreadStatus(IN HANDLE ThreadHandle)
{
	return (THREAD_STATUS)(PsGetThreadPtr(ThreadHandle)->thread_status);
}

//쓰레드 핸들을 가지고 부모 프로세스의 핸들을 얻는 함수
KERNELAPI HANDLE PsGetParentProcess(IN HANDLE ThreadHandle)
{
	return (HANDLE)(PsGetThreadPtr(ThreadHandle)->parent_process_handle);
}



// 과제2를 위한 코드 추가
KERNELAPI VOID ShellCommand_ps(VOID) {
	const PROCESS_MANAGER_BLOCK *process_mgr;
	const PROCESS_CONTROL_BLOCK *now_process;
	const THREAD_CONTROL_BLOCK *now_thread;

	char *CASE;

	// 현재 돌고있는 프로세스 리스트를 가져온다.
	process_mgr = &m_ProcMgrBlk;

	now_process = process_mgr->pt_head_process;
	
	CrtPrintf("\tPID\t\tTID\t\tCMD   \r\n");  // 명령어 실행창 첫줄
	// 프로세스를 탐색하는 반복문 내에 쓰레드를 탐색하는 반복문을 넣어 각 프로세스마다 보유하고 있는 쓰레드들을 출력한다.
	while(now_process) {
		// 프로세스 ID별 프로세스 이름을 설정해준다.
		switch(now_process->process_id) {
		case 0:
			CASE = "Process Scheduler";
			break;
		case 1:
			CASE = "Keyboard Driver";
			break;
		case 2:
			CASE = "FDD Driver";
			break;
		case 3:
			CASE = "Shell";
			break;
		default:
			CASE = "User Process";
			break;
		}

		// 해당 프로세스의 내용 출력
		CrtPrintf("\t %d \t\t   \t\t%s   \r\n", now_process->process_id, CASE);

		// 현재 프로세스의 첫번째 쓰레드를 가져온다.
		now_thread = now_process->pt_head_thread;

		// now_thread가 없는 경우까지 반복(즉 모든 thread 탐색)
		while(now_thread) {
			// 해당 쓰레드의 ID를 출력한다.
			CrtPrintf("\t   \t\t %d \t\t%s   \r\n", now_thread->thread_id, CASE);
			// 다음 쓰레드를 가져온다.
			now_thread = now_thread->pt_next_thread;
		}

		// 다음 프로세스
		now_process = now_process->pt_next_process;
	}
	return TRUE;
}
// !!!!!!!!