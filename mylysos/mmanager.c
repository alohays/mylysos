#include "mylysos.h"

#define MEMORY_POOL_START_ADDRESS	0x00200000

#define MEM_BLK_SIZE				512
#define MEM_BLK_FREE				0
#define MEM_BLK_USED				1

typedef struct _MEM_BLK_DESC {
	DWORD					status;
	DWORD					block_size;
	struct _MEM_BLK_DESC	*pNext;
} MEM_BLK_DESC, *PMEM_BLK_DESC;

typedef struct _MEM_BLK_MANAGER {
	DWORD					nBlocks;
	DWORD					nUsedBlocks;
	DWORD					nFreeBlocks;
	MEM_BLK_DESC			*pDescEntry;
	int						*pPoolEntry;
} MEM_BLK_MANAGER, *PMEM_BLK_MANAGER;

static DWORD				m_MemSize;

static MEM_BLK_MANAGER		m_MemBlkManager;

static BOOL MmpCheckMemorySize(void);
static BOOL MmpCreateMemPoolBlk(void);
static DWORD MmpGetRequiredBlocksFromBytes(DWORD bytes);
static VOID *MmpGetPoolAddrFromDescAddr(MEM_BLK_DESC *pDescAddr);
static MEM_BLK_DESC *MmpGetDescAddrFromPoolAddr(VOID *pPoolAddr);

KERNELAPI VOID *MmAllocateNonCachedMemory(IN ULONG NumberOfBytes);
KERNELAPI VOID MmFreeNonCachedMemory(IN PVOID BaseAddress);

BOOL MmkInitializeMemoryManager(VOID)
{
	if(!MmpCheckMemorySize()) {
		DbgPrint("MmpCheckMemorySize() returned an error.\r\n");
		return FALSE;
	}

	if(!MmpCreateMemPoolBlk()) {
		DbgPrint("MmpCreateMemPookBlk() returned an error.\r\n");
		return FALSE;
	}
	DbgPrint("Memory Manager is initialized!!\r\n");

	return TRUE;
}

static BOOL MmpCheckMemorySize(void)
{
	BOOL bResult;
	DWORD *pAddr = (DWORD *)0x00000000, tmp;

	while(1) {
		pAddr += (4*1024*1024);
		tmp = *pAddr;
		*pAddr = 0x11223344;
		if(*pAddr != 0x11223344)
			break;

		*pAddr = tmp;
	}

ENTER_CRITICAL_SECTION();
	m_MemSize = (DWORD)pAddr;
	bResult = (m_MemSize < MEMORY_POOL_START_ADDRESS+(1*1024*1024) ? FALSE : TRUE);
EXIT_CRITICAL_SECTION();

	return bResult;
}

static BOOL MmpCreateMemPoolBlk(void)
{
	DWORD dwUsableMemSize;
	DWORD dwBlksOfUsableMem;
	DWORD dwBlksOfAllocatableMem;
	DWORD dwBlksOfDescs;
	DWORD i;

	int *pPoolEntry;
	MEM_BLK_DESC *pPrev, *pCur;

ENTER_CRITICAL_SECTION();
	dwUsableMemSize = m_MemSize - MEMORY_POOL_START_ADDRESS;
EXIT_CRITICAL_SECTION();

	dwBlksOfUsableMem		= MmpGetRequiredBlocksFromBytes(dwUsableMemSize);
	dwBlksOfDescs			= MmpGetRequiredBlocksFromBytes(dwBlksOfUsableMem*sizeof(MEM_BLK_DESC));
	dwBlksOfAllocatableMem	= dwBlksOfUsableMem-dwBlksOfDescs;
	dwBlksOfDescs			= MmpGetRequiredBlocksFromBytes(dwBlksOfAllocatableMem*sizeof(MEM_BLK_DESC));
	pPoolEntry				= (int *)(MEMORY_POOL_START_ADDRESS+dwBlksOfDescs*MEM_BLK_SIZE);

ENTER_CRITICAL_SECTION();
	m_MemBlkManager.nBlocks			= dwBlksOfAllocatableMem;
	m_MemBlkManager.nUsedBlocks		= 0;
	m_MemBlkManager.nFreeBlocks		= dwBlksOfAllocatableMem;
	m_MemBlkManager.pDescEntry		= (MEM_BLK_DESC *)MEMORY_POOL_START_ADDRESS;
	m_MemBlkManager.pPoolEntry		= pPoolEntry;

	pPrev = m_MemBlkManager.pDescEntry;
	pPrev->status = MEM_BLK_FREE;

	for(i=1; i<dwBlksOfAllocatableMem; i++)
	{
		pCur = (MEM_BLK_DESC *)(MEMORY_POOL_START_ADDRESS+sizeof(MEM_BLK_DESC)*i);
		pCur->status = MEM_BLK_FREE;
		pCur->block_size = 0;
		pPrev->pNext = pCur;
		pPrev = pCur;
	}
	pCur->pNext = NULL;
EXIT_CRITICAL_SECTION();

	return TRUE;
}

