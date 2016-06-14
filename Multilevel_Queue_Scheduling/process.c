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


	SCHEDULE_PRIORITY					priority;					// �ش� �������� �켱������ ����

} THREAD_CONTROL_BLOCK, *PTHREAD_CONTROL_BLOCK;

typedef struct _PROCESS_CONTROL_BLOCK {
	DWORD								process_id;					/* process id */
	HANDLE								process_handle;				/* memory address */

	struct _PROCESS_CONTROL_BLOCK		*pt_next_process;			/* next process point used by RR-Scheduler */

	DWORD								thread_count;				/* number of threads */
    DWORD								next_thread_id;				/* next thread id used in this process */
	struct _THREAD_CONTROL_BLOCK		*pt_head_thread;			/* first thread point */
} PROCESS_CONTROL_BLOCK, *PPROCESS_CONTROL_BLOCK;


//Process ����Ʈ�� �����ϴ� ����ü
typedef struct _PROCESS_MANAGER_BLOCK {
	DWORD								process_count;				/* number of processes */
	DWORD								next_process_id;			/* next prodess id */
	struct _THREAD_CONTROL_BLOCK		*pt_current_thread;			/* running thread */
	struct _PROCESS_CONTROL_BLOCK		*pt_head_process;			/* first process point */
} PROCESS_MANAGER_BLOCK, *PPROCESS_MANAGER_BLOCK;


//�ý��ۿ��� �����ؾ��ϴ� Process�� Thread�� ����Ʈ�� ���� ������ �����ϴ� ����ü
typedef struct _CUTTING_LIST {

	BYTE								count;
	BYTE								head;
	BYTE								tail;
	HANDLE								handle_list[MAX_CUTTING_ITEM];

} CUTTING_LIST, *PCUTTING_LIST;

// ����1�� ���� �ڵ� �߰�!!!!!!!!
typedef struct _PRIORITY_QUEUE_DATA {
	HANDLE								thread;
	struct _PRIORITY_QUEUE_DATA			*next;
} PRIORITY_QUEUE_DATA;

// ��Ƽ���� ť �����ٸ��� ���� ť ����ü ����

