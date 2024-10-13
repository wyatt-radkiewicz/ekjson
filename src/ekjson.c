#include <assert.h>

#include "ekjson.h"

// Transitions
enum { CANY, COS, COE, CWS, CQ, CFS, CBS, CZ, CDIG, CCOM, CCOL, CMIN, CPLS,
	CDOT, CAS, CAE, Ca, Cb, Cc, Cd, CAD, Ce, CE, Cf, CF, Cl, Cn, Ct, Cu,
	Cr, Cs, CEOF, CTOP_EOF, COBJ_Q, CKV_COM, CKV_OE, CARR_AE, CARR_COM,
	CMAX, };

// States
enum { SERR, SVAL, SOBJ, SSTR, SESC, SU1, SU2, SU3, SU4, SKEY, SEND,
	SNEG, SZERO, SDIGI, SFRACA, SFRACB, SEXPA, SEXPB, SEXPC, ST, SR, STU,
	SF, SA, SFL, SS, SN, SNU, SNL, SMAX, SEOF, };

// Stack states
enum { STOBJ, STKV, STARR, STTOP, STMAX, };
typedef struct stack {
	uint32_t type : 2;
	uint32_t len : 30;
	uint32_t id;
} stack_t;

// Transition Actions
enum { AERR, ANO, APUSH, APOPKV, ATOK, APOP, AEND, APUSHKV, };

// State Transitions
typedef struct trans {
	uint8_t action : 3;
	uint8_t state : 5;
} trans_t;

