#include "fdddrv.h"

#define DEFAULT_STACK_SIZE				(64*1024) /* 64kbytes */

//�÷��� ��ũ���� ���Ǵ� ��Ʈ
#define FDD_DOR_PORT		0x3f2
#define FDD_STATUS_PORT		0x3f4
#define FDD_CMD_PORT		0x3f4
#define FDD_DATA_PORT		0x3f5

typedef enum _FDD_JOB_TYPE {
	FDD_READ_SECTOR,
	FDD_WRITE_SECTOR,
} FDD_JOB_TYPE;

//�÷��� ��ũ�� �۾��� �����ϱ� ���� ť ����ü
typedef struct _FDD_JOB_ITEM {
	FDD_JOB_TYPE			type;
	WORD					sector;
	BYTE					numbers_of_sectors;
	BYTE					*pt_data;

	HANDLE					thread;
} FDD_JOB_ITEM, *PFDD_JOB_ITEM;

#define FDD_JOB_ITEM_Q_SIZE		32

//ť �����ϴ� ����ü
typedef struct _FDD_JOB_ITEM_Q {
	BYTE				cnt;

	BYTE				head;
	BYTE				tail;
	FDD_JOB_ITEM		queue[FDD_JOB_ITEM_Q_SIZE];
} FDD_JOB_ITEM_Q, *PFDD_JOB_ITEM_Q;


static BOOL  FddpPopJobItem(FDD_JOB_ITEM *pJobItem);
static BOOL  FddpPushJobItem(FDD_JOB_ITEM *pJobItem);
static DWORD FddpJobProcessThread(PVOID StartContext);

static BOOL FddpReadWriteSector(FDD_JOB_TYPE JobType, WORD Sector, BYTE NumbersOfSectors, BYTE *pData);

static BOOL FddpTurnOnMotor(void);
static BOOL FddpTurnOffMotor(void);
static BOOL FddpSetupDMA(FDD_JOB_TYPE JobType);
static BOOL FddpWriteFdcData(BYTE Data);
static BOOL FddpReadFdcData(BYTE *pData);



static FDD_JOB_ITEM_Q m_JobItemQ;
static HANDLE m_ProcessHandle, m_ThreadHandle;

static BOOL m_FddInterruptOccurred;

//�÷��� ��ũ ����̽� ����̹� �ʱ�ȭ �Լ�
BOOL FddInitializeDriver(VOID)
{
	if(!FddpTurnOffMotor()){
		DbgPrint("FddTurnOffMotor() returned an error.\r\n");
		return FALSE;
	}

	if(!PsCreateProcess(&m_ProcessHandle))
		return FALSE;

	if(!PsCreateThread(&m_ThreadHandle, m_ProcessHandle, FddpJobProcessThread, NULL, DEFAULT_STACK_SIZE, FALSE))
		return FALSE;
	PsSetThreadStatus(m_ThreadHandle, THREAD_STATUS_READY);

	return TRUE;
}

//�÷��� ��ũ ���ͷ�Ʈ�� �߻������� �˸��� ������ ����
VOID Fdd_IRQ_Handler(VOID)
{
	m_FddInterruptOccurred=TRUE;
}


#define BYTES_PER_SECTOR			512
#define SECTORS_PER_TRACK			18

//�÷��� ��ũ�� ���� ON
static BOOL FddpTurnOnMotor(void)
{
	WRITE_PORT_UCHAR((PUCHAR)FDD_DOR_PORT, 0x1c);
	return TRUE;
}

//�÷��� ��ũ�� ���� OFF
static BOOL FddpTurnOffMotor(void)
{
	WRITE_PORT_UCHAR((PUCHAR)FDD_DOR_PORT, 0x00);
	return TRUE;
}


//�÷��� ��ũ ��Ʈ�ѷ����� �����͸� ���� �Լ�
static BOOL FddpWriteFdcData(BYTE Data)
{
	UCHAR status;

	do{
		status = READ_PORT_UCHAR((PUCHAR)FDD_STATUS_PORT);
	}while( (status & 0xc0) != 0x80 );
	WRITE_PORT_UCHAR((PUCHAR)FDD_DATA_PORT, Data);

	return TRUE;
}
//�÷��� ��ũ ��Ʈ�ѷ����� �����͸� �д� �Լ�
static BOOL FddpReadFdcData(BYTE *pData)
{
	UCHAR status;

	do{
		status = READ_PORT_UCHAR((PUCHAR)FDD_STATUS_PORT);
	}while( !(status & 0x80) );
	*pData = READ_PORT_UCAHR((PUCHAR)FDD_DATA_PORT);

	return TRUE;
}

//������ �б� ����� �����ϱ� ���� DMA�� �����ϴ� �Լ�
static BOOL FddpSetupDMA(FDD_JOB_TYPE JobType)
{
	WORD count = BYTES_PER_SECTOR*SECTORS_PER_TRACK-1;

	if(JobType == FDD_READ_SECTOR){
		WRITE_PORT_UCHAR((PUCHAR)0x0b, 0x46);
	}else{
		WRITE_PORT_UCHAR((PUCHAR)0x0b,0x4a);
	}

	WRITE_PORT_UCHAR((PUCHAR)0x04, 0x00);
	WRITE_PORT_UCHAR((PUCHAR)0x04, 0x00);
	WRITE_PORT_UCHAR((PUCHAR)0x81, 0x00);

	WRITE_PORT_UCHAR((PUCHAR)0x05, (BYTE)count);
	WRITE_PORT_UCHAR((PUCHAR)0x05, (BYTE)(count >> 8));

	WRITE_PORT_UCHAR((PUCHAR)0x0a, 0x02);

	return TRUE;
}

