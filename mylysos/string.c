#include "string.h"

#ifndef	NULL
# define NULL ((void * )0)
#endif

void* memset(void *pSource, int ch, size_t size)
{
	size_t i;

	for( i=0; i<size; i++)
		*(((char *)pSource)+i ) = (char)ch;

	return pSource;
}

//문자열 대문자로 변환
char *strupr(char *str)
{
	char *cp = str;

	while(*cp != '\0') {
		if(*cp >= 'a' && *cp <= 'z')
			*cp = *cp-'a' + 'A';
		cp++;
	}

	return str;
}

//문자열 비교
int strcmp(const char *cs, const char *ct)
{
	char __res;

	while(1) {
		if((__res = *cs - *ct++ ) != 0 || !*cs++)
			break;
	}

	return __res;
}

//문자열 길이
size_t strlen(const char * s)
{
	const char *sc;

	for(sc = s; *sc != '\0'; ++sc);

	return sc - s;
}

//메모리 복사함수
void *memcpy( void *dest, const void *src, size_t count)
{
	char *tmp = (char *) dest, *s = (char *) src;

	while (count--)
		*tmp++ = *s++;

	return dest;
}

//문자열의 뒷 부분부터 임의의 문자위치를 검색
char *strrchr(const char * s, int c)
{
	const char *p = s + strlen(s);
	do {
		if (*p == (char)c )
			return (char *)p;
	} while(--p >= s);
	return NULL;
}

//문자열 복사 
char * strcpy(char * dest, const char *src)
{
	char *tmp = dest;

	while ((*dest++ = *src++) != '\0');

	return tmp;
}

//문자열 덧붙이기
char * strcat(char * dest, const char *src)
{
	char *tmp = dest;

	while(*dest)
		dest++;
	while ((*dest++ = *src++) != '\0');

	return tmp;
}