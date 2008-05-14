#include "ucl.h"
#include "target.h"
#include "reg.h"
#include "output.h"

static int ORG;
static int FloatNum;
static char *ASMTemplate[] =
{
#define TEMPLATE(code, str) str, 
#include "x86win32.tpl"
#undef TEMPLATE
};

static void Align(Symbol p)
{
	int align = p->ty->align;

	if (ORG % align != 0)
	{
		Print("align %d\n", align);
		ORG = ALIGN(ORG, align);
	}
	ORG += p->ty->size;
}

static char* GetAccessName(Symbol p)
{
	if (p->aname != NULL)
		return p->aname;
	
	switch (p->kind)
	{
	case SK_Constant:
		if (p->name[0] == '0' && p->name[1] == 'x')
			p->aname = FormatName("0%sH", &p->name[2]);
		else
			p->aname = p->name;
		break;

	case SK_String:
	case SK_IRegister:
	case SK_Label:
		p->aname = p->name;
		break;

	case SK_Variable:
	case SK_Temp:
		if (p->level == 0)
		{
			p->aname = FormatName("_%s", p->name);
		}
		else if (p->sclass == TK_STATIC)
		{
			p->aname = FormatName("%s%d", p->name, TempNum++);
		}
		else
		{
			
			p->aname = FormatName("(%d)[ebp]", AsVar(p)->offset);
		}
		break;

	case SK_Function:
		p->aname = FormatName("_%s", p->name);
		break;
		
	case SK_Offset:
		{
			Symbol base = p->link;
			int n = AsVar(p)->offset;

			if (base->level == 0 || base->sclass == TK_STATIC)
			{
				p->aname = FormatName("%s%s%d", GetAccessName(base), n >= 0 ? "+" : "", n);
			}
			else
			{
				n += AsVar(base)->offset;
				p->aname = FormatName("(%d)[ebp]", n);
			}
		}
		break;

	default:
		assert(0);
	}

	return p->aname;
}

void SetupRegisters(void)
{
	X86Regs[EAX] = CreateReg("eax", "[eax]", EAX);
	X86Regs[EBX] = CreateReg("ebx", "[ebx]", EBX);
	X86Regs[ECX] = CreateReg("ecx", "[ecx]", ECX);
	X86Regs[EDX] = CreateReg("edx", "[edx]", EDX);
	X86Regs[ESI] = CreateReg("esi", "[esi]", ESI);
	X86Regs[EDI] = CreateReg("edi", "[edi]", EDI);

	X86WordRegs[EAX] = CreateReg("ax", NULL, EAX);
	X86WordRegs[EBX] = CreateReg("bx", NULL, EBX);
	X86WordRegs[ECX] = CreateReg("cx", NULL, ECX);
	X86WordRegs[EDX] = CreateReg("dx", NULL, EDX);
	X86WordRegs[ESI] = CreateReg("si", NULL, ESI);
	X86WordRegs[EDI] = CreateReg("di", NULL, EDI);

	X86ByteRegs[EAX] = CreateReg("al", NULL, EAX);
	X86ByteRegs[EBX] = CreateReg("bl", NULL, EBX);
	X86ByteRegs[ECX] = CreateReg("cl", NULL, ECX);
	X86ByteRegs[EDX] = CreateReg("dl", NULL, EDX);
}

void PutASMCode(int code, Symbol opds[])
{
	char *fmt = ASMTemplate[code];
	char *prefix = NULL;
	int i;
	
	PutChar('\t');
	while (*fmt)
	{
		switch (*fmt)
		{
		case ';':
			PutString("\n\t");
			break;

		case '%':
			fmt++;
			if (*fmt == 'b')
			{
				prefix = "BYTE PTR ";
				fmt++;
			}
			else if (*fmt == 'w')
			{
				prefix = "WORD PTR ";
				fmt++;
			}
			else if (*fmt == 'd')
			{
				prefix = "DWORD PTR ";
				fmt++;
			}
			else
				prefix = NULL;

			i = *fmt - '0';
			if (opds[i]->reg != NULL)
			{
				PutString(opds[i]->reg->name);
			}
			else
			{
				if (prefix && opds[i]->kind != SK_Constant && opds[i]->kind != SK_Function)
				{
					PutString(prefix);
				}
				PutString(GetAccessName(opds[i]));
			}
			break;

		default:
			PutChar(*fmt);
			break;
		}
		fmt++;
	}
	PutChar('\n');		
}

