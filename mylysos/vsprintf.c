#include "vsprintf.h"

#define SIGN   1
#define LARGE  2

static int ffmt(char *buf, const char *fmt, va_list args);


int vsprintf(char *buf, const char *fmt, va_list args)
{
	return ffmt(buf, fmt, args);
}

static int ffmt(char *buf, const char *fmt, va_list args)
{
	char *str, temp, numtemp[33], *numptr, prefix;
	unsigned int number, base, flag, width;

	str = buf;
	numtemp[32]=0;

	for (; *fmt ; fmt++){
		if (*fmt != '%'){
			*str = *fmt;
			++str;
			continue;
		}

		fmt++;
		flag=0;
		prefix= ' ';
		width=0;

		if(*fmt == '0')
			prefix='0';

		while(*fmt >= '0' && *fmt <= '9') {
			width=(width * 10) + (*(fmt++) - '0');
		}

		switch(*fmt) {
			case 'c':
				*str++ = va_arg(args, char);
				continue;
			case 's':
				numptr = va_arg(args, char *);
				while(*numptr != 0 )
					*str++ = *numptr++;
				continue;
			case '%':
				*str++ = '%';
				continue;
			case 'b':
				base = 2;
				break;
			case 'd':   case 'i':
				flag |= SIGN;
			case 'u':
				base = 10;
				break;
			case 'o':
				base = 8;
				break;
			case 'X':
				flag |= LARGE;
			case 'x':
				base = 16;
				break;
			default:
				continue;
		}

		numptr = &numtemp[31];
		number = va_arg(args, unsigned int);

		if( (flag&SIGN) && (number&0x80000000) ) {
			number = ~number + 1;
			*str++ = '-';
		}

		do {
			temp = number%base;
			if(temp > 9) {
				temp += ((flag & LARGE ? 'A' : 'a') - 10);
			} else {
				temp += '0';
			}
			*numptr-- = temp;
			if(width > 0) width--;
		} while(number/=base);

		if(width > 0) {
			while(width-- != 0)
				*numptr-- = prefix;
		}
		while(temp = *(++numptr)) {
			*str++ = temp;
		}
	}
	*str = 0;

	return (int)(str-buf+1);
}