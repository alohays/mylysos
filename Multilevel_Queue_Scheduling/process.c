#include "myoksos.h"
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


	SCHEDULE_PRIORITY					priority;					// 해당 쓰레드의 우선순위를 저장

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


typedef struct _PRIORITY_QUEUE_DATA {
	HANDLE								thread;
	struct _PRIORITY_QUEUE_DATA			*next;
} PRIORITY_QUEUE_DATA;

// 멀티레벨 큐 스케줄링을 위한 큐 구조체 생성

typedef struct _PRIORITY_QUEUE {
	int count;  // queue 안에 있는 data들의 수
	PRIORITY_QUEUE_DATA *front;  // queue 데이터 중 가장 앞
	PRIORITY_QUEUE_DATA *end;  // queue 데이터 중 가장 뒤
} PRIORITY_QUEUE;

// !!!!!!!!

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


static SCHEDULE_PRIORITY current_schedule_priority;
static DWORD highT, normalT, lowT;  // 각 우선순위 별로 time을 얼마나 차지했는지 저장
static PRIORITY_QUEUE highQ, normalQ, lowQ;   // high, normal, low의 우선순위 큐들
static BOOL PspPushScheduleData(PRIORITY_QUEUE *pQueue, HANDLE data);
static BOOL PspPopScheduleData(PRIORITY_QUEUE *pQueue, HANDLE *pdata);
// !!!!!!!!




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

	
	current_schedule_priority		= PRIORITY_HIGH;
	
	highQ.count = 0;
	highQ.end = NULL;
	highQ.front = NULL;

	normalQ.count = 0;
	normalQ.end = NULL;
	normalQ.front = NULL;

	lowQ.count = 0;
	lowQ.end = NULL;
	lowQ.front = NULL;

	// !!!!!!!!

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
	//현재 실행되는 thread의 출cb가지고옴
	current_thread = PsGetCurrentThread();
	//tcb의 start_routine 콜백함수에 start_context 포인터를 넘겨서 콜백 함수를 호출
	start_routine = PsGetThreadPtr(current_thread)->start_routine;
	ret_value = start_routine(PsGetThreadPtr(current_thread)->start_context);
	
	//쓰레드의 상태를 THREAD_STATUS_TERMINATED로 설정
	PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_TERMINATED;
	//태스크 스위칭 함수를 호출
	HalTaskSwitch();

	while(1);
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
KERNELAPI BOOL PsCreateThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, 
					 IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete)
{
	SCHEDULE_PRIORITY Priority;

	if(ProcessHandle == m_ProcMgrBlk.pt_head_process) Priority = PRIORITY_HIGH;
	else Priority = PRIORITY_NORMAL;

	return PsCreateThreadPriority(ThreadHandle, ProcessHandle, StartRoutine, StartContext, StackSize, AutoDelete, Priority);
}

KERNELAPI BOOL		PsCreateThreadPriority(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete, SCHEDULE_PRIORITY Priority)
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

	pThread->priority					= Priority;


	//PspAddNewThread 함수를 통해 Process에 생성된 쓰레드를 추가
	if(!PspAddNewThread(ProcessHandle, (HANDLE)pThread)) return FALSE;

	if(pThread->priority == PRIORITY_HIGH){
		PspPushScheduleData(&highQ, (HANDLE)pThread);
	}else if(pThread->priority == PRIORITY_NORMAL)
		PspPushScheduleData(&normalQ, (HANDLE)pThread);
	else PspPushScheduleData(&lowQ, (HANDLE)pThread);

	HalSetupTSS(&pThread->thread_tss32, TRUE, (int)PspTaskEntryPoint, pStack, StackSize);

	*ThreadHandle = pThread;

	return TRUE;
}