//�÷��� ��ũ�� ���������� ����ϸ� �б�/���⸦ ó���ϴ� �Լ�
static BOOL FddpReadWriteSector(FDD_JOB_TYPE JobType, WORD Sector, BYTE NumbersOfSectors, BYTE *pData)
{
	BYTE *pDMAAddr = (BYTE *)0x00000000;
	BYTE drive, head, track, sector;
	int i;

	if(JobType == FDD_WRITE_SECTOR)
		return FALSE;

	drive	= 0;
	head	= ((Sector % (SECTORS_PER_TRACK * 2)) / SECTORS_PER_TRACK);
	track	= (Sector / (SECTORS_PER_TRACK * 2));
	sector	= (Sector % SECTORS_PER_TRACK) + 1;

	m_FddInterruptOccurred = FALSE;{
		FddpTurnOnMotor();
	}while(!m_FddInterruptOccurred);

	m_FddInterruptOccurred = FALSE;{
		FddpWriteFdcData(0x07);
		FddpWriteFdcData(0x00);
	}while(!m_FddInterruptOccurred);

	m_FddInterruptOccurred = FALSE;{
		FddpWriteFdcData(0x0f);
		FddpWriteFdcData((head << 2) + drive);
		FddpWriteFdcData(track);
	}while(!m_FddInterruptOccurred);

	FddpSetupDMA(JobType);

	m_FddInterruptOccurred = FALSE;{
		FddpWriteFdcData(0xe6);
		FddpWriteFdcData((head << 2) + drive);
		FddpWriteFdcData(track);
		FddpWriteFdcData(head);
		FddpWriteFdcData(1);
		FddpWriteFdcData(2);
		FddpWriteFdcData(SECTORS_PER_TRACK);
		FddpWriteFdcData(27);
		FddpWriteFdcData(0xff);
	}while(!m_FddInterruptOccurred);

	pDMAAddr += (BYTES_PER_SECTOR*(sector-1));
	if(JobType == FDD_READ_SECTOR){
		for(i=0; i<(BYTES_PER_SECTOR*NumbersOfSectors); i++){
			*(pData+i) = *(pDMAAddr+i);
		}
	}

	FddpTurnOffMotor();

	return TRUE;

}

//�÷��� ��ũ ����̹��� ó���� ����ϴ� �Լ�
static DWORD FddpJobProcessThread(PVOID StartContext)
{
	FDD_JOB_ITEM job_item;

	while(1){
		if(!FddpPopJobItem(&job_item)){
			HalTaskSwitch();
			continue;
		}

		FddpReadWriteSector(job_item.type, job_item.sector, job_item.numbers_of_sectors, job_item.pt_data);
		PsSetThreadStatus(job_item.thread, THREAD_STATUS_READY);
	}

	return 0;
}

//�۾� ť���� �÷��� ��ũ�� �۾��� �޾ƿ��� �Լ�
static BOOL FddpPopJobItem(FDD_JOB_ITEM *pJobItem)
{
	BOOL bResult = TRUE;

ENTER_CRITICAL_SECTION();
	{
		if(m_JobItemQ.cnt == 0){
			bResult = FALSE;
			goto $exit;
		}

		m_JobItemQ.cnt--;
		pJobItem->type					= m_JobItemQ.queue[m_JobItemQ.head].type;
		pJobItem->sector				= m_JobItemQ.queue[m_JobItemQ.head].sector;
		pJobItem->numbers_of_sectors	= m_JobItemQ.queue[m_JobItemQ.head].numbers_of_sectors;
		pJobItem->pt_data				= m_JobItemQ.queue[m_JobItemQ.head].pt_data;
		pJobItem->thread				= m_JobItemQ.queue[m_JobItemQ.head].thread;
		m_JobItemQ.head++;
		if(m_JobItemQ.head >= FDD_JOB_ITEM_Q_SIZE)
			m_JobItemQ.head = 0;
	}
$exit:
EXIT_CRITICAL_SECTION();
	return bResult;
}

//�۾� ť���� �÷��� ��ũ�� �۾��� �����ϴ� �Լ�
static BOOL FddpPushJobItem(FDD_JOB_ITEM *pJobItem)
{
	BOOL bResult = TRUE;

ENTER_CRITICAL_SECTION();
	{
		if(m_JobItemQ.cnt >= FDD_JOB_ITEM_Q_SIZE){
			bResult = FALSE;
			goto $exit;
		}

		m_JobItemQ.cnt++;
		m_JobItemQ.queue[m_JobItemQ.tail].type					= pJobItem->type;
		m_JobItemQ.queue[m_JobItemQ.tail].sector				= pJobItem->sector;
		m_JobItemQ.queue[m_JobItemQ.tail].numbers_of_sectors	= pJobItem->numbers_of_sectors;
		m_JobItemQ.queue[m_JobItemQ.tail].pt_data				= pJobItem->pt_data;
		m_JobItemQ.queue[m_JobItemQ.tail].thread				= pJobItem->thread;
		m_JobItemQ.tail++;
		if(m_JobItemQ.tail >= FDD_JOB_ITEM_Q_SIZE)
			m_JobItemQ.tail = 0;
	}
$exit:
EXIT_CRITICAL_SECTION();
	return bResult;
}