typedef struct _PRIORITY_QUEUE {
	int count;  // queue �ȿ� �ִ� data���� ��
	PRIORITY_QUEUE_DATA *front;  // queue ������ �� ���� ��
	PRIORITY_QUEUE_DATA *end;  // queue ������ �� ���� ��
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

static BOOL m_bShowTSWatchdogClock; //�ٶ����� ���
static DWORD m_TickCount;	//�ý����� ƽ ���� �����ϴ� ����

// ����1�� ���� �ڵ� �߰�!!!!!!!!
static SCHEDULE_PRIORITY current_schedule_priority;
static DWORD highT, normalT, lowT;  // �� �켱���� ���� time�� �󸶳� �����ߴ��� ����
static PRIORITY_QUEUE highQ, normalQ, lowQ;   // high, normal, low�� �켱���� ť��
static BOOL PspPushScheduleData(PRIORITY_QUEUE *pQueue, HANDLE data);
static BOOL PspPopScheduleData(PRIORITY_QUEUE *pQueue, HANDLE *pdata);
// !!!!!!!!




BOOL PskInitializeProcessManager(VOID)
{
	//��� ���� �ʱ�ȭ
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

	// ����1�� ���� �ڵ� �߰�!!!!!!!!
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
//Init �������� �ڵ鷯 �Լ�
static void PspTaskEntryPoint(void)
{
	PKSTART_ROUTINE start_routine;
	HANDLE current_thread;
	DWORD ret_value;
	//���� ����Ǵ� thread�� ��cb�������
	current_thread = PsGetCurrentThread();
	//tcb�� start_routine �ݹ��Լ��� start_context �����͸� �Ѱܼ� �ݹ� �Լ��� ȣ��
	start_routine = PsGetThreadPtr(current_thread)->start_routine;
	ret_value = start_routine(PsGetThreadPtr(current_thread)->start_context);
	
	//�������� ���¸� THREAD_STATUS_TERMINATED�� ����
	PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_TERMINATED;
	//�½�ũ ����Ī �Լ��� ȣ��
	HalTaskSwitch();

	while(1);
}

//���μ��� ���� �Լ� (PID�Ҵ� �� ����)
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


//������ ���� �Լ�
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
	
	//�޸��Ҵ�
	pThread = MmAllocateNonCachedMemory(sizeof(THREAD_CONTROL_BLOCK));
	if(pThread == NULL) return FALSE;
	//�����忡�� ����� ���� �Ҵ�
	pStack  = MmAllocateNonCachedMemory(StackSize);
	if(pStack == NULL) return FALSE;

	//�θ� ���μ����� �ڵ� ����
	pThread->parent_process_handle		= ProcessHandle;
	//Thread id �� handle �Ҵ�
	pThread->thread_id					= PspGetNextThreadID(ProcessHandle);
	pThread->thread_handle				= (HANDLE)pThread;
	pThread->thread_status				= THREAD_STATUS_STOP; //Thread ���¸� STOP���� ����
	pThread->auto_delete				= AutoDelete; 
	pThread->pt_next_thread				= NULL;
	//�����尡 �����ؾ� �ϴ� �Լ�(StartRoutine), �Լ��� �Ѿ�� ����(StartContext), ���� ������ ����
	pThread->start_routine				= StartRoutine;
	pThread->start_context				= StartContext;
	pThread->pt_stack_base_address		= pStack;
	pThread->stack_size					= StackSize;

	pThread->priority					= Priority;


	//PspAddNewThread �Լ��� ���� Process�� ������ �����带 �߰�
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
	//PsCreateThread�Լ��� �ٸ��� auto_delete�� false
	pThread->auto_delete				= FALSE;
	pThread->pt_next_thread				= NULL;
	//PsCreateThread�Լ��� �ٸ��� argument�� StartRoutine�� �ٷ� �Ҵ�
	pThread->start_routine				= StartRoutine;
	pThread->start_context				= StartContext;
	pThread->pt_stack_base_address		= pStack;
	pThread->stack_size					= StackSize;
	if(!PspAddNewThread(ProcessHandle, (HANDLE)pThread)) return FALSE;

	HalSetupTSS(&pThread->thread_tss32, TRUE, (int)StartRoutine, pStack, StackSize);

	*ThreadHandle = pThread;

	return TRUE;
}
//�������� ���¸� �����ϴ� �Լ�
KERNELAPI BOOL PsSetThreadStatus(HANDLE ThreadHandle, THREAD_STATUS Status)
{
	PsGetThreadPtr(ThreadHandle)->thread_status = Status;

	return TRUE;
}

//���� ���μ������� ����ǰ� �ִ� �������� TCB�� ��ȯ�ϴ� �Լ�
KERNELAPI HANDLE PsGetCurrentThread(VOID)
{
	HANDLE thread;

ENTER_CRITICAL_SECTION();
	//m_ProcMgrBlk�� pt_current_thread�� ��ȯ
	thread = (HANDLE)(m_ProcMgrBlk.pt_current_thread);
EXIT_CRITICAL_SECTION();
	return thread;
}

//�� �̻� �ʿ����� ���� �����带 �����ϱ����� �Լ�
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
		// Ŀ�� ť�� ī���� üũ
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
		// Ŀ��ť�� ���� ���� üũ
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


//Process ID ����
static DWORD PspGetNextProcessID(void)
{
	DWORD process_id;

ENTER_CRITICAL_SECTION();
	process_id = m_ProcMgrBlk.next_process_id++;
EXIT_CRITICAL_SECTION();

	return process_id;
}

//Thread ID ����
static DWORD PspGetNextThreadID(HANDLE ProcessHandle)
{
	DWORD thread_id;

ENTER_CRITICAL_SECTION();
	thread_id = PsGetProcessPtr(ProcessHandle)->next_thread_id++;
EXIT_CRITICAL_SECTION();

	return thread_id;
}

//���ο� ���μ����� ���� ������ ã�Ƽ� ����
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

//���ο� �����带 ���� ������ ã�Ƽ� ����
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
//���� ���� ������ �����带 ã�� ���� �Լ�
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
	} else if(pt_thread->thread_status == THREAD_STATUS_TERMINATED) // ť�� ����ִ� ���¿� �������̴� �����嵵 ����� ����
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