#define T(C, A, S) [C] = ((trans_t){ .action = A, .state = S })
static const trans_t trans[SMAX][CMAX] = {
	[SVAL] = { T(COS, APUSH, SOBJ), T(CQ, ATOK, SSTR), T(CZ, ATOK, SZERO),
		T(CTOP_EOF, AEND, SEND), T(CMIN, ATOK, SNEG), T(Ct, ATOK, ST),
		T(CPLS, ATOK, SDIGI), T(Cf, ATOK, SF), T(Cn, ATOK, SN),
		T(CAS, APUSH, SVAL), T(CWS, ANO, SVAL), T(CDIG, ATOK, SDIGI) },
	[SOBJ] = { T(CWS, ANO, SOBJ), T(COE, APOP, SVAL),
		T(COBJ_Q, ATOK, SSTR) },
	[SSTR] = { T(COS, ANO, SSTR), T(COE, ANO, SSTR), T(CWS, ANO, SSTR),
		T(CQ, ANO, SEND), T(CBS, ANO, SESC), T(CZ, ANO, SSTR),
		T(CDIG, ANO, SSTR), T(CCOM, ANO, SSTR), T(CCOL, ANO, SSTR),
		T(CMIN, ANO, SSTR), T(CPLS, ANO, SSTR), T(CDOT, ANO, SSTR),
		T(CAS, ANO, SSTR), T(CAE, ANO, SSTR), T(CANY, ANO, SSTR),
		T(Ca, ANO, SSTR), T(Cb, ANO, SSTR), T(Cd, ANO, SSTR),
		T(CAD, ANO, SSTR), T(Ce, ANO, SSTR), T(CE, ANO, SSTR),
		T(Cf, ANO, SSTR), T(CF, ANO, SSTR), T(Cl, ANO, SSTR),
		T(Cn, ANO, SSTR), T(Ct, ANO, SSTR), T(Cu, ANO, SSTR),
		T(Cr, ANO, SSTR), T(CTOP_EOF, AEND, SSTR), T(Cc, ANO, SSTR),
		T(COBJ_Q, APUSHKV, SKEY), T(CKV_COM, ANO, SSTR),
		T(Cs, ANO, SSTR), T(CKV_OE, ANO, SSTR), T(CARR_AE, ANO, SSTR),
		T(CARR_COM, ANO, SSTR), T(CFS, ANO, SSTR), },
	[SESC] = { T(CBS, ANO, SSTR), T(CFS, ANO, SSTR), T(Cb, ANO, SSTR),
		T(Cf, ANO, SSTR), T(Cn, ANO, SSTR), T(Cr, ANO, SSTR),
		T(Ct, ANO, SSTR), T(Cu, ANO, SU1), },
	[SU1] = { T(CZ, ANO, SU2), T(CDIG, ANO, SU2), T(Ca, ANO, SU2),
		T(Cb, ANO, SU2), T(Cc, ANO, SU2), T(Cd, ANO, SU2),
		T(Ce, ANO, SU2), T(Cf, ANO, SU2), T(CAD, ANO, SU2),
		T(CE, ANO, SU2), T(CF, ANO, SU2), },
	[SU2] = { T(CZ, ANO, SU3), T(CDIG, ANO, SU3), T(Ca, ANO, SU3),
		T(Cb, ANO, SU3), T(Cc, ANO, SU3), T(Cd, ANO, SU3),
		T(Ce, ANO, SU3), T(Cf, ANO, SU3), T(CAD, ANO, SU3),
		T(CE, ANO, SU3), T(CF, ANO, SU3), },
	[SU3] = { T(CZ, ANO, SU4), T(CDIG, ANO, SU4), T(Ca, ANO, SU4),
		T(Cb, ANO, SU4), T(Cc, ANO, SU4), T(Cd, ANO, SU4),
		T(Ce, ANO, SU4), T(Cf, ANO, SU4), T(CAD, ANO, SU4),
		T(CE, ANO, SU4), T(CF, ANO, SU4), },
	[SU4] = { T(CZ, ANO, SSTR), T(CDIG, ANO, SSTR), T(Ca, ANO, SSTR),
		T(Cb, ANO, SSTR), T(Cc, ANO, SSTR), T(Cd, ANO, SSTR),
		T(Ce, ANO, SSTR), T(Cf, ANO, SSTR), T(CAD, ANO, SSTR),
		T(CE, ANO, SSTR), T(CF, ANO, SSTR), },
	[SKEY] = { T(CWS, ANO, SKEY), T(CCOL, ANO, SVAL), },
	[SEND] = { T(CWS, ANO, SEND), T(CTOP_EOF, AEND, SEOF),
		T(CKV_COM, APOP, SOBJ), T(CKV_OE, APOPKV, SVAL),
		T(CARR_COM, ANO, SVAL), T(CARR_AE, APOP, SVAL), },
	[SZERO] = { T(CDOT, ANO, SFRACA), T(CE, ANO, SEXPA),
		T(CKV_COM, APOP, SOBJ), T(CKV_OE, APOPKV, SVAL),
		T(CARR_COM, ANO, SVAL), T(CARR_AE, APOP, SVAL),
		T(CTOP_EOF, AEND, SEOF), T(CWS, ANO, SEND), },
	[SNEG] = { T(CZ, ANO, SZERO), T(CDIG, ANO, SDIGI),
		T(CKV_COM, APOP, SOBJ), T(CKV_OE, APOPKV, SVAL),
		T(CARR_COM, ANO, SVAL), T(CARR_AE, APOP, SVAL),
		T(CTOP_EOF, AEND, SEOF), T(CWS, ANO, SEND), },
	[SDIGI] = { T(CZ, ANO, SDIGI), T(CDIG, ANO, SDIGI), T(CE, ANO, SEXPA),
		T(Ce, ANO, SEXPA), T(CDOT, ANO, SFRACA),
		T(CKV_COM, APOP, SOBJ), T(CWS, ANO, SEND), 
		T(CKV_OE, APOPKV, SVAL), T(CARR_COM, ANO, SVAL),
		T(CARR_AE, APOP, SVAL), T(CTOP_EOF, AEND, SEOF), },
	[SFRACA] = { T(CZ, ANO, SFRACA), T(CDIG, ANO, SFRACA), },
	[SFRACB] = { T(CZ, ANO, SFRACB), T(CDIG, ANO, SFRACB),
		T(Ce, ANO, SEXPA), T(CE, ANO, SEXPA), T(CTOP_EOF, AEND, SEOF), 
		T(CKV_COM, APOP, SOBJ), T(CKV_OE, APOPKV, SVAL),
		T(CARR_COM, ANO, SVAL), T(CARR_AE, APOP, SVAL),
		T(CWS, ANO, SEND), },
	[SEXPA] = { T(CZ, ANO, SEXPC), T(CDIG, ANO, SEXPC),
		T(CPLS, ANO, SEXPB), T(CMIN, ANO, SEXPB), },
	[SEXPB] = { T(CZ, ANO, SEXPC), T(CDIG, ANO, SEXPC), },
	[SEXPC] = { T(CZ, ANO, SEXPC), T(CDIG, ANO, SEXPC),
		T(CKV_COM, APOP, SOBJ), T(CKV_OE, APOPKV, SVAL),
		T(CARR_COM, ANO, SVAL), T(CARR_AE, APOP, SVAL),
		T(CTOP_EOF, AEND, SEOF), T(CWS, ANO, SEND), },
	[ST] = { T(Cr, ANO, SR) }, [SR] = { T(Cu, ANO, STU) },
	[STU] = { T(Ce, ANO, SEND) }, [SF] = { T(Ca, ANO, SA) },
	[SA] = { T(Cl, ANO, SFL) }, [SFL] = { T(Cs, ANO, SS) },
	[SS] = { T(Ce, ANO, SEND) }, [SN] = { T(Cu, ANO, SNU) },
	[SNU] = { T(Cl, ANO, SNL) }, [SNL] = { T(Cl, ANO, SEND) },
};
#undef T