void BeginProgram(void)
{
	int i;

	ORG = 0;
	FloatNum = TempNum = 0;
	for (i = EAX; i < EDI; ++i)
	{
		if (X86Regs[i] != NULL)
		{
			X86Regs[i]->link = NULL;
		}
	}
	PutString("; Code auto-generated by UCC\n");
	PutString(".486\n");
	PutString(".MODEL FLAT\n");
	PutString("EXTRN __fltused:NEAR32\n");
	PutString("EXTRN __ftol:NEAR32\n");
	PutString("EXTRN _memset:NEAR32\n\n");
}

void Segment(int seg)
{
	if (seg == DATA)
	{
		PutString(".DATA\n\n");
	}
	else if (seg == CODE)
	{
		PutString(".CODE\n\n");
	}
}

void Import(Symbol p)
{
	Print("EXTRN %s:NEAR32\n\n", GetAccessName(p));
}

void Export(Symbol p)
{
	Print("PUBLIC %s\n\n", GetAccessName(p));
}

void DefineString(String str, int size)
{
	int i = 0;
	
	PutString("BYTE\t");
	while (i < size)
	{
		if (! isprint(str->chs[i]))
		{
			Print("0%xH", str->chs[i]);
			i++;
		}
		else
		{
			PutString("'");
			while (i < size && isprint(str->chs[i]))
			{
				if (str->chs[i] == '\'')
					PutString("''");
				else
					PutChar(str->chs[i]);
				i++;
			}
			PutString("'");
		}
		if (i < size)
			PutString(", ");
	}
	PutString("\n");
}

void DefineFloatConstant(Symbol p)
{
	int align = p->ty->align;

	p->aname = FormatName("flt%d", FloatNum++);
	
	Align(p);
	Print("%s\t", p->aname);
	DefineValue(p->ty, p->val);
}

void DefineGlobal(Symbol p)
{
	Align(p);
	if (p->sclass != TK_STATIC)
	{
		Export(p);
	}
	Print("%s\t", GetAccessName(p));
}

void DefineCommData(Symbol p)
{
	Align(p);
	GetAccessName(p);
	if (p->sclass == TK_STATIC)
	{
		Print("%s\t", p->aname);
		Space(p->ty->size);
	}
	else
	{
		Print("COMM\t%s:%d\n", p->aname, p->ty->size);
	}
}

void DefineAddress(Symbol p)
{
	Print("DWORD\t%s", GetAccessName(p));
}

void DefineValue(Type ty, union value val)
{
	int tcode = TypeCode(ty);

	switch (tcode)
	{
	case I1: case U1:
		Print("BYTE\t0%xH\n", val.i[0] & 0xff);
		break;

	case I2: case U2:
		Print("WORD\t0%xH\n", val.i[0] & 0xffff);
		break;

	case I4: case U4:
		Print("DWORD\t0%xH\n", val.i[0]);
		break;

	case F4:
		Print("DWORD\t0%xH\n", *(unsigned *)&val.f );
		break;

	case F8:
		{
			unsigned *p = (unsigned *)&val.d;
			Print("DWORD\t0%xH, 0%xH\n", p[0], p[1]);
			break;
		}

	default:
		assert(0);
	}
}

void Space(int size)
{
	Print("BYTE %d DUP (0)\n", size);
}

void DefineLabel(Symbol p)
{
	Print("%s:\n", GetAccessName(p));
}

void EndProgram(void)
{
	PutString("END\n");
	Flush();
}

