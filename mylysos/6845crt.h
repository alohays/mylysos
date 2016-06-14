#ifndef _6845_CRT_DRIVER_HEADER_FILE_
#define _6845_CRT_DRIVER_HEADER_FILE_

#include "mylysos.h"

KERNELAPI VOID CrtClearScreen(VOID);
KERNELAPI VOID CrtGetCursorPos(BYTE *pX, BYTE *pY);

KERNELAPI BOOL CrtPrintText(LPCSTR pText);

KERNELAPI BOOL CrtPrintTextXYWithAttr(LPCSTR pText, WORD x, WORD y, UCHAR Attr);

KERNELAPI int CrtPrintf(const char *fmt, ...);

#endif
