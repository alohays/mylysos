#ifndef	_FDD_DRIVER_HEADER_FILE_
#define	_FDD_DRIVER_HEADER_FILE_


#include "mylysos.h"

KERNELAPI BOOL FddReadSector (WORD SectorNumber, BYTE NumbersOfSectors, BYTE *pData);

#endif