//�½�ũ ����Ī ��� �� �ϳ�
static void PspSetupTaskSWEnv(void)
{
	HANDLE current_thread, next_thread;
	// ���� ����Ǵ� �����带 ������
	current_thread = PsGetCurrentThread();
	// ���� ���డ���� �����带 ã�´�
	next_thread = PspFindNextThreadScheduled();

	// ���� �������� ���¸� Ȯ���Ͽ� ��������� ��� ���μ��� ���� 
	if(PsGetThreadPtr(current_thread)->thread_status == THREAD_STATUS_TERMINATED){
		if(PsGetThreadPtr(current_thread)->auto_delete){
			PsDeleteThread(current_thread);
		}
	} // �������� ������ ���, ���� �����ٸ� �� �簳�� �� �ֵ��� ������ ���·� ���� - �ٽ� ����� �� �����Ƿ� �ش� priority queue�� �־��ش�
	else if(PsGetThreadPtr(current_thread)->thread_status == THREAD_STATUS_RUNNING){
		PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_READY;
		if(PsGetThreadPtr(current_thread)->priority == PRIORITY_HIGH)
			PspPushScheduleData(&highQ, current_thread);
		else if(PsGetThreadPtr(current_thread)->priority == PRIORITY_NORMAL)
			PspPushScheduleData(&normalQ, current_thread);
		else
			PspPushScheduleData(&lowQ, current_thread);
	}

	// �½�ũ ����Ī
	if(current_thread != next_thread && next_thread != NULL){
		HalWriteTssIntoGdt(&PsGetThreadPtr(next_thread)->thread_tss32, sizeof(TSS_32), TASK_SW_SEG, TRUE);
		PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_RUNNING;
	}
}


//IDLE �������� �ڵ鷯 �Լ�
//����ؼ� �½�ũ ����Ī(HalTaskSwitch)�� �õ�
static DWORD PspIdleThread(PVOID StartContext)
{
	while(1){
		HalTaskSwitch();
	}

	return 0;
}

//����� ���μ����� ����
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
	//������ ���μ����� �ý������μ������� Ȯ��
	if(ProcessHandle == PsGetThreadPtr(PsGetCurrentThread())->parent_process_handle){
		goto $exit;
	}
	

	pt_prev_process = pt_cur_process = &(m_ProcMgrBlk.pt_head_process);
	while(*pt_cur_process != PsGetProcessPtr(ProcessHandle)){
		//����Ʈ ������ ���� ���μ����� ������ ���μ����� ��� ����
		if((*pt_cur_process)->pt_next_process == NULL){
			goto $exit;
		}
		pt_prev_process = pt_cur_process;
		pt_cur_process = &((*pt_cur_process)->pt_next_process);
	}
	// ���� ���μ����� �޾ƿ´�.
	(*pt_prev_process)->pt_next_process = (*pt_cur_process)->pt_next_process;
	m_ProcMgrBlk.process_count--;

	// ������ ���μ����� ã�Ҵٸ� �ش� ���μ��� ���� ��� �����忡 �Ҵ�� �޸� ����
	pt_cur_thread = &(PsGetProcessPtr(ProcessHandle)->pt_head_thread);
	while(*pt_cur_thread != NULL){
		MmFreeNonCachedMemory((PVOID)((*pt_cur_thread)->pt_stack_base_address));
		MmFreeNonCachedMemory((PVOID)(*pt_cur_thread));
		pt_cur_thread = &((*pt_cur_thread)->pt_next_thread);
	}
	// ������ ���μ��� ��ü �޸𸮵� ����
	MmFreeNonCachedMemory((PVOID)ProcessHandle);

$exit:
EXIT_CRITICAL_SECTION();
	}

	return 0;
}


