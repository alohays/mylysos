#ifndef _DEBUG_H_
#define _DEBUG_H_


#include "6845crt.h"

/*
 * 디버그 모드 출력 설정 부분 ( 프로젝트 디버그 모드일 경우 디버그 메세지 표시)
 */
#ifdef __DEBUG__
#define DbgPrint      CrtPrintf
#else
int DbgPrint(const char *fmt, ...) {return -1;}
#endif

#endif /* #ifndef _DEBUG_H_ */