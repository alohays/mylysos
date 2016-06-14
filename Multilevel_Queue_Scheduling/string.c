#include "string.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

void* memset(void *pSource, int ch, size_t size)
{
	size_t i;

	for(i=0; i<size; i++)
		*((char *)pSource+i) = (char)ch;

	return pSource;
}