KERNELAPI BOOL PsCreateIntThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine,
					   IN PVOID StartContext, IN DWORD StackSize)
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
	//m_ProcMgrBlk의 pt_current_thread를 반환
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
		// 커터 큐에 카운터 체크
		if(pCuttingList->count == 0){
			bResult = FALSE;
			goto $exit;
		}

		//POP
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
		// 커터큐에 남은 공간 체크
		if(pCuttingList->count == MAX_CUTTING_ITEM){
			bResult = FALSE;
			goto $exit;
		}

		//POP
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

	PRIORITY_QUEUE *pQueue;

	HANDLE handle;

	if(m_ProcMgrBlk.process_count == 0 || m_ProcMgrBlk.pt_current_thread == NULL || m_ProcMgrBlk.pt_head_process == NULL){
		return NULL;
	}

	pt_thread = m_ProcMgrBlk.pt_current_thread;

	if(current_schedule_priority == PRIORITY_HIGH)
		pQueue = &highQ;
	else if(current_schedule_priority == PRIORITY_NORMAL)
		pQueue = &normalQ;
	else
		pQueue = &lowQ;
	
	while(pQueue->front != NULL && PsGetThreadPtr(pQueue->front->thread)->thread_status != THREAD_STATUS_READY &&
		PsGetThreadPtr(pQueue->front->thread)->thread_status != THREAD_STATUS_RUNNING)
		PspPopScheduleData(pQueue, &handle);

	if(pQueue->front != NULL){
		PspPopScheduleData(pQueue, &handle);
		pt_thread = PsGetThreadPtr(handle);
	} else if(pt_thread->thread_status == THREAD_STATUS_TERMINATED) // 큐가 비어있는 상태에 실행중이던 쓰레드도 종료된 상태
		pt_thread = NULL;

	if(pt_thread == NULL) 
		pt_thread = m_ProcMgrBlk.pt_current_thread;