//����� �������� ����
static DWORD PspThreadCutterThread(PVOID StartContext)
{
	HANDLE ProcessHandle, ThreadHandle;
	PTHREAD_CONTROL_BLOCK *pt_prev_thread, *pt_cur_thread;

	while(1){
		//thread�� cutting list Ȯ��
		if(!PspPopCuttingItem(&m_ThreadCuttingList, &ThreadHandle)){
			HalTaskSwitch();
			continue;
		}
ENTER_CRITICAL_SECTION();
	ProcessHandle = PsGetThreadPtr(ThreadHandle)->parent_process_handle;
	//������ �����尡 �����ִ� ���μ����� �ý��� ���μ������� Ȯ��
	if(ProcessHandle == PsGetThreadPtr(PsGetCurrentThread())->parent_process_handle){
		goto $exit;
	}
	//TCB���� ������ ���� ���� ��� 
	if(PsGetProcessPtr(ProcessHandle)->thread_count == 0){
		goto $exit;
	}
	// �����ִ� ���μ��� ������ �Ѱ��� �����常 ����
	else if (PsGetProcessPtr(ProcessHandle)->thread_count == 1){
		PsGetProcessPtr(ProcessHandle)->pt_head_thread = NULL;
	}
	// �ΰ��� ������ ����
	else{
		pt_prev_thread = pt_cur_thread = &(PsGetProcessPtr(ProcessHandle)->pt_head_thread);
		while(*pt_cur_thread != PsGetThreadPtr(ThreadHandle)){
			if((*pt_cur_thread)->pt_next_thread == NULL){
				goto $exit;
			}
			pt_prev_thread = pt_cur_thread;
			pt_cur_thread = &((*pt_cur_thread)->pt_next_thread);
		}
		//���� ������ ����Ʈ �����´�
		(*pt_prev_thread)->pt_next_thread = (*pt_cur_thread)->pt_next_thread;
	}
	PsGetProcessPtr(ProcessHandle)->thread_count--;

	if(PsGetThreadPtr(ThreadHandle)->pt_stack_base_address >= (int *)0x00200000)
		// ���ÿ��� �Ҵ� ����
		MmFreeNonCachedMemory((PVOID)(PsGetThreadPtr(ThreadHandle)->pt_stack_base_address));
	//������ ��ü �޸� ����
	MmFreeNonCachedMemory((PVOID)(PsGetThreadPtr(ThreadHandle)));

$exit:
EXIT_CRITICAL_SECTION();
	}

	return 0;
}

//����Ʈ���� Interrupt �������� �ڵ鷯 �Լ�
static DWORD PspSoftTaskSW(PVOID StartContext)
{
	int cnt=0, pos=0;
	char *addr=(char *)TS_WATCHDOG_CLOCK_POS, status[] = {'-', '\\', '|', '/', '-', '\\', '|', '/'}; 

	while(1){
		//���ͷ� �Ұ�
		_asm cli
		//����ȭ�鿡 �ٶ����� ǥ��
		if(cnt++ >= TIMEOUT_PER_SECOND){
			if(++pos > 7) pos = 0;
			cnt = 0;
			if(m_bShowTSWatchdogClock)
				*addr = status[pos];
		}
		// �½�ũ ����Ī�ϴ� �Լ� ȣ��
		PspSetupTaskSWEnv();
		// ���ͷ� ó�� �ÿ� ��� ó���� �Ϸ��ϰ� �ٽ� �½�ũ�� ����
		_asm iretd
	}

	return 0;
}

