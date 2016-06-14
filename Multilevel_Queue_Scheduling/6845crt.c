#include "6845crt.h"

#define SCREEN_CX				80
#define SCREEN_CY				25
#define VIDEO_BUFFER_ADDRESS	0xb8000
#define VIDEO_BUFFER_SIZE		((SCREEN_CX*SCREEN_CY)*2)

//텍스트 출력 ATTRIBUTE 설정
//x86 CPU에서의 기본 출력 문자의 색상, 배경 색상, 문자 강도(밝기), 문자 깜빡임 여부 등 설정
#define DEFAULT_ATTRIBUTE		7

#define KEY_LF					'\n'
#define KEY_CR					'\r'
#define KEY_TAB					'\t'
#define DEFAULT_TAB_SIZE		4

static WORD m_CursorXPos, m_CursorYPos;

static void CrtpSetCursorPos(WORD x, WORD y);
static void CrtpUpdateCursorPosByPtr(UCHAR *pPtr);
static UCHAR* CrtpGetNextVideoPtr(void);
static UCHAR* CrtpGetNextVideoPtrWithPos(WORD x, WORD y);
static void CrtpKeyLF(UCHAR **pVideoPtr);
static void CrtpKeyCR(UCHAR **pVideoPtr);
static void CrtpKeyBackspace(UCHAR **pVideoPtr);
static void CrtpScrollOneLine(void);
static int CrtPrintfFmt(UCHAR Attr, WORD x, WORD y, const char *fmt, va_list args);



//문자열 출력 함, char* 및 기타 가변인수를 받아서 출력
//(가변 인수는 https://msdn.microsoft.com/ko-kr/library/fxhdxye9.aspx 참조)
//기능 구현은 stdarg.h에 별도로 구현(윈도우 라이브러리 못쓰기 때문)
//e.g. printf("%d + %d = %d", a, b, c)에서 a, b, c등 여러 인수 저장 위해 사용

KERNELAPI int CrtPrintf(const char *fmt, ...)
{
	int i;
	va_list args;

	va_start(args,fmt);

	i=CrtPrintfFmt(DEFAULT_ATTRIBUTE, 0xffff, 0xffff, fmt, args);
	va_end(args);

	return i;
}

static UCHAR* CrtpGetNextVideoPtr(void)
{
	return (UCHAR *)(VIDEO_BUFFER_ADDRESS+(m_CursorXPos+m_CursorYPos*SCREEN_CX)*2);
}

static UCHAR* CrtpGetNextVideoPtrWithPos(WORD x, WORD y)
{
	return (UCHAR *)(VIDEO_BUFFER_ADDRESS+(x+y*SCREEN_CX)*2);
}

static void CrtpScrollOneLine(void)
{
	UCHAR *pVideoBase = (UCHAR *)VIDEO_BUFFER_ADDRESS;
	UCHAR *pSecondLine = (UCHAR *)(VIDEO_BUFFER_ADDRESS+SCREEN_CX*2);

	while(pSecondLine < (UCHAR *)(VIDEO_BUFFER_ADDRESS+VIDEO_BUFFER_SIZE)){
		*pVideoBase++ = (*pSecondLine++);
	}
	while(pVideoBase < (UCHAR *)(VIDEO_BUFFER_ADDRESS+VIDEO_BUFFER_SIZE)){
		*pVideoBase++ = ' ';
		*pVideoBase++ = DEFAULT_ATTRIBUTE;
	}
}

//특수문자 처리(Line Feed - 커서를 다음 줄(아래)로 이동)
static void CrtpKeyLF(UCHAR **pVideoPtr)
{
	WORD x, y;
	UCHAR *pVideoBase = (UCHAR *)VIDEO_BUFFER_ADDRESS;
	int videoPos = ((int)*pVideoPtr-VIDEO_BUFFER_ADDRESS)/2;

	y = (WORD)(videoPos / SCREEN_CX);
	x = (WORD)(videoPos % SCREEN_CX);

	if(y == (SCREEN_CY-1)){
		CrtpScrollOneLine();
		*pVideoPtr = (UCHAR *)(VIDEO_BUFFER_ADDRESS+VIDEO_BUFFER_SIZE-(SCREEN_CX-x)*2);
	} else{
		*pVideoPtr += (SCREEN_CX*2);
	}
}