$find_thread:
	if(pt_thread->pt_next_thread != NULL){
		pt_thread = pt_thread->pt_next_thread;
	} else{
		while(1){
			pt_process = PsGetProcessPtr(pt_thread->parent_process_handle)->pt_next_process;
$find_process:
			if(pt_process == NULL)
				pt_process = m_ProcMgrBlk.pt_head_process;
			if(pt_process->pt_head_thread == NULL){
				pt_process = pt_process->pt_next_process;
				goto $find_process;
			} else{
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
	// 현재 실행되는 쓰레드를 가져옴
	current_thread = PsGetCurrentThread();
	// 다음 실행가능한 쓰레드를 찾는다
	next_thread = PspFindNextThreadScheduled();

	// 현재 쓰레드의 상태를 확인하여 종료상태일 경우 프로세스 제거 
	if(PsGetThreadPtr(current_thread)->thread_status == THREAD_STATUS_TERMINATED){
		if(PsGetThreadPtr(current_thread)->auto_delete){
			PsDeleteThread(current_thread);
		}
	} // 실행중인 상태일 경우, 다음 스케줄링 시 재개될 수 있도록 실행대기 상태로 변경 - 다시 실행될 수 있으므로 해당 priority queue에 넣어준다
	else if(PsGetThreadPtr(current_thread)->thread_status == THREAD_STATUS_RUNNING){
		PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_READY;
		if(PsGetThreadPtr(current_thread)->priority == PRIORITY_HIGH)
			PspPushScheduleData(&highQ, current_thread);
		else if(PsGetThreadPtr(current_thread)->priority == PRIORITY_NORMAL)
			PspPushScheduleData(&normalQ, current_thread);
		else
			PspPushScheduleData(&lowQ, current_thread);
	}

	// 태스크 스위칭
	if(current_thread != next_thread && next_thread != NULL){
		HalWriteTssIntoGdt(&PsGetThreadPtr(next_thread)->thread_tss32, sizeof(TSS_32), TASK_SW_SEG, TRUE);
		PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_RUNNING;
	}
}


//IDLE 쓰레드의 핸들러 함수
//계속해서 태스크 스위칭(HalTaskSwitch)을 시도
static DWORD PspIdleThread(PVOID StartContext)
{
	while(1){
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

	while(1){
		if(!PspPopCuttingItem(&m_ProcessCuttingList, &ProcessHandle)){
			HalTaskSwitch();
			continue;
		}
ENTER_CRITICAL_SECTION();
	//삭제할 프로세스가 시스템프로세스인지 확인
	if(ProcessHandle == PsGetThreadPtr(PsGetCurrentThread())->parent_process_handle){
		goto $exit;
	}
	

	pt_prev_process = pt_cur_process = &(m_ProcMgrBlk.pt_head_process);
	while(*pt_cur_process != PsGetProcessPtr(ProcessHandle)){
		//리스트 내에서 현재 프로세스가 마지막 프로세스일 경우 종료
		if((*pt_cur_process)->pt_next_process == NULL){
			goto $exit;
		}
		pt_prev_process = pt_cur_process;
		pt_cur_process = &((*pt_cur_process)->pt_next_process);
	}
	// 다음 프로세스를 받아온다.
	(*pt_prev_process)->pt_next_process = (*pt_cur_process)->pt_next_process;
	m_ProcMgrBlk.process_count--;

	// 삭제할 프로세스를 찾았다면 해당 프로세스 내에 모든 쓰레드에 할당된 메모리 해제
	pt_cur_thread = &(PsGetProcessPtr(ProcessHandle)->pt_head_thread);
	while(*pt_cur_thread != NULL){
		MmFreeNonCachedMemory((PVOID)((*pt_cur_thread)->pt_stack_base_address));
		MmFreeNonCachedMemory((PVOID)(*pt_cur_thread));
		pt_cur_thread = &((*pt_cur_thread)->pt_next_thread);
	}
	// 삭제할 프로세스 자체 메모리도 해제
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

	while(1){
		//thread의 cutting list 확인
		if(!PspPopCuttingItem(&m_ThreadCuttingList, &ThreadHandle)){
			HalTaskSwitch();
			continue;
		}
ENTER_CRITICAL_SECTION();
	ProcessHandle = PsGetThreadPtr(ThreadHandle)->parent_process_handle;
	//삭제할 쓰레드가 속해있는 프로세스가 시스템 프로세스인지 확인
	if(ProcessHandle == PsGetThreadPtr(PsGetCurrentThread())->parent_process_handle){
		goto $exit;
	}
	//TCB에서 쓰레드 수가 없을 경우 
	if(PsGetProcessPtr(ProcessHandle)->thread_count == 0){
		goto $exit;
	}
	// 속해있는 프로세스 내에서 한개의 쓰레드만 존재
	else if (PsGetProcessPtr(ProcessHandle)->thread_count == 1){
		PsGetProcessPtr(ProcessHandle)->pt_head_thread = NULL;
	}
	// 두개의 쓰레드 존재
	else{
		pt_prev_thread = pt_cur_thread = &(PsGetProcessPtr(ProcessHandle)->pt_head_thread);
		while(*pt_cur_thread != PsGetThreadPtr(ThreadHandle)){
			if((*pt_cur_thread)->pt_next_thread == NULL){
				goto $exit;
			}
			pt_prev_thread = pt_cur_thread;
			pt_cur_thread = &((*pt_cur_thread)->pt_next_thread);
		}
		//다음 쓰레드 포인트 가져온다
		(*pt_prev_thread)->pt_next_thread = (*pt_cur_thread)->pt_next_thread;
	}
	PsGetProcessPtr(ProcessHandle)->thread_count--;

	if(PsGetThreadPtr(ThreadHandle)->pt_stack_base_address >= (int *)0x00200000)
		// 스택영역 할당 해제
		MmFreeNonCachedMemory((PVOID)(PsGetThreadPtr(ThreadHandle)->pt_stack_base_address));
	//쓰레드 자체 메모리 해제
	MmFreeNonCachedMemory((PVOID)(PsGetThreadPtr(ThreadHandle)));

$exit:
EXIT_CRITICAL_SECTION();
	}

	return 0;
}

//소프트웨어 Interrupt 쓰레드의 핸들러 함수
static DWORD PspSoftTaskSW(PVOID StartContext)
{
	int cnt=0, pos=0;
	char *addr=(char *)TS_WATCHDOG_CLOCK_POS, status[] = {'-', '\\', '|', '/', '-', '\\', '|', '/'}; 

	while(1){
		//인터럽 불가
		_asm cli
		//실행화면에 바람개비 표시
		if(cnt++ >= TIMEOUT_PER_SECOND){
			if(++pos > 7) pos = 0;
			cnt = 0;
			if(m_bShowTSWatchdogClock)
				*addr = status[pos];
		}
		// 태스크 스위칭하는 함수 호출
		PspSetupTaskSWEnv();
		// 인터럽 처리 시에 모든 처리를 완료하고 다시 태스크로 복귀
		_asm iretd
	}

	return 0;
}

//타이머 인터럽트 핸들러 함수
static DWORD Psp_IRQ_SystemTimer(PVOID StartContext)
{
	while(1){
		_asm cli
		
		// 10번의 타임 인터럽트 중 6번은 high, 3번은 normal, 1번은 low로 수행할 수 있도록 한다.
		// 순서는 high normal low 순으로 큐에서 꺼내간다.
		// 우선순위가 높은 큐들부터 확인을 하며 목표했던 횟수보다 적은 수를 수행 했으면 그 순위를 수행해준다.
		// high에서 3번을 수행한 뒤 큐가 비어있는 상태에서 normal을 2번 수행하다가 high가 다시 들어오면 high를 다시 수행해준다는 것이다.
		m_TickCount++; // tickcount값 1씩 증가

		if(highQ.count > 0 && highT < 6){
			highT++;
			current_schedule_priority = PRIORITY_HIGH;
		} else if (normalQ.count > 0 && normalT < 3){
			normalT++;
			current_schedule_priority = PRIORITY_NORMAL;
		} else if (lowQ.count > 0 && lowT<1){
			lowT++;
			current_schedule_priority = PRIORITY_LOW;
		} else{
			// 모두 배정받은 CPU 타임을 수행한 경우로 다시 초기화 해준다.
			highT = 0;
			normalT = 0;
			lowT = 0;

			if(highQ.count != 0)
				current_schedule_priority = PRIORITY_HIGH;
			else if(normalQ.count != 0)
				current_schedule_priority = PRIORITY_NORMAL;
			else
				current_schedule_priority = PRIORITY_LOW;
		}
		// !!!!!!!!
		PspSetupTaskSWEnv(); //태스크 스위칭
		WRITE_PORT_UCHAR((PUCHAR)0x20, 0x20); //EOI신호를 전송

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


	//Interuupt thread 수행 시 타이머 처리를 위한 쓰레드 생성 
	if(!PsCreateIntThread(&tmr_thread_handle, process_handle, Psp_IRQ_SystemTimer, NULL, DEFAULT_STACK_SIZE))
		return FALSE;
	HalWriteTssIntoGdt(&PsGetThreadPtr(tmr_thread_handle)->thread_tss32, sizeof(TSS_32), TMR_TSS_SEG, FALSE);
	//소프트웨어 interrupt를 위한 interrupt thread 생성
	if(!PsCreateIntThread(&sw_task_sw_handle, process_handle, PspSoftTaskSW, NULL, DEFAULT_STACK_SIZE))
		return FALSE;
	HalWriteTssIntoGdt(&PsGetThreadPtr(sw_task_sw_handle)->thread_tss32, sizeof(TSS_32), SOFT_TS_TSS_SEG, FALSE);
	
	//idle thread 생성
	if(!PsCreateThread(&idle_thread_handle, process_handle, PspIdleThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(idle_thread_handle, THREAD_STATUS_RUNNING);
	HalWriteTssIntoGdt(&PsGetThreadPtr(idle_thread_handle)->thread_tss32, sizeof(TSS_32), TASK_SW_SEG, TRUE);
	m_ProcMgrBlk.pt_current_thread = idle_thread_handle;
	//종료된 프로세스와 스레드를 삭제하는 cutter쓰레드
	if(!PsCreateThread(&process_cutter_handle, process_handle, PspProcessCutterThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(process_cutter_handle, THREAD_STATUS_READY);
	if(!PsCreateThread(&thread_cutter_handle, process_handle, PspThreadCutterThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(thread_cutter_handle, THREAD_STATUS_READY);

	return TRUE;
}


static BOOL PspPushScheduleData(PRIORITY_QUEUE *pQueue, HANDLE data){
	BOOL bResult = TRUE;
	PRIORITY_QUEUE_DATA *scheduleData;
ENTER_CRITICAL_SECTION();
	{
		scheduleData = MmAllocateNonCachedMemory(sizeof(PRIORITY_QUEUE_DATA));
		scheduleData->next = NULL;
		scheduleData->thread = data;

		if(pQueue->front == NULL){
			pQueue->end = scheduleData;
			pQueue->front = scheduleData;
		} else{
			pQueue->end->next = scheduleData;
			pQueue->end = scheduleData;
		}

		pQueue->count++;
	}
$exit:
EXIT_CRITICAL_SECTION();
	
	return bResult;

}
static BOOL PspPopScheduleData(PRIORITY_QUEUE *pQueue, HANDLE *pdata){
	BOOL bResult = TRUE;
	PRIORITY_QUEUE_DATA *scheduleData;
ENTER_CRITICAL_SECTION();
	{
		if(pQueue->count == 0){
			bResult = FALSE;
			goto $exit;
		} // 큐가 비어있는지 체크, 비어있으면 팝 불가
		
		*pdata = pQueue->front->thread;

		scheduleData = pQueue->front->next;
		MmFreeNonCachedMemory((PVOID) pQueue->front);
		pQueue->front = scheduleData;
		pQueue->count--;
	}
$exit:
EXIT_CRITICAL_SECTION();
	return bResult;
}

// !!!!!!!!
