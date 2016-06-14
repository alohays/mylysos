#ifndef _SYS_DESC_H_
#define _SYS_DESC_H_

#define TIMEOUT_PER_SECOND		50

#define USER_CS					0x001b
#define USER_DS					0x0023

#define TASK_SW_SEG				0x0028
#define INIT_TSS_SEG			0x0030

#define SYSCALL_GATE			0x0048

#define KERNEL_CS				0x0008
#define KERNEL_DS				0x0010

#define KERNEL_SS				KERNEL_DS
#define USER_SS					USER_DS

#define NUMBERS_OF_GDT_ENTRIES	10

#define TMR_TSS_SEG				0x0038
#define SOFT_TS_TSS_SEG			0x0040

#define SYSTEM_TMR_INT_NUMBER	0x20
#define SOFT_TASK_SW_INT_NUMBER	0x30

#pragma pack(push, 1)

typedef struct _IDT_GATE{
	WORD	offset_low;
	WORD	selector;
	WORD	type;
	WORD	offset_high;
} IDT_GATE, *PIDT_GATE;

typedef struct _TSS_32{
	WORD	prev_task_link, res0;
	DWORD	esp0;
	WORD	ss0, res8;
	DWORD	esp1;
	WORD	ss1, res16;
	DWORD	esp2;
	WORD	ss2, res24;
	DWORD	cr3;
	int		eip;
	DWORD	eflags;
	DWORD	eax;
	DWORD	ecx;
	DWORD	edx;
	DWORD	ebx;
	DWORD	esp;
	DWORD	ebp;
	DWORD	esi;
	DWORD	edi;
	WORD	es, res72;
	WORD	cs, res76;
	WORD	ss, res80;
	WORD	ds, res84;
	WORD	fs, res88;
	WORD	gs, res92;
	WORD	ldt_set_selector, res96;
	WORD	t_flag, io_map_base_address;
} TSS_32, *PTSS_32;

typedef struct _SEGMENT_DESC{

	BYTE	limit_1;
	BYTE	limit_2;
	BYTE	base_1;
	BYTE	base_2;
	BYTE	base_3;
	BYTE	type;
	BYTE	glimit_3;
	BYTE	base_4;

} SEGMENT_DESC, *PSEGMENT_DESC;

struct _DESC_TABLE_REG{
	WORD	size;
	int		address;
};

typedef struct _CALLGATE_DESC{
	BYTE	offset_1;
	BYTE	offset_2;
	WORD	selector;
	BYTE	count;
	BYTE	type;
	BYTE	offset_3;
	BYTE	offset_4;

} CALLGATE_DESC, *PCALLGATE_DESC;

typedef struct _DESC_TABLE_REG		IDTR_DESC;
typedef struct _DESC_TABLE_REG		*PIDTR_DESC;
typedef struct _DESC_TABLE_REG		GDTR_DESC;
typedef struct _DESC_TABLE_REG		*PGDTR_DESC;
typedef struct _DESC_TABLE_REG		LDTR_DESC;
typedef struct _DESC_TABLE_REG		*PLDTR_DESC;

#pragma pack(pop)

#endif