//Ÿ�̸� ���ͷ�Ʈ �ڵ鷯 �Լ�
static DWORD Psp_IRQ_SystemTimer(PVOID StartContext)
{
	while(1){
		_asm cli
		// ����1�� ���� �ڵ� �߰�!!!!!!!!
		// 10���� Ÿ�� ���ͷ�Ʈ �� 6���� high, 3���� normal, 1���� low�� ������ �� �ֵ��� �Ѵ�.
		// ������ high normal low ������ ť���� ��������.
		// �켱������ ���� ť����� Ȯ���� �ϸ� ��ǥ�ߴ� Ƚ������ ���� ���� ���� ������ �� ������ �������ش�.
		// high���� 3���� ������ �� ť�� ����ִ� ���¿��� normal�� 2�� �����ϴٰ� high�� �ٽ� ������ high�� �ٽ� �������شٴ� ���̴�.
		m_TickCount++; // tickcount�� 1�� ����

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
			// ��� �������� CPU Ÿ���� ������ ���� �ٽ� �ʱ�ȭ ���ش�.
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
		PspSetupTaskSWEnv(); //�½�ũ ����Ī
		WRITE_PORT_UCHAR((PUCHAR)0x20, 0x20); //EOI��ȣ�� ����

		_asm iretd
	}

	return 0;
}

//�ʱ� ���μ����� �������� ������ ����
static BOOL PspCreateSystemProcess(void)
{
	HANDLE process_handle;
	HANDLE init_thread_handle, idle_thread_handle, process_cutter_handle, thread_cutter_handle;
	HANDLE tmr_thread_handle, sw_task_sw_handle;

	//���� ���μ����� �������ִ� PSCreateProcess �Լ� ȣ��
	if(!PsCreateProcess(&process_handle)) 
		return FALSE;

	//���μ����� �����ϱ� ���� ���̽��� �� ���� ������(init ������) ���� 
	if(!PsCreateThread(&init_thread_handle, process_handle, NULL, NULL, DEFAULT_STACK_SIZE, FALSE)) 
		return FALSE;

	//�ʱ� �������� �鸵ũ(Prev-Link) ����
	HalSetupTaskLink(&PsGetThreadPtr(init_thread_handle)->thread_tss32, TASK_SW_SEG);
	//�ʱ� �������� TSS�� GDT���� ����
	HalWriteTssIntoGdt(&PsGetThreadPtr(init_thread_handle)->thread_tss32, sizeof(TSS_32), INIT_TSS_SEG, FALSE);
	_asm {
		push	ax
		mov		ax, INIT_TSS_SEG
		ltr		ax
		pop		ax
	}


	//Interuupt thread ���� �� Ÿ�̸� ó���� ���� ������ ���� 
	if(!PsCreateIntThread(&tmr_thread_handle, process_handle, Psp_IRQ_SystemTimer, NULL, DEFAULT_STACK_SIZE))
		return FALSE;
	HalWriteTssIntoGdt(&PsGetThreadPtr(tmr_thread_handle)->thread_tss32, sizeof(TSS_32), TMR_TSS_SEG, FALSE);
	//����Ʈ���� interrupt�� ���� interrupt thread ����
	if(!PsCreateIntThread(&sw_task_sw_handle, process_handle, PspSoftTaskSW, NULL, DEFAULT_STACK_SIZE))
		return FALSE;
	HalWriteTssIntoGdt(&PsGetThreadPtr(sw_task_sw_handle)->thread_tss32, sizeof(TSS_32), SOFT_TS_TSS_SEG, FALSE);
	
	//idle thread ����
	if(!PsCreateThread(&idle_thread_handle, process_handle, PspIdleThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(idle_thread_handle, THREAD_STATUS_RUNNING);
	HalWriteTssIntoGdt(&PsGetThreadPtr(idle_thread_handle)->thread_tss32, sizeof(TSS_32), TASK_SW_SEG, TRUE);
	m_ProcMgrBlk.pt_current_thread = idle_thread_handle;
	//����� ���μ����� �����带 �����ϴ� cutter������
	if(!PsCreateThread(&process_cutter_handle, process_handle, PspProcessCutterThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(process_cutter_handle, THREAD_STATUS_READY);
	if(!PsCreateThread(&thread_cutter_handle, process_handle, PspThreadCutterThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(thread_cutter_handle, THREAD_STATUS_READY);

	return TRUE;
}



// ����1�� ���� �ڵ� �߰�!!!!!!!!
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
		} // ť�� ����ִ��� üũ, ��������� �� �Ұ�
		
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