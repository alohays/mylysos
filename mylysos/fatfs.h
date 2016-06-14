#ifndef _FATFS_HEADER_FILE_
#define _FATFS_HEADER_FILE_

#include "hshell.h"


#define FILE_ATTR_READ_ONLY				0x01
#define FILE_ATTR_HIDDEN				0x02
#define FILE_ATTR_SYSTEM				0x04
#define FILE_ATTR_VOLUME_ID				0x08
#define FILE_ATTR_DIRECTORY				0x10
#define FILE_ATTR_ARCHIVE				0x20
#define FILE_ATTR_LONG_NAME				(FILE_ATTR_READ_ONLY | FILE_ATTR_HIDDEN | FILE_ATTR_SYSTEM | FILE_ATTR_VOLUME_ID)

typedef struct _FILE_INFO {
	//Buffer size가 [8+3+1+1] 
	//8(파일의 이름 크기) + 3(확장자 크기) + 1(‘.’문자의 크기) + 1(문자열 종료자의 크기)
	BYTE			filename[8+3+1+1]; 
	BYTE			attribute;
	WORD			time;
	WORD			date;
	DWORD			filesize;
	WORD			start_cluster;
} FILE_INFO, *PFILE_INFO;

typedef BOOL (*FS_FILE_INFO_CALLBACK)(FILE_INFO *pFileInfo, PVOID Context);

#define OF_READ_ONLY					0x01



KERNELAPI VOID FsGetFileList(FS_FILE_INFO_CALLBACK CallBack, PVOID Context);

KERNELAPI HANDLE FsOpenFile(BYTE *pFilename, DWORD Attribute);
KERNELAPI BOOL   FsCloseFile(HANDLE FileHandle);
KERNELAPI DWORD  FsReadFile(HANDLE FileHandle, BYTE *pData, DWORD NumberOfBytesToRead);


#endif