static DWORD MmpGetRequiredBlocksFromBytes(DWORD bytes)
{
	DWORD dwBlocks = 0;

	dwBlocks = bytes/MEM_BLK_SIZE;
	if(bytes % MEM_BLK_SIZE)
		dwBlocks++;

	return dwBlocks;
}

static VOID *MmpGetPoolAddrFromDescAddr(MEM_BLK_DESC *pDescAddr)
{
	int ResultAddr;

ENTER_CRITICAL_SECTION();
	ResultAddr	= (int)(m_MemBlkManager.pPoolEntry);
	ResultAddr += (int)((pDescAddr-m_MemBlkManager.pDescEntry)*MEM_BLK_SIZE);
EXIT_CRITICAL_SECTION();

	return (VOID *)ResultAddr;
}

static MEM_BLK_DESC *MmpGetDescAddrFromPoolAddr(VOID *pPoolAddr)
{
	int ResultAddr;

ENTER_CRITICAL_SECTION();
	ResultAddr	= (int)pPoolAddr-(int)(m_MemBlkManager.pPoolEntry);
	ResultAddr	= ResultAddr/MEM_BLK_SIZE*sizeof(MEM_BLK_DESC);
	ResultAddr += (int)(m_MemBlkManager.pDescEntry);
EXIT_CRITICAL_SECTION();

	return (MEM_BLK_DESC *)ResultAddr;
}

KERNELAPI VOID *MmAllocateNonCachedMemory(IN ULONG NumberOfBytes)
{
	DWORD dwRequiredBlocks, dwCurBlocks, i;
	MEM_BLK_DESC *pAllocStart, *pCur;

	if(NumberOfBytes == 0)
		return (VOID *)NULL;

	dwRequiredBlocks = MmpGetRequiredBlocksFromBytes(NumberOfBytes);
ENTER_CRITICAL_SECTION();
	if(m_MemBlkManager.nFreeBlocks < dwRequiredBlocks) {
		EXIT_CRITICAL_SECTION();
		return (VOID *)NULL;
	}

	pAllocStart = pCur = m_MemBlkManager.pDescEntry;
	dwCurBlocks = 0;
	while(dwCurBlocks < dwRequiredBlocks) {
		if(pCur->pNext == NULL) {
			EXIT_CRITICAL_SECTION();
			return (VOID *)NULL;
		}

		if(pCur->status == MEM_BLK_USED) {
			dwCurBlocks = 0;
			pAllocStart = pCur = pCur+pCur->block_size;
			continue;
		}

		dwCurBlocks++;
		pCur = pCur->pNext;
	}
	pCur = pAllocStart;
	for(i=0; i<dwRequiredBlocks; i++) {
		pCur->status = MEM_BLK_USED;
		pCur = pCur->pNext;
	}
	pAllocStart->block_size = dwRequiredBlocks;

	m_MemBlkManager.nFreeBlocks -= dwRequiredBlocks;
	m_MemBlkManager.nUsedBlocks += dwRequiredBlocks;

	pAllocStart = (MEM_BLK_DESC *)MmpGetPoolAddrFromDescAddr(pAllocStart);
EXIT_CRITICAL_SECTION();

	return (VOID *)pAllocStart;
}

KERNELAPI VOID MmFreeNonCachedMemory(IN PVOID BaseAddress)
{
	MEM_BLK_DESC *pCur;
	DWORD i, dwBlockSize;

	if(BaseAddress == NULL)
		return;

	pCur = (MEM_BLK_DESC *)MmpGetDescAddrFromPoolAddr(BaseAddress);
ENTER_CRITICAL_SECTION();
	if(pCur->status != MEM_BLK_USED) {
		EXIT_CRITICAL_SECTION();
		return;
	}

	dwBlockSize=pCur->block_size;
	for(i=0; i<dwBlockSize; i++) {
		pCur->status = MEM_BLK_FREE;
		pCur->block_size = 0;
		pCur = pCur->pNext;
	}

	m_MemBlkManager.nFreeBlocks += dwBlockSize;
	m_MemBlkManager.nUsedBlocks -= dwBlockSize;
EXIT_CRITICAL_SECTION();
}

// 과제2를 위한 코드 추가
KERNELAPI DWORD GetTotalMemoryBlocks(VOID) {
	return m_MemSize;
}
KERNELAPI DWORD GetUsedMemoryBlocks(VOID) {
	DWORD usedb;
ENTER_CRITICAL_SECTION();
	usedb = m_MemBlkManager.nUsedBlocks;
EXIT_CRITICAL_SECTION();  // 메모리 블럭 매니저 구조체에서 사용된 메모리 블럭 수를 가져온다(크리티컬 섹션 이용)

	return usedb;
}
KERNELAPI DWORD GetMemoryBlockSize(VOID) {
	return MEM_BLK_SIZE;
	// 한 메모리 블럭 당 할당하고 있는 메모리 크기를 반환한다.
}
// !!!!!!!!