//특수문자 처리(Carriage Return - 커서를 맨 왼쪽으로 이동)
static void CrtpKeyCR(UCHAR **pVideoPtr)
{
	WORD x;
	int videoPos = ((int)*pVideoPtr-VIDEO_BUFFER_ADDRESS)/2;

	x = (WORD)(videoPos % SCREEN_CX);
	*pVideoPtr -= (x*2);
	
}

//특수문자 처리(BackSpace)
static void CrtpKeyBackspace(UCHAR **pVideoPtr)
{
	if(*pVideoPtr < (UCHAR *)(VIDEO_BUFFER_ADDRESS+2))
		return;

	*pVideoPtr -= 2;
	**pVideoPtr = ' ';
}

//마지막 문자 위치로 커서 이동
static void CrtpUpdateCursorPosByPtr(UCHAR *pPtr)
{
	WORD x, y;
	int bufPos = ((int)pPtr-VIDEO_BUFFER_ADDRESS)/2;

	y = (WORD)(bufPos / SCREEN_CX);
	x = (WORD)(bufPos % SCREEN_CX);

	CrtpSetCursorPos(x, y);
}

//실제 커서 위치 제어 함수
static void CrtpSetCursorPos(WORD x, WORD y)
{
	UCHAR data;
//커서 위치 설정 시 다른 곳에서 커서 위치를 동시에 수정하는 걸 막기 위해
//Critical Section 지정(구현은 myoksos.h 참조)

ENTER_CRITICAL_SECTION();
	m_CursorXPos=x; m_CursorYPos=y;
EXIT_CRITICAL_SECTION();
	
	data=(UCHAR)((x+y*SCREEN_CX)>>8);
	WRITE_PORT_UCHAR((PUCHAR)0x3d4, 0x0e);
	WRITE_PORT_UCHAR((PUCHAR)0x3d5, data);
	data=(UCHAR)((x+y*SCREEN_CX)&0xff);
	WRITE_PORT_UCHAR((PUCHAR)0x3d4, 0x0f);
	WRITE_PORT_UCHAR((PUCHAR)0x3d5, data);
}

KERNELAPI VOID CrtClearScreen(VOID)
{
	UCHAR* pVideoBuf;

	pVideoBuf=(UCHAR *)VIDEO_BUFFER_ADDRESS;
	while(pVideoBuf < (UCHAR *)(VIDEO_BUFFER_ADDRESS+VIDEO_BUFFER_SIZE)){
		*pVideoBuf++=' ';
		*pVideoBuf++=DEFAULT_ATTRIBUTE;
	}

	CrtpSetCursorPos(0, 0);
}

BOOL CrtInitializeDriver(VOID)
{
	CrtClearScreen();

	return TRUE;
}

//화면 글씨 출력 및 스크롤링(글자 한줄 위로 올리기), 커서 업데이트
KERNELAPI BOOL CrtPrintTextXYWithAttr(LPCSTR pText, WORD x, WORD y, UCHAR Attr)
{
	int i;
	UCHAR *pVideoBuf;

	if(pText == NULL) return FALSE;

	if(x==0xffff && y==0xffff){
		pVideoBuf=CrtpGetNextVideoPtr();
	} else{
		pVideoBuf=CrtpGetNextVideoPtrWithPos(x, y);
	}

	while (*pText != NULL){
		if(pVideoBuf >= (UCHAR *)(VIDEO_BUFFER_ADDRESS+VIDEO_BUFFER_SIZE)){
			CrtpScrollOneLine();
			pVideoBuf -= (SCREEN_CX*2);
		}
		
		if(*pText==KEY_LF){
			;
		} else if(*pText==KEY_CR){
			CrtpKeyCR(&pVideoBuf);
			CrtpKeyLF(&pVideoBuf);
		} else if(*pText=='\b'){
			CrtpKeyBackspace(&pVideoBuf);
		} else if(*pText==KEY_TAB){
			for(i=0; i<DEFAULT_TAB_SIZE; i++){
				*pVideoBuf++ = ' ';
				*pVideoBuf++ = Attr;
			}
		} else{
			*pVideoBuf++ = (*pText);
			*pVideoBuf++ = Attr;
		}

		pText++;
	}

	CrtpUpdateCursorPosByPtr(pVideoBuf);

	return TRUE;
}


//가변인수로 받은 문자랑 출력 문자열 조합해서 버퍼에 저장
static int CrtPrintfFmt(UCHAR Attr, WORD x, WORD y, const char *fmt, va_list args)
{
	char buf[256];
	int i;

	i=vsprintf(buf, fmt, args);
	CrtPrintTextXYWithAttr(buf, x, y, Attr);

	return i;
}

