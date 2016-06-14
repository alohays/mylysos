#ifndef _SYSCALL_TYPE_H
#define _SYSCALL_TYPE_H

#include "types.h"

typedef enum _SYSCALL_TYPES{
	SYSCALL_TERMINATED=0,
	SYSCALL_DELAY,
	SYSCALL_GET_TICKCOUNT,
	SYSCALL_SHOW_TSWATCHDOG,
	SYSCALL_HIDE_TSWATCHDOG,

	SYSCALL_CLEAR_SCREEN,
	SYSCALL_PRINT_TEXT,
	SYSCALL_PRINT_TEXT_XY,
	SYSCALL_PRINT_TEXT_ATTR,
	SYSCALL_PRINT_TEXT_XY_ATTR,

	SYSCALL_HAS_KEY,
	SYSCALL_GET_KEYDATA,

	SYSCALL_SET_VIDEO_MODE,
	SYSCALL_GET_CURRENT_VIDEO_MODE,
	SYSCALL_LOAD_BITMAP,
	SYSCALL_GET_PALETTE_HANDLE,
	SYSCALL_GET_BITMAP_INFO,
	SYSCALL_BITBLT,
	SYSCALL_CLOSE_BITMAP_HANDLE,
} SYSCALL_TYPES;

typedef struct _SYSCALL_MSG{
	SYSCALL_TYPES		syscall_type;
	union{
		struct {
			WORD	x, y;
			BYTE	*pt_text;
			BYTE	attr;
		} PRINT_TEXT;

		struct {
			DWORD	milli_sec;
		} DELAY;

		struct {
			char	*pt_filename;
		} LOAD_BITMAP;

		struct {
			HANDLE	bitmap;
		}  GET_PALETTE_HANDLE;

		struct {
			HANDLE	bitmap;
		} CLOSE_BITMAP_HANDLE;

	} parameters;
} SYSCALL_MSG, *PSYSCALL_MSG;

#endif