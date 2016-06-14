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

KERNELAPI BOOL PsShowTSWachdogClock(BOOL Show)
{
	m_bShowTSWatchdogClock = Show;
	if(!Show)
		*((BYTE *)TS_WATCHDOG_CLOCK_POS) = ' ';

	return TRUE;
}

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

	current_thread = PsGetCurrentThread();
	start_routine = PsGetThreadPtr(current_thread)->start_routine;
	ret_value = start_routine(PsGetThreadPtr(current_thread)->start_context);

	PsGetThreadPtr(current_thread)->thread_status = THREAD_STATUS_TERMINATED;
	HalTaskSwitch();

	while(1) ;
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
KERNELAPI BOOL PsCreateThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PKSTART_ROUTINE StartRoutine, IN PVOID StartContext, IN DWORD StackSize, IN BOOL AutoDelete)
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
	//PspAddNewThread �Լ��� ���� Process�� ������ �����带 �߰�
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

//�½�ũ ����Ī ��� �� �ϳ�
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


//IDLE �������� �ڵ鷯 �Լ�
//����ؼ� �½�ũ ����Ī(HalTaskSwitch)�� �õ�
static DWORD PspIdleThread(PVOID StartContext)
{
	while(1) {
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

//����� �������� ����
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

//����Ʈ���� Interrupt �������� �ڵ鷯 �Լ�
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


//Ÿ�̸� ���ͷ�Ʈ �ڵ鷯 �Լ�
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

//���� ������ �����ϴ� �Լ�
KERNELAPI BOOL PsCreateUserThread(OUT PHANDLE ThreadHandle, IN HANDLE ProcessHandle, IN PVOID StartContext)
{
#define USER_APP_ENTRY_POINT		0x00101000 //���� ���α׷��� �������� �ּ�
#define USER_APP_STACK_PTR			0x001f0000 //���� ���α׷��� ���� �ּ�
#define USER_APP_STACK_SIZE			(1024*64)  //���� ���α׷��� ������ ũ��
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

//������ �ڵ��� ������ �������� ���¸� ��� �Լ�
KERNELAPI THREAD_STATUS PsGetThreadStatus(IN HANDLE ThreadHandle)
{
	return (THREAD_STATUS)(PsGetThreadPtr(ThreadHandle)->thread_status);
}

//������ �ڵ��� ������ �θ� ���μ����� �ڵ��� ��� �Լ�
KERNELAPI HANDLE PsGetParentProcess(IN HANDLE ThreadHandle)
{
	return (HANDLE)(PsGetThreadPtr(ThreadHandle)->parent_process_handle);
}



// ����2�� ���� �ڵ� �߰�
KERNELAPI VOID ShellCommand_ps(VOID) {
	const PROCESS_MANAGER_BLOCK *process_mgr;
	const PROCESS_CONTROL_BLOCK *now_process;
	const THREAD_CONTROL_BLOCK *now_thread;

	char *CASE;

	// ���� �����ִ� ���μ��� ����Ʈ�� �����´�.
	process_mgr = &m_ProcMgrBlk;

	now_process = process_mgr->pt_head_process;
	
	CrtPrintf("\tPID\t\tTID\t\tCMD   \r\n");  // ��ɾ� ����â ù��
	// ���μ����� Ž���ϴ� �ݺ��� ���� �����带 Ž���ϴ� �ݺ����� �־� �� ���μ������� �����ϰ� �ִ� ��������� ����Ѵ�.
	while(now_process) {
		// ���μ��� ID�� ���μ��� �̸��� �������ش�.
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

		// �ش� ���μ����� ���� ���
		CrtPrintf("\t %d \t\t   \t\t%s   \r\n", now_process->process_id, CASE);

		// ���� ���μ����� ù��° �����带 �����´�.
		now_thread = now_process->pt_head_thread;

		// now_thread�� ���� ������ �ݺ�(�� ��� thread Ž��)
		while(now_thread) {
			// �ش� �������� ID�� ����Ѵ�.
			CrtPrintf("\t   \t\t %d \t\t%s   \r\n", now_thread->thread_id, CASE);
			// ���� �����带 �����´�.
			now_thread = now_thread->pt_next_thread;
		}

		// ���� ���μ���
		now_process = now_process->pt_next_process;
	}
	return TRUE;
}
// !!!!!!!!