#define C(TR, CHR) [CHR] = { TR, TR, TR, TR }
static const uint8_t chars[256][STMAX] = {
	C(COS, '{'), C(CWS, ' '), C(CWS, '\r'), C(CWS, '\t'), C(CCOL, ':'),
	C(CWS, '\n'), C(CFS, '/'), C(CBS, '\\'), C(CZ, '0'), C(CDIG, '1'),
	C(CDIG, '2'), C(CDIG, '3'), C(CDIG, '4'), C(CDIG, '5'), C(CDIG, '6'),
	C(CDIG, '7'), C(CDIG, '8'), C(CDIG, '9'), C(CMIN, '-'), C(CPLS, '+'),
	C(CDOT, '.'), C(CAS, '['), C(Ca, 'a'), C(Cb, 'b'), C(Cc, 'c'),
	C(Cd, 'd'), C(CAD, 'A'), C(CAD, 'B'), C(CAD, 'C'), C(CAD, 'D'),
	C(Ce, 'e'), C(CE, 'E'), C(Cf, 'f'), C(CF, 'F'), C(Cl, 'l'), C(Cn, 'n'),
	C(Ct, 't'), C(Cu, 'u'), C(Cr, 'r'), C(Cs, 's'),
	['\0'] = { CEOF,	CEOF,		CEOF,		CTOP_EOF },
	['"'] = {  COBJ_Q,	CQ,		CQ,		CQ },
	[','] = {  CCOM,	CKV_COM,	CARR_COM,	CCOM },
	['}'] = {  COE,		CKV_OE,		COE,		COE },
	[']'] = {  CANY,	CANY,		CARR_AE,	CANY },
};
#undef C

#define DEPTH 1024

ejresult_t ejparse(const char *src, ejtok_t *t, const size_t nt) {
	static const uint8_t types[SMAX] = { [SSTR] = EJSTR, [SZERO] = EJNUM,
		[SNEG] = EJNUM, [SDIGI] = EJNUM, [ST] = EJBOOL, [SF] = EJBOOL,
		[SN] = EJNULL };
	static const uint8_t pt[SMAX] = {
		[SOBJ] = EJOBJ, [SSTR] = EJKV, [SVAL] = EJARR };
	static void *const actions[] = { [ANO] = &&next, [APUSH] = &&push,
		[APOPKV] = &&popkv, [ATOK] = &&newtok, [APOP] = &&pop,
		[AERR] = &&err, [AEND] = &&end, [APUSHKV] = &&pushkv, };
	static stack_t stack[DEPTH] = { { .type = STTOP, .len = 0, .id = 0 } };

	const char *const base = src + 1;
	ejtok_t *const start = t, *const end = t + nt;
	trans_t newstate;
	int top = 0, state = SVAL;

next:	newstate = trans[state][chars[*src++][stack[top].type]];
	state = newstate.state;
	goto *actions[newstate.action];

push:
	if (t == end || ++top == DEPTH) goto oom;
	stack[top] = (stack_t){ .type = pt[state], .len = 1, .id = t - start };
	*t++ = (ejtok_t){ .start = src - base, .type = pt[state], .len = 1 };
	goto next;
newtok:
	if (t == end) goto oom;
	stack[top].len++;
	*t++ = (ejtok_t){ .start = src - base, .type = types[state], .len = 1 };
	goto next;
pushkv:
	(t - 1)->type = EJKV;
	stack[top].len--;
	if (++top == DEPTH) goto oom;
	stack[top] = (stack_t){ .type = STKV, .len = 1, .id = t - start - 1 };
	goto next;
popkv:
	{
		const uint32_t len = stack[top].len;
		start[stack[top].id].len = len;
		stack[--top].len += len;
	}
pop:
	{
		const uint32_t len = stack[top].len;
		start[stack[top].id].len = len;
		stack[--top].len += len;
	}
	goto next;
end:	return (ejresult_t){ .err = false, .loc = NULL, .ntoks = t - start };
err:	return (ejresult_t){ .err = true, .loc = src, .ntoks = t - start };
oom:	return (ejresult_t){ .err = false, .loc = src, .ntoks = t - start };
}

