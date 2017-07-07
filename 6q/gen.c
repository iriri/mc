#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "util.h"
#include "parse.h"
#include "mi.h"
#include "qasm.h"
#include "../config.h"

char qtag(Gen *g, Type *ty);
static void outtypebody(Gen *g, Type *ty);
static void outqtype(Gen *g, Type *ty);

static void out(Gen *g, Qop o, Loc r, Loc a, Loc b);
static void ocall(Gen *g, Loc fn, Loc ret, Loc env, Loc *args, Type **types, size_t nargs);
static Loc rval(Gen *g, Node *n);
static Loc lval(Gen *g, Node *n);
static Loc seqbase(Gen *g, Node *sl, Node *off);
static Loc slicelen(Gen *g, Node *n, Type *rty);
static Loc gencast(Gen *g, Srcloc loc, Loc val, Type *to, Type *from);

static Loc abortoob;
static Loc ptrsize;

static int isexport(Node *dcl)
{
	Node *n;

	/* Vishidden should also be exported. */
	if (dcl->decl.vis != Visintern)
		return 1;
	n = dcl->decl.name;
	if (!n->name.ns && streq(n->name.name, "main"))
		return 1;
	if (streq(n->name.name, "__init__"))
		return 1;
	return 0;
}

int isstack(Node *n)
{
	if (n->type == Nexpr)
		return isstacktype(n->expr.type);
	else
		return isstacktype(n->decl.type);
}

char *asmname(Node *n, char sigil)
{
	char *ns, *name, *sep;

	ns = n->name.ns;
	if (!ns)
		ns = "";
	name = n->name.name;
	sep = "";
	if (ns && ns[0])
		sep = "$";
	return strfmt("%c%s%s%s", sigil, ns, sep, name);
}


static Loc qtemp(Gen *g, char type)
{
	return (Loc){.kind=Ltemp, .tmp=g->nexttmp++, .tag=type};
}

static Loc temp(Gen *g, Node *n)
{
	char type;

	if (n->type == Ndecl)
		type = qtag(g, decltype(n));
	else
		type = qtag(g, exprtype(n));
	return (Loc){.kind=Ltemp, .tmp=g->nexttmp++, .tag=type};
}

static Loc qvar(Gen *g, Node *n)
{
	char tag;

	assert(n);
	if (n->type == Nexpr && exprop(n) == Ovar)
		n = decls[n->expr.did];
	assert(n->type == Ndecl);
	tag = qtag(g, n->decl.type);
	return (Loc){.kind=Ldecl, .dcl=n->decl.did, .tag=tag};
}

static Loc qlabelstr(Gen *g, char *lbl)
{
	return (Loc){.kind=Llabel, .lbl=lbl, .tag='l'};
}

static Loc qlabel(Gen *g, Node *lbl)
{
	return (Loc){.kind=Llabel, .lbl=lblstr(lbl), .tag='l'};
}

static Loc qconst(Gen *g, uint64_t cst, char tag)
{
	return (Loc){.kind=Lconst, .cst=cst, .tag=tag};
}

static Loc qblob(Gen *g, Blob *b)
{
	return (Loc){.kind=Lblob, .blob=b, .tag='l'};
}

static void out(Gen *g, Qop op, Loc r, Loc a, Loc b)
{
	if (g->ninsn == g->insnsz) {
		g->insnsz += g->insnsz/2 + 1;
		g->insn = xrealloc(g->insn, g->insnsz * sizeof(Insn));
	}
	g->insn[g->ninsn].op = op;
	g->insn[g->ninsn].ret = r;
	g->insn[g->ninsn].arg[0] = a;
	g->insn[g->ninsn].arg[1] = b;
	g->ninsn++;
}

static void ocall(Gen *g, Loc fn, Loc ret, Loc env, Loc *args, Type **types, size_t nargs) 
{
	if (g->ninsn == g->insnsz) {
		g->insnsz += g->insnsz/2 + 1;
		g->insn = xrealloc(g->insn, g->insnsz * sizeof(Insn));
	}
	g->insn[g->ninsn].op = Qcall;
	g->insn[g->ninsn].fn = fn;
	g->insn[g->ninsn].ret = ret;
	g->insn[g->ninsn].env = env;
	g->insn[g->ninsn].farg = args;
	g->insn[g->ninsn].fty = types;
	g->insn[g->ninsn].nfarg = nargs;
	g->ninsn++;
}

Type *codetype(Type *ft)
{
	ft = tybase(ft);
	if (ft->type == Tycode)
		return ft;
	assert(ft->type == Tyfunc);
	ft = tydup(ft);
	ft->type = Tycode;
	return ft;
}

static void addlocal(Gen *g, Node *n)
{
	lappend(&g->local, &g->nlocal, n);
}

static Loc qstacktemp(Gen *g, Node *e)
{
	Node *dcl;

	if (e->type == Nexpr)
		gentemp(e->loc, e->expr.type, &dcl);
	else if (e->type == Nfunc)
		gentemp(e->loc, e->func.type, &dcl);
	else
		dcl = e;
	addlocal(g, dcl);
	return qvar(g, dcl); 
}

static Loc ptrsized(Gen *g, Loc v)
{
	Loc r;

	if (v.tag == 'l')
		return v;
	r = qtemp(g, 'l');
	switch (v.tag) {
	case 'w':	out(g, Qextuw, r, v, Zq);	break;
	case 'h':	out(g, Qextuh, r, v, Zq);	break;
	case 'b':	out(g, Qextub, r, v, Zq);	break;
	default:	die("invalid tag\n");	break;
	}
	return r;
}

char qtag(Gen *g, Type *ty)
{
	switch (ty->type) {
	case Tybool:	return 'w';	break;
	case Tychar:	return 'w';	break;
	case Tyint8:	return 'w';	break;
	case Tyint16:	return 'w';	break;
	case Tyint:	return 'w';	break;
	case Tyint32:	return 'w';	break;
	case Tyint64:	return 'l';	break;
	case Tybyte:	return 'w';	break;
	case Tyuint8:	return 'w';	break;
	case Tyuint16:	return 'w';	break;
	case Tyuint:	return 'w';	break;
	case Tyuint32:	return 'w';	break;
	case Tyuint64:	return 'l';	break;
	case Tyflt32:	return 's';	break;
	case Tyflt64:	return 'd';	break;
	case Typtr:	return 'l';	break;
	default:	return 'l';	break;
	}
}

char *qtype(Gen *g, Type *ty)
{
	ty = tydedup(ty);
	switch (ty->type) {
	case Tybool:	return "b";	break;
	case Tychar:	return "w";	break;
	case Tyint8:	return "b";	break;
	case Tyint16:	return "h";	break;
	case Tyint:	return "w";	break;
	case Tyint32:	return "w";	break;
	case Tyint64:	return "l";	break;
	case Tybyte:	return "b";	break;
	case Tyuint8:	return "b";	break;
	case Tyuint16:	return "h";	break;
	case Tyuint:	return "w";	break;
	case Tyuint32:	return "w";	break;
	case Tyuint64:	return "l";	break;
	case Tyflt32:	return "s";	break;
	case Tyflt64:	return "d";	break;
	case Typtr:	return "l";	break;
	default:	return g->typenames[ty->tid];	break;
	}
}

static Qop loadop(Type *ty)
{
	Qop op;

	switch (tybase(ty)->type) {
	case Tybool:	op = Qloadub;	break;
	case Tybyte:	op = Qloadub;	break;
	case Tyuint8:	op = Qloadub;	break;
	case Tyint8:	op = Qloadsb;	break;

	case Tyint16:	op = Qloadsh;	break;
	case Tyuint16:	op = Qloaduh;	break;

	case Tyint:	op = Qloadsw;	break;
	case Tyint32:	op = Qloadsw;	break;
	case Tychar:	op = Qloaduw;	break;
	case Tyuint32:	op = Qloaduw;	break;

	case Tyint64:	op = Qloadl;	break;
	case Tyuint64:	op = Qloadl;	break;
	case Typtr:	op = Qloadl;	break;
	case Tycode:	op = Qloadl;	break;

	case Tyflt32:	op = Qloads;	break;
	case Tyflt64:	op = Qloadd;	break;
	default:	die("badload");	break;
	}
	return op;
}

static Qop storeop(Type *ty)
{
	Qop op;

	switch (tybase(ty)->type) {
	case Tybool:	op = Qstoreb;	break;
	case Tybyte:	op = Qstoreb;	break;
	case Tyuint8:	op = Qstoreb;	break;
	case Tyint8:	op = Qstoreb;	break;

	case Tyint16:	op = Qstoreh;	break;
	case Tyuint16:	op = Qstoreh;	break;

	case Tyint:	op = Qstorew;	break;
	case Tyint32:	op = Qstorew;	break;
	case Tychar:	op = Qstorew;	break;
	case Tyuint32:	op = Qstorew;	break;

	case Tyint64:	op = Qstorel;	break;
	case Tyuint64:	op = Qstorel;	break;
	case Typtr:	op = Qstorel;	break;
	case Tyflt32:	op = Qstores;	break;
	case Tyflt64:	op = Qstored;	break;
	default:	die("badstore");	break;
	}
	return op;
}

void fillglobls(Stab *st, Htab *globls)
{
	size_t i, j, nk, nns;
	void **k, **ns;
	Stab *stab;
	Node *s;
	char *n;

	k = htkeys(st->dcl, &nk);
	for (i = 0; i < nk; i++) {
		s = htget(st->dcl, k[i]);
		if (isconstfn(s))
			s->decl.type = codetype(s->decl.type);
		n = asmname(s->decl.name, '$');
		htput(globls, s, n);
	}
	free(k);

	ns = htkeys(file->file.ns, &nns);
	for (j = 0; j < nns; j++) {
		stab = htget(file->file.ns, ns[j]);
		k = htkeys(stab->dcl, &nk);
		for (i = 0; i < nk; i++) {
			s = htget(stab->dcl, k[i]);
			n = asmname(s->decl.name, '$');
			htput(globls, s, n);
		}
		free(k);
	}
	free(ns);
}

static void initconsts(Gen *g, Htab *globls)
{
	Type *ty;
	Node *name;
	Node *dcl;
	char *n;

	ptrsize = qconst(g, Ptrsz, 'l');
	ty = mktyfunc(Zloc, NULL, 0, mktype(Zloc, Tyvoid));
	ty->type = Tycode;
	name = mknsname(Zloc, "_rt", "abort_oob");
	dcl = mkdecl(Zloc, name, ty);
	dcl->decl.isconst = 1;
	dcl->decl.isextern = 1;
	dcl->decl.isglobl = 1;
	n = asmname(dcl->decl.name, '$');
	htput(globls, dcl, n);

	abortoob = qvar(g, dcl);
}

static Loc binop(Gen *g, Qop op, Node *a, Node *b)
{
	Loc t, l, r;
	char tag;

	tag = qtag(g, exprtype(a));
	t = qtemp(g, tag);
	l = rval(g, a);
	r = rval(g, b);
	out(g, op, t, l, r);
	return t;
}

static Loc binopk(Gen *g, Qop op, Node *n, uvlong k)
{
	Loc t, l, r;
	char tag;

	tag = qtag(g, exprtype(n));
	t = qtemp(g, tag);
	l = rval(g, n);
	r = qconst(g, k, l.tag);
	out(g, op, t, l, r);
	return t;
}

/*static*/ Loc getcode(Gen *g, Node *n)
{
	Loc r, p;

	if (isconstfn(n))
		r = qvar(g, n);
	else {
		r = qtemp(g, 'l');
		p = binopk(g, Qadd, n, Ptrsz);
		out(g, Qloadl, r, p, Zq);
	}
	return r;
}

static Loc slicelen(Gen *g, Node *n, Type *ty)
{
	Loc sl, lp, r;

	ty = tybase(ty);
	sl = rval(g, n);
	lp = qtemp(g, 'l');
	r = qtemp(g, qtag(g, ty));
	out(g, Qadd, lp, sl, ptrsize);
	out(g, loadop(ty), r, lp, Zq);
	return r;
}

Loc cmpop(Gen *g, Op op, Node *ln, Node *rn)
{
	Qop intcmptab[][2] = {
		/* signed */
		[Ole]  = {Qcslew, Qcslel},
		[Olt]  = {Qcsltw, Qcsltl},
		[Ogt]  = {Qcsgtw, Qcsgtl},
		[Oge]  = {Qcsgew, Qcsgel},
		[Oeq]  = {Qceqw,  Qceql},
		[One]  = {Qcnew,  Qcnel},
		/* unsigned */
		[Oule] = {Qculew, Qculel},
		[Oult] = {Qcultw, Qcultl},
		[Ougt] = {Qcugtw, Qcugtl},
		[Ouge] = {Qcugew, Qcugel},
		[Oueq] = {Qceqw,  Qceql},
		[Oune] = {Qcnew,  Qcnel},
	};

	Qop fltcmptab[][2] = {
		[Ofle] = {Qcles, Qcled},
		[Oflt] = {Qclts,  Qcltd},
		[Ofgt] = {Qcgts,  Qcgtd},
		[Ofge] = {Qcges,  Qcged},
		[Ofeq] = {Qceqs,  Qceqd},
		[Ofne] = {Qcnes,  Qcned},
	};

	Loc l, r, t;
	Type *ty;
	Qop qop;

	ty = exprtype(ln);
	l = rval(g, ln);
	r = rval(g, rn);
	t = qtemp(g, qtag(g, ty));
	if (istyfloat(ty))
		qop = fltcmptab[op][l.tag == 'd'];
	else
		qop = intcmptab[op][l.tag == 'l'];

	out(g, qop, t, l, r);
	return t;
}

static Loc intcvt(Gen *g, Loc v, char to, int sz, int sign)
{
	Loc t;
	Qop op;

	static const Qop optab[][2] = {
		[8] = {Qcopy,	Qcopy},
		[4] = {Qextuw,	Qextsw},
		[2] = {Qextuh,	Qextsh},
		[1] = {Qextub,	Qextsb},
	};

	t = qtemp(g, to);
	if (sz == 4 && to == 'w')
		op = Qcopy;
	else
		op = optab[sz][sign];
	out(g, op, t, v, Zq);
	return t;
}

static Loc gencast(Gen *g, Srcloc loc, Loc val, Type *to, Type *from)
{
	Loc a, r;

	r = val;
	/* do the type conversion */
	switch (tybase(to)->type) {
	case Tybool:
	case Tyint8: case Tyint16: case Tyint32: case Tyint64:
	case Tyuint8: case Tyuint16: case Tyuint32: case Tyuint64:
	case Tyint: case Tyuint: case Tychar: case Tybyte:
	case Typtr:
		switch (tybase(from)->type) {
		/* ptr -> slice conversion is disallowed */
		case Tyslice:
			/* FIXME: we should only allow casting to pointers. */
			if (tysize(to) != Ptrsz)
				lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
			r = qtemp(g, 'l');
			out(g, Qloadl, r, val, Zq);
			break;
		case Tyfunc:
			if (to->type != Typtr)
				lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
			a = qtemp(g, 'l');
			r = qtemp(g, 'l');
			out(g, Qadd, a, val, ptrsize);
			out(g, Qloadl, r, a, Zq);
			break;
		/* signed conversions */
		case Tyint8: case Tyint16: case Tyint32: case Tyint64:
		case Tyint:
			r = intcvt(g, val, qtag(g, to), tysize(from), 1);
			break;
		/* unsigned conversions */
		case Tybool:
		case Tyuint8: case Tyuint16: case Tyuint32: case Tyuint64:
		case Tyuint: case Tychar: case Tybyte:
		case Typtr:
			r = intcvt(g, val, qtag(g, to), tysize(from), 0);
			break;
		case Tyflt32: 
			if (tybase(to)->type == Typtr)
				lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
			r = qtemp(g, qtag(g, to));
			out(g, Qstosi, r, val, Zq);
			break;
		case Tyflt64:
			if (tybase(to)->type == Typtr)
				lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
			r = qtemp(g, qtag(g, to));
			out(g, Qdtosi, r, val, Zq);
			break;
		default:
			lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
		}
		break;
	case Tyflt32: case Tyflt64:
		switch (from->type) {
		case Tyint8: case Tyint16: case Tyint32: case Tyint64:
		case Tyuint8: case Tyuint16: case Tyuint32: case Tyuint64:
		case Tyint: case Tyuint: case Tychar: case Tybyte:
			r = qtemp(g, qtag(g, to));
			out(g, Qswtof, r, val, Zq);
			break;
		case Tyflt32: case Tyflt64:
			fprintf(stderr, "flt<->flt casts not supported");
			r = qconst(g, 0, qtag(g, to));
			break;
			break;
		default:
			lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
			break;
		}
		break;
		/* no other destination types are handled as things stand */
	default:
		lfatal(loc, "bad cast from %s to %s", tystr(from), tystr(to));
	}
	return r;
}

static Loc simpcast(Gen *g, Node *val, Type *to)
{
	Loc l;

	l = rval(g, val);
	return gencast(g, val->loc, l, to, exprtype(val));
}


void blit(Gen *g, Loc dst, Loc src, size_t sz, size_t align)
{
	static const Qop ops[][2] = {
		[8] = {Qloadl, Qstorel},
		[4] = {Qloaduw, Qstorew},
		[2] = {Qloaduh, Qstoreh},
		[1] = {Qloadub, Qstoreb},
	};
	Loc l, a, s, d;
	size_t i;

	assert(sz % align == 0);
	s = qtemp(g, 'l');
	d = qtemp(g, 'l');
	l = qtemp(g, 'l');
	a = qconst(g, align, 'l');
	out(g, Qcopy, s, src, Zq);
	out(g, Qcopy, d, dst, Zq);
	for (i = 0; i < sz; i += align) {
		out(g, ops[align][0], l, s, Zq);
		out(g, ops[align][1], Zq, l, d);
		out(g, Qadd, s, s, a);
		out(g, Qadd, d, d, a);
	}
}

Loc assign(Gen *g, Node *dst, Node* src)
{
	Loc d, s;
	Type *ty;

	ty = exprtype(dst);
	d = lval(g, dst);
	s = rval(g, src);
	if (isstacktype(ty))
		blit(g, d, s, tysize(ty), tyalign(ty));
	else 
		out(g, storeop(ty), Zq, s, d);
	return d;
}

/*static*/ void checkidx(Gen *g, Qop op, Loc len, Loc idx)
{
	char ok[128], fail[128];
	Loc oklbl, faillbl;
	Loc inrange;

	genlblstr(ok, sizeof ok, "");
	genlblstr(fail, sizeof fail, "");

	inrange = qtemp(g, 'w');
	oklbl = qlabelstr(g, strdup(fail));
	faillbl = qlabelstr(g, strdup(fail));

	out(g, op, inrange, len, idx);
	out(g, Qjnz, inrange, oklbl, faillbl);
	out(g, Qlabel, faillbl, Zq, Zq);
	ocall(g, abortoob, Zq, Zq, NULL, NULL, 0);
	out(g, Qlabel, oklbl, Zq, Zq);
}

static Loc loadvar(Gen *g, Node *var)
{
	Node *dcl;
	Type *ty;
	Loc r, l;

	dcl = decls[var->expr.did];
	ty = exprtype(var);
	l = qvar(g, dcl);
	if (isstacktype(ty)) {
		r = l;
	} else {
		r = qtemp(g, l.tag);
		out(g, loadop(ty), r, l, Zq);
	}
	return r;
}

static Loc deref(Gen *g, Node *n)
{
	Loc l, r;
	Type *ty;

	ty = exprtype(n);
	l = rval(g, n);
	if (isstacktype(ty))
		return l;
	r = qtemp(g, qtag(g, ty));
	out(g, loadop(ty), r, l, Zq);
	return r;
}

static size_t countargs(Type *t)
{
	size_t n, i;

	n = 0;
	t = tybase(t);
	for (i = 1; i < t->nsub; i++) {
		if (tybase(t->sub[i])->type == Tyvalist)
			break;
		if (tybase(t->sub[i])->type != Tyvoid)
			n++;
	}
	return n;
}

static Type *vatype(Gen *g, Srcloc l, Node **al, size_t na)
{
	Type **t;
	size_t i, nt;

	nt = 0;
	t = NULL;
	for (i = 0; i < na; i++) {
		lappend(&t, &nt, exprtype(al[i]));
	}
	return mktytuple(l, t, nt);
}

static Loc vablob(Gen *g, Srcloc l, Type *t)
{
	Node *d;
	Type *tt, *tl[2];

	tl[0] = mktype(l, Tyuint64);
	tl[1] = t;
	tt = mktytuple(l, tl, 2);
	gentemp(l, tt, &d);
	addlocal(g, d);
	return qvar(g, d);
}

static Loc genvarargs(Gen *g, Srcloc l, Node **al, size_t na, Type **t)
{
	size_t i, sz, a, off;
	Loc va, s, d;
	Type *ty, *tt;
	Blob *b;

	/* genrate varargs */
	sz = Ptrsz;	/* tydesc */
	for (i = 0; i < na; i++) {
		a  = min(size(al[i]), Ptrsz);
		sz = align(sz, a);
		sz += size(al[i]);
	}
	tt = vatype(g, l, al, na);
	b = tydescblob(tt);
	tagreflect(tt);
	va = vablob(g, l, tt);
	off =Ptrsz;
	d = qtemp(g, 'l');
	out(g, Qstorel, Zq, qblob(g, b), va);
	out(g, Qadd, d, va, ptrsize);
	for (i = 0; i < na; i++) {
		ty = exprtype(al[i]);
		off = align(off, tyalign(ty));
		s = rval(g, al[i]);
		d = qtemp(g, 'l');
		out(g, Qadd, d, va, qconst(g, off, 'l'));
		if (isstacktype(ty))
			blit(g, d, s, tysize(ty), tyalign(ty));
		else 
			out(g, storeop(ty), Zq, s, d);
		off += tysize(ty);
	}

	lappend(&g->vatype, &g->nvatype, tt);
	g->typenames = xrealloc(g->typenames, ntypes * sizeof(Type*));
	g->typenames[tt->tid] = strfmt(":t%d", tt->tid);

	*t = tt;
	return va;
}

static Loc gencall(Gen *g, Node *n)
{
	Type **tl, *ft, *vt, *ty;
	size_t nva, na, i;
	Loc env, ret, fn;
	Loc *al;
	Node **va;

	ft = exprtype(n->expr.args[0]);
	al = xalloc(ft->nsub * sizeof(Loc));
	tl = xalloc(ft->nsub * sizeof(Type *));
	va = n->expr.args + (ft->nsub - 1);
	nva = n->expr.nargs - (ft->nsub - 1);

	ret = Zq;
	env = Zq;
	if (tybase(exprtype(n))->type != Tyvoid)
		ret = temp(g, n);

	na = countargs(ft);
	/* +1 to skip past the function */
	for (i = 1; i <= na; i++) {
		ty = exprtype(n->expr.args[i]);
		ty = tybase(ty);
		if (ty->type == Tyvoid) {
			rval(g, n->expr.args[i]);
			continue;
		}
		al[i - 1] = rval(g, n->expr.args[i]);
		tl[i - 1] = ty;
	}
	if (ft->sub[ft->nsub - 1]->type == Tyvalist) {
		al[i - 1] = genvarargs(g, n->loc, va, nva, &vt);
		tl[i - 1] = vt;
		na++;
	}
	fn = rval(g, n->expr.args[0]);

	ocall(g, fn, ret, env, al, tl, na);
	return ret;
}

static void checkbounds(Gen *g, Node *n, Loc len)
{
}

ssize_t memboff(Gen *g, Type *ty, Node *memb)
{
	size_t i;
	size_t off;

	ty = tybase(ty);
	if (ty->type == Typtr)
		ty = tybase(ty->sub[0]);

	assert(ty->type == Tystruct);
	off = 0;
	for (i = 0; i < ty->nmemb; i++) {
		off = alignto(off, decltype(ty->sdecls[i]));
		if (!strcmp(namestr(memb), declname(ty->sdecls[i])))
			return off;
		off += size(ty->sdecls[i]);
	}
	die("bad member in offset");
	return 0;
}

static Loc membaddr(Gen *g, Node *n)
{
	Type *t;
	Loc b, r;
	size_t o;

	t = exprtype(n->expr.args[0]);
	b = rval(g, n->expr.args[0]);
	o = memboff(g, t, n->expr.args[1]);
	r = qtemp(g, 'l');
	out(g, Qadd, r, b, qconst(g, o, 'l'));
	return r;
}

static Loc seqbase(Gen *g, Node *slnode, Node *offnode)
{
	Loc u, v, r;
	Type *ty;
	size_t sz;
	Loc sl, off;

	ty = tybase(exprtype(slnode));
	sl = rval(g, slnode);
	switch (ty->type) {
	case Typtr:	u = sl;	break;
	case Tyarray:	u = sl;	break;
	case Tyslice:	
		u = qtemp(g, 'l');
		out(g, Qloadl, u, sl, Zq);
		break;
	default: 
		die("Unslicable type %s", tystr(ty));
	}
	/* safe: all types we allow here have a sub[0] that we want to grab */
	if (!offnode)
		return u;
	off = ptrsized(g, rval(g, offnode));
	sz = tysize(slnode->expr.type->sub[0]);
	v = qtemp(g, 'l');
	r = qtemp(g, 'l');
	out(g, Qmul, v, off, qconst(g, sz, 'l'));
	out(g, Qadd, r, u, v);
	return r;
}

static Loc genslice(Gen *g, Node *n)
{
	Node *arg, *off, *lim;
	Loc lo, hi, start, sz;
	Loc base, p, len;
	Loc sl, lenp;
	Type *ty;


	arg = n->expr.args[0];
	off = n->expr.args[1];
	lim = n->expr.args[1];
	ty = exprtype(arg);
	sl = qstacktemp(g, n);
	lenp = qtemp(g, 'l');
	out(g, Qadd, lenp, sl, ptrsize);
	/* Special case: Literal arrays with 0 need to be
	have zero base pointers and lengths in order to 
	act as nice initializers */
	if (exprop(arg) == Oarr && arg->expr.nargs == 0) {
		base = qconst(g, 0, 'l');
	} else {
		base = seqbase(g, arg, off);
	}
	lo = rval(g, off);
	if (lo.tag != 'l')
		lo = intcvt(g, lo, 'l', size(off), 1);
	hi = rval(g, lim);
	if (hi.tag != 'l')
		hi = intcvt(g, hi, 'l', size(lim), 1);
	sz = qconst(g, tysize(ty->sub[0]), 'l');
	len = qtemp(g, 'l');
	start = qtemp(g, 'l');

	p = qtemp(g, 'l');
	out(g, Qadd, p, base, lo);
	out(g, Qsub, len, hi, lo);
	out(g, Qmul, start, len, sz);

	checkbounds(g, n, len);
	out(g, Qstorel, Zq, p, sl);
	out(g, Qstorel, Zq, len, lenp);
	return sl;
}

static Loc genucon(Gen *g, Node *n)
{
	Loc u, tag, dst, elt, align;
	size_t sz, al;
	Type *ty;
	Ucon *uc;

	ty = tybase(exprtype(n));
	uc = finducon(ty, n->expr.args[0]);
	if (!uc)
		die("Couldn't find union constructor");

	u = qstacktemp(g, n);
	tag = qconst(g, uc->id, 'w');
	out(g, Qstorew, Zq, tag, u);
	if (!uc->etype)
		return u;

	al = max(4, tyalign(uc->etype));
	sz = tysize(uc->etype);
	dst = qtemp(g, 'l');
	align = qconst(g, al, 'l');
	out(g, Qadd, dst, u, align);

	elt = rval(g, n->expr.args[1]);
	if (isstacktype(uc->etype))
		blit(g, dst, elt, sz, al);
	else
		out(g, storeop(uc->etype), Zq, elt, dst);
	return u;
}

static Blob *strblob(Gen *g, Str s)
{
	Blob **v, *d, *b;
	char buf[128];

	d = mkblobbytes(s.buf, s.len);
	d->lbl = strdup(genlblstr(buf, sizeof buf, ""));
	lappend(&g->blobs, &g->nblobs, d);
	v = xalloc(2*sizeof(Blob*));
	v[0] = mkblobref(d->lbl, 0, 0);
	v[1] = mkblobi(Bti64, s.len);
	b = mkblobseq(v, 2);
	b->lbl = strdup(genlblstr(buf, sizeof buf, ""));
	lappend(&g->blobs, &g->nblobs, b);
	return b;
}

static Loc strlabel(Gen *g, Node *s)
{
	Blob *b;

	b = strblob(g, s->lit.strval);
	return qblob(g, b);
}

static int envcmp(const void *pa, const void *pb)
{
	const Node *a, *b;

	a = *(const Node**)pa;
	b = *(const Node**)pb;
	return b->decl.did - a->decl.did;
}

static Loc code(Gen *g, Node *fn)
{
	Node *d, *n;
	char lbl[128];

	n = mkname(fn->loc, genlblstr(lbl, 128, ""));
	d = mkdecl(fn->loc, n, exprtype(fn));

	d->decl.type = exprtype(fn);
	d->decl.init = fn;
	d->decl.isconst = 1;
	d->decl.isglobl = 1;

	lappend(&file->file.stmts, &file->file.nstmts, d);
	return qvar(g, d);
}

static Loc capture(Gen *g, Node *n, Node *fn)
{
	size_t nenv, nenvt, off, i;
	Loc f, e, s, d, fp;
	Type **envt, *t;
	Node **env, *dcl;

	fp = qstacktemp(g, n);
	env = getclosure(fn->func.scope, &nenv);
	if (env && 0) {
		/* we need these in a deterministic order so that we can
		   put them in the right place both when we use them and
		   when we capture them.  */
		qsort(env, nenv, sizeof(Node*), envcmp);

		/* make the tuple type that will hold the environment */
		envt = NULL;
		nenvt = 0;
		/* reserve space for size */
		lappend(&envt, &nenvt, mktype(n->loc, Tyuint64));
		for (i = 0; i < nenv; i++)
			lappend(&envt, &nenvt, decltype(env[i]));

		gentemp(n->loc, mktytuple(n->loc, envt, nenvt), &dcl);
		e = qstacktemp(g, dcl);
		d = qtemp(g, 'l');
		off = Ptrsz;    /* we start with the size of the env */
		for (i = 0; i < nenv; i++) {
			off = alignto(off, decltype(env[i]));
			t = decltype(env[i]);
			s = qvar(g, env[i]);
			out(g, Qadd, d, e, qconst(g, off, 'l'));
			blit(g, d, s, tysize(t), tyalign(t));
			off += size(env[i]);
		}
		free(env);
		out(g, Qstorel, Zq, qconst(g, off, 'l'), e);
		out(g, Qstorel, Zq, e, fp);
	}
	f = code(g, n);
	out(g, Qadd, fp, fp, qconst(g, Ptrsz, 'l'));
	out(g, Qstorel, Zq, f, fp);
	return fp;
}

static Loc loadlit(Gen *g, Node *e)
{
	Node *n;
	char t;

	t = qtag(g, exprtype(e));
	n = e->expr.args[0];
	switch (n->lit.littype) {
	case Llbl:	return qlabel(g, n);			break;
	case Lstr:	return strlabel(g, n);			break;
	case Lchr:	return qconst(g, n->lit.chrval, t);	break;
	case Lbool:	return qconst(g, n->lit.boolval, t);	break;
	case Lint:	return qconst(g, n->lit.intval, t);	break;
	case Lflt:	return qconst(g, n->lit.fltval, t);	break;
	case Lvoid:	return qconst(g, n->lit.chrval, t);	break;
	case Lfunc:	return capture(g, e, n->lit.fnval);	break;
	}
	die("unreachable");
	return Zq;
}

Loc rval(Gen *g, Node *n)
{
	Node **args, *a;
	Type *ty, *ety;
	size_t i, o, idx;
	Loc r, l;
	Loc d, s;
	Op op;

	r = Zq;
	args = n->expr.args;
	op = exprop(n);
	switch (op) {
	/* arithmetic */
	case Oadd:	r = binop(g, Qadd, args[0], args[1]);	break;
	case Osub:	r = binop(g, Qsub, args[0], args[1]);	break;
	case Omul:	r = binop(g, Qmul, args[0], args[1]);	break;
	case Odiv:	r = binop(g, Qdiv, args[0], args[1]);	break;
	case Omod:	r = binop(g, Qrem, args[0], args[1]);	break;
	case Ofadd:	r = binop(g, Qadd, args[0], args[1]);	break;
	case Ofsub:	r = binop(g, Qsub, args[0], args[1]);	break;
	case Ofmul:	r = binop(g, Qmul, args[0], args[1]);	break;
	case Ofdiv:	r = binop(g, Qdiv, args[0], args[1]);	break;
	case Obor:	r = binop(g, Qor, args[0], args[1]);	break;
	case Oband:	r = binop(g, Qand, args[0], args[1]);	break;
	case Obxor:	r = binop(g, Qxor, args[0], args[1]);	break;
	case Obsl:	r = binop(g, Qshl, args[0], args[1]);	break;
	case Obsr:	
		if (istysigned(exprtype(n)))
			r = binop(g, Qshr, args[0], args[1]);
		else
			r = binop(g, Qshl, args[0], args[1]);
		break;
	case Obnot:	
		r = qtemp(g, qtag(g, exprtype(n)));
		l = qconst(g, ~0, qtag(g, exprtype(n)));
		out(g, Qxor, r, rval(g, args[0]), l);
		break;
	case Oneg:
	case Ofneg:
		r = qtemp(g, qtag(g, exprtype(n)));
		l = qconst(g, 0, qtag(g, exprtype(n)));
		out(g, Qsub, r, l, rval(g, args[0]));
		break;

	/* comparisons */
	case Oeq:	r = cmpop(g, op, args[0], args[1]);	break;
	case One:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ogt:	r = cmpop(g, op, args[0], args[1]);	break;
	case Oge:	r = cmpop(g, op, args[0], args[1]);	break;
	case Olt:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ole:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ofeq:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ofne:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ofgt:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ofge:	r = cmpop(g, op, args[0], args[1]);	break;
	case Oflt:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ofle:	r = cmpop(g, op, args[0], args[1]);	break;
	case Oueq:	r = cmpop(g, op, args[0], args[1]);	break;
	case Oune:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ougt:	r = cmpop(g, op, args[0], args[1]);	break;
	case Ouge:	r = cmpop(g, op, args[0], args[1]);	break;
	case Oult:	r = cmpop(g, op, args[0], args[1]);	break;
	case Oule:	r = cmpop(g, op, args[0], args[1]);	break;

	case Oasn:	r = assign(g, args[0], args[1]);	break;

	case Oslbase:	r = seqbase(g, args[0], args[1]);	break;
	case Osllen:	r = slicelen(g, args[0], exprtype(n));	break;
	case Oslice:	r = genslice(g, n);			break;

	case Ocast:	r = simpcast(g, args[0], exprtype(n));	break;
	case Ocall:	r = gencall(g, n);			break;

	case Ovar:	r = loadvar(g, n);			break;
	case Olit:	r = loadlit(g, n);			break;
	case Osize:	r = qconst(g, size(args[0]), 'l');	break;
	case Ojmp:	out(g, Qjmp, qlabel(g, args[0]), Zq, Zq);	break;
	case Oidx:
		ty = exprtype(n);
		s = seqbase(g, args[0], args[1]);
		if (isstacktype(ty)) {
			r = s;
		} else {
			r = temp(g, n);
			out(g, loadop(ty), r, s, Zq);
		}
		break;
	case Oret:
		ty = tybase(exprtype(args[0]));
		if (ty->type != Tyvoid)
			assign(g, g->retval, args[0]);
		out(g, Qjmp, g->retlbl, Zq, Zq);
		break;

	case Oderef:
		ty = tybase(exprtype(n));
		if (isstacktype(ty)) {
			r = rval(g, args[0]);
		} else {
			r = temp(g, n);
			out(g, loadop(ty), r, rval(g, args[0]), Zq);
		}
		break;
	case Oaddr:
		ty = tybase(exprtype(args[0]));
		if (exprop(n) == Ovar)
			r = qvar(g, n);
		else
			r = lval(g, args[0]);
		break;

	case Ocjmp:
		out(g, Qjnz, rval(g, args[0]), qlabel(g, args[1]), qlabel(g, args[2]));
		break;

	case Outag:
		l = rval(g, args[0]);
		r = qtemp(g, 'w');
		out(g, Qloadw, r, l, Zq);
		break;

	case Oucon:
		r = genucon(g, n);
		break;

	case Ostruct:
		r = qstacktemp(g, n);
		d = r;
		o = 0;
		ety = exprtype(n);
		for (i = 0; i < n->expr.nargs; i++) {
			a = n->expr.args[i];
			ty = exprtype(a);

			s = rval(g, a);
			o = memboff(g, ety, a->expr.idx);
			out(g, Qadd, d, d, qconst(g, o, 'l'));
			if (isstacktype(ty))
				blit(g, d, s, tysize(ty), tyalign(ty));
			else 
				out(g, storeop(ty), Zq, s, d);
		}
		break;

	/* Otup and Oarr have a similar enough structure that this works */
	case Oarr:
	case Otup:
		r = qstacktemp(g, n);
		d = r;
		o = 0;
		ety = exprtype(n);
		for (i = 0; i < n->expr.nargs; i++) {
			a = n->expr.args[i];
			ty = exprtype(a);

			o = alignto(o, ty);
			s = rval(g, a);
			out(g, Qadd, d, d, qconst(g, o, 'l'));
			if (isstacktype(ty))
				blit(g, d, s, tysize(ty), tyalign(ty));
			else 
				out(g, storeop(ty), Zq, s, d);
			o += tysize(ty);
		}
		break;

	case Omemb:
		ty = exprtype(n);
		l = membaddr(g, n);
		if (isstacktype(ty)) {
			r = l;
		} else {
			r = qtemp(g, qtag(g, ty));
			out(g, loadop(ty), r, l, Zq);
		}
		break;
	case Oudata:
		ty = exprtype(n->expr.args[0]);
		s = rval(g, n->expr.args[0]);
		r = qtemp(g, 'l');
		out(g, Qadd, r, s, qconst(g, Ptrsz, 'l'));
		break;

	case Otupget:
		assert(exprop(args[1]) == Olit);
		s = rval(g, args[0]);
		ty = exprtype(n);
		o = 0;
		idx = args[1]->expr.args[0]->lit.intval;
		for (i = 0; i < ty->nsub; i++) {
			o = alignto(o, ty->sub[i]);
			if (i == idx)
				break;
			o += tysize(ty->sub[i]);
		}
		l = qtemp(g, 'l');
		out(g, Qadd, l, s, qconst(g, o, 'l'));
		if (isstacktype(ty)) {
			r = l;
		} else {
			r = qtemp(g, qtag(g, ty));
			out(g, loadop(ty), r, l, Zq);
		}
		break;
	case Olnot:
		ty = exprtype(n);
		l = rval(g, args[0]);
		r = qtemp(g, qtag(g, ty));
		if (r.tag == 'w')
			out(g, Qceqw, r, l, qconst(g, 0, r.tag));
		else
			out(g, Qceql, r, l, qconst(g, 0, r.tag));
		break;
	case Ocallind:
	case Ovjmp:
                die("unimplemented operator %s", opstr[exprop(n)]);
		break;

	case Odead:
	case Oundef:
	case Odef:
		break;

	/* should not be generated by middle end */
	case Oset:
	case Oblit:
	case Otrunc:
	case Ozwiden:
	case Oswiden:
	case Oflt2int:
	case Oint2flt:
	case Oflt2flt:
	case Oclear:
	/* should be dealt with earlier */
	case Olor: case Oland:
	case Oaddeq: case Osubeq: case Omuleq: case Odiveq: case Omodeq:
	case Oboreq: case Obandeq: case Obxoreq: case Obsleq: case Obsreq:
	case Opreinc: case Opredec: case Opostinc: case Opostdec:
	case Obreak: case Ocontinue:
	case Numops: case Ogap:
                die("invalid operator %s: should have been removed", opstr[exprop(n)]);
	case Obad:
		die("bad operator");
		break;
	}
	return r;
}

static Loc lval(Gen *g, Node *n)
{
	Node **args;
	Loc r;

	args = n->expr.args;
	switch (exprop(n)) {
	case Oidx:	r = seqbase(g, args[0], args[1]);	break;
	case Oderef:	r = deref(g, args[0]);	break;
	case Ovar:	r = qvar(g, n);	break;
	case Omemb:	r = membaddr(g, n);	break;
	case Ostruct:	r = rval(g, n);	break;
	case Oucon:	r = rval(g, n);	break;
	case Oarr:	r = rval(g, n);	break;
	case Ogap:	r = temp(g, n);	break;

	/* not actually expressible as lvalues in syntax, but we generate them */
	case Oudata:	r = rval(g, n);	break;
	case Outag:	r = rval(g, n);	break;
	case Otupget:	r = rval(g, n);	break;
	default:
			fatal(n, "%s cannot be an lvalue", opstr[exprop(n)]);
			break;
	}
	return r;
}

void genbb(Gen *g, Cfg *cfg, Bb *bb)
{
	size_t i;

	for (i = 0; i < bb->nlbls; i++)
		out(g, Qlabel, qlabelstr(g, bb->lbls[i]), Zq, Zq);

	for (i = 0; i < bb->nnl; i++) {
		switch (bb->nl[i]->type) {
		case Ndecl:
			break;
		case Nexpr:
			rval(g, bb->nl[i]);
			break;
		default:
			dump(bb->nl[i], stderr);
			die("bad node passsed to simp()");
			break;
		}

	}
}

void spillargs(Gen *g, Node *fn)
{	
	Loc arg, val;
	Type *ty;
	Node *a;
	size_t i;

	g->arg = zalloc(fn->func.nargs * sizeof(Loc));
	for (i = 0; i < fn->func.nargs; i++) {
		a = fn->func.args[i];
		ty = tybase(decltype(a));
		if (ty->type == Tyvoid)
			continue;
		if (isstacktype(ty)) {
			arg = qvar(g, a);
		} else {
			arg = qtemp(g, qtag(g, ty));
			val = qvar(g, a);
			out(g, storeop(ty), Zq, arg, val);
			addlocal(g, a);
		}
		g->arg[i] = arg;
		g->narg++;
	}
}

void declare(Gen *g, Node *n)
{
	size_t align, size;
	char *name;
	Type *ty;

	assert(n);
	if (n->type == Nexpr && exprop(n) == Ovar)
		n = decls[n->expr.did];
	assert(n->type == Ndecl);
	name = declname(n);
	ty = decltype(n);
	align = tyalign(ty);
	size = tysize(ty);
	fprintf(g->f, "\t%%%s =l alloc%zd %zd\n", name, align, size);
}

static const char *insnname[] = {
#define Insn(name) #name,
#include "qbe.def"
#undef Insn
};

void emitloc(Gen *g, Loc l, char *sep)
{
	Node *d;
	char *n;
	char c;
	
	if (!l.tag)
		return;
	switch (l.kind) {
	case Lblob:	fprintf(g->f, "$%s%s", l.blob->lbl, sep);	break;
	case Ltemp:	fprintf(g->f, "%%.%lld%s", l.tmp, sep);	break;
	case Lconst:	fprintf(g->f, "%lld%s", l.cst, sep);	break;
	case Llabel:	fprintf(g->f, "@%s%s", l.lbl, sep);	break;
	case Ldecl:
		d = decls[l.dcl];
		c = d->decl.isglobl ? '$' : '%';
		n = asmname(d->decl.name, c);
		fprintf(g->f, "%s%s", n, sep);
		free(n);
	}
}

void emitinsn(Gen *g, Insn *insn)
{
	char *sep, *t;
	size_t i;

	if (insn->op != Qlabel)
		fprintf(g->f, "\t");
	switch (insn->op) {
	case Qlabel:
		emitloc(g, insn->ret, "");
		break;
	case Qjmp:
		fprintf(g->f, "%s ", insnname[insn->op]);
		emitloc(g, insn->ret, "");
		break;
	case Qjnz:
		fprintf(g->f, "%s ", insnname[insn->op]);
		emitloc(g, insn->ret, ", ");
		emitloc(g, insn->arg[0], ", ");
		emitloc(g, insn->arg[1], "");
		break;
	case Qcall:
		emitloc(g, insn->ret, "");
		if (insn->ret.tag)
			fprintf(g->f, " =%c\t", insn->ret.tag);
		else
			fprintf(g->f, "\t");
		fprintf(g->f, "%s\t", insnname[insn->op]);
		emitloc(g, insn->fn, " ");
		fprintf(g->f, "(");
		for (i = 0; i < insn->nfarg; i++) {
			sep = (i == insn->nfarg - 1) ? "" : ", ";
			t = qtype(g, insn->fty[i]);
			if (!strcmp(t, "b") || !strcmp(t, "h"))
				t = "w";
			fprintf(g->f, "%s ", t);
			emitloc(g, insn->farg[i], sep);
		}
		fprintf(g->f, ")");
		break;
	default:
		emitloc(g, insn->ret, "");
		if (insn->ret.tag)
			fprintf(g->f, "=%c\t", insn->ret.tag);
		else
			fprintf(g->f, "\t");
		fprintf(g->f, "%s\t", insnname[insn->op]);
		sep = insn->arg[1].tag ? ", " : "";
		emitloc(g, insn->arg[0], sep);
		emitloc(g, insn->arg[1], "");
		break;
	}
	fprintf(g->f, "\n");
}

void emitfn(Gen *g, Node *d)
{
	Node *a, *n, *fn;
	Type *ty, *rtype;
	size_t i, arg;
	char *x, *sep, *t, *name;

	n = d->decl.init;
	n = n->expr.args[0];
	fn = n->lit.fnval;

	for (i = 0; i < g->nvatype; i++) {
		ty = g->vatype[i];
		fprintf(g->f, "type :t%d = align %zd { %zd }\n", 
			ty->tid, tyalign(ty), tysize(ty) + Ptrsz);
	}

	rtype = tybase(g->rettype);
	x = isexport(d) ? "export " : "";
	name = asmname(d->decl.name, '$');
	t = (rtype->type == Tyvoid) ? "" : qtype(g, rtype);
	if (!strcmp(t, "b") || !strcmp(t, "h"))
		t = "w";
	fprintf(g->f, "%sfunction %s %s", x, t, name);
	free(name);
	fprintf(g->f, "(");
	arg = 0;
	for (i = 0; i < fn->func.nargs; i++) {
		a = fn->func.args[i];
		ty = tybase(decltype(a));
		if (ty->type != Tyvoid) {
			sep = (arg == g->narg - 1) ? "" : ", ";
			t = qtype(g, ty);
			if (!strcmp(t, "b") || !strcmp(t, "h"))
				t = "w";
			fprintf(g->f, "%s ", t);
			emitloc(g, g->arg[arg++], sep);
		}
	}
	fprintf(g->f, ")\n");
	fprintf(g->f, "{\n");
	fprintf(g->f, "@start\n");
	if (g->retval)
		declare(g, g->retval);
	for (i = 0; i < g->nlocal; i++)
		declare(g, g->local[i]);
	for (i = 0; i < g->ninsn; i++)
		emitinsn(g, &g->insn[i]);
	fprintf(g->f, "}\n");
}

void genfn(Gen *g, Node *dcl)
{
	Node *n, *b, *retdcl;
	size_t i;
	Cfg *cfg;
	Bb *bb;
	Loc r;

	if (dcl->decl.isextern || dcl->decl.isgeneric)
		return;

	n = dcl->decl.init;

	/* set up the simp context */
	/* unwrap to the function body */
	//collectenv(s, n);
	n = n->expr.args[0];
	n = n->lit.fnval;
	b = n->func.body;

	cfg = mkcfg(dcl, b->block.stmts, b->block.nstmts);
	check(cfg);
	if (debugopt['t'] || debugopt['s'])
		dumpcfg(cfg, stdout);

	/* func declaration */
	g->retval = NULL;
	g->rettype = tybase(dcl->decl.type->sub[0]);
	g->retlbl = qlabel(g, genlbl(dcl->loc));
	lfree(&g->vatype, &g->nvatype);

	if (tybase(g->rettype)->type != Tyvoid)
		g->retval = gentemp(dcl->loc, g->rettype, &retdcl);
	spillargs(g, n);
	for (i = 0; i < cfg->nbb; i++) {
		bb = cfg->bb[i];
		if (!bb)
			continue;
		genbb(g, cfg, bb);
	}
	out(g, Qlabel, g->retlbl, Zq, Zq);
	if (g->retval)
		r = rval(g, g->retval);
	else
		r = Zq;
	out(g, Qret, Zq, r, Zq);
	emitfn(g, dcl);
	g->ninsn = 0;
	g->narg = 0;
	free(g->arg);
}

void gendata(Gen *g)
{
	size_t i;

	for (i = 0; i < g->nblobs; i++)
		genblob(g, g->blobs[i]);
}

void gentydesc(Gen *g)
{
	Blob *b;
	Type *ty;
	size_t i;

	for (i = Ntypes; i < ntypes; i++) {
		if (!types[i]->isreflect)
			continue;
		ty = tydedup(types[i]);
		if (ty->isemitted || ty->isimport)
			continue;
		ty->isemitted = 1;
		b = tydescblob(ty);
		b->iscomdat = 1;
		genblob(g, b);
		blobfree(b);
	}
	fprintf(g->f, "\n");
}

void outarray(Gen *g, Type *ty)
{
	size_t sz;

	sz = 0;
	if (ty->asize)
		sz = ty->asize->expr.args[0]->lit.intval;
	outtypebody(g, ty->sub[0]);
	fprintf(g->f, "\t%s %zd,\n", qtype(g, ty->sub[0]), sz);
}

void outstruct(Gen *g, Type *ty)
{
	size_t i;
	Type *mty;

	for (i = 0; i < ty->nmemb; i++) {
		mty = decltype(ty->sdecls[i]);
		outtypebody(g, mty);
	}
}

void outunion(Gen *g, Type *ty)
{
	size_t i;
	Type *mty;
	size_t maxalign;
	size_t maxsize;

	maxalign = 1;
	maxsize = 0;
	for (i = 0; i < ty->nmemb; i++) {
		mty = ty->udecls[i]->etype;
		if (!mty)
			continue;
		if (tyalign(mty) > maxalign)
			maxalign = tyalign(mty);
		if (tysize(mty) > maxsize)
			maxsize = tysize(mty);
	}
	maxsize += align(4, maxalign);
	fprintf(g->f, "%zd\n", maxsize);
}

void outtuple(Gen *g, Type *ty)
{
	size_t i;
	Type *mty;

	for (i = 0; i < ty->nsub; i++) {
		mty = ty->sub[i];
		outtypebody(g, mty);
		fprintf(g->f, "\t%s,\n", qtype(g, mty));
	}
}

void outtypebody(Gen *g, Type *ty)
{
	ty = tydedup(ty);
	switch (ty->type) {
	case Tyvoid:	break;
	case Tybool:	fprintf(g->f, "\tb,\n");	break;
	case Tychar:	fprintf(g->f, "\tw,\n");	break;
	case Tyint8:	fprintf(g->f, "\tb,\n");	break;
	case Tyint16:	fprintf(g->f, "\th,\n");	break;
	case Tyint:	fprintf(g->f, "\tw,\n");	break;
	case Tyint32:	fprintf(g->f, "\tw,\n");	break;
	case Tyint64:	fprintf(g->f, "\tl,\n");	break;
	case Tybyte:	fprintf(g->f, "\tb,\n");	break;
	case Tyuint8:	fprintf(g->f, "\tb,\n");	break;
	case Tyuint16:	fprintf(g->f, "\th,\n");	break;
	case Tyuint:	fprintf(g->f, "\tw,\n");	break;
	case Tyuint32:	fprintf(g->f, "\tw,\n");	break;
	case Tyuint64:	fprintf(g->f, "\tl,\n");	break;
	case Tyflt32:	fprintf(g->f, "\ts,\n");	break;
	case Tyflt64:	fprintf(g->f, "\td,\n");	break;
	case Typtr:	fprintf(g->f, "\tl,\n");	break;
	case Tyslice:	fprintf(g->f, "\tl, l,\n");	break;
	case Tycode:	fprintf(g->f, "\tl,\n");	break;
	case Tyfunc:	fprintf(g->f, "\tl, l,\n");	break;
	case Tyvalist:	fprintf(g->f, "\tl\n");	break;
	case Tyarray:	outarray(g, ty);	break;
	case Tystruct:	outstruct(g, ty);	break;
	case Tytuple:	outtuple(g, ty);	break;
	case Tyunion:	outunion(g, ty);	break;
	case Tyname:	fprintf(g->f, "\t:t%d,\n", ty->tid);	break;

	/* frontend/invalid types */
	case Tyvar:
	case Tybad:
	case Typaram:
	case Tyunres:
	case Tygeneric:
	case Ntypes:
			die("invalid type %s");
			break;
	}
}

static void outstructtype(Gen *g, Type *ty)
{
	size_t i;

	for (i = 0; i < ty->nmemb; i++)
		outqtype(g, decltype(ty->sdecls[i]));
}

static void outtupletype(Gen *g, Type *ty)
{
	size_t i;

	for (i = 0; i < ty->nsub; i++)
		outqtype(g, ty->sub[i]);
}


static void outuniontype(Gen *g, Type *ty)
{
	size_t i;
	Type *mty;

	for (i = 0; i < ty->nmemb; i++) {
		mty = ty->udecls[i]->etype;
		if (!mty)
			continue;
		outqtype(g, mty);
	}
}

static void outqtype(Gen *g, Type *t)
{
	char buf[1024];
	Type *ty;
	Ty tt;

	ty = tydedup(t);
	tt = ty->type;
	if (tt == Tycode || tt == Tyvar || tt == Tyunres || hasparams(ty))
		return;
	if (!istyconcrete(ty))
		return;
	if (ty->vis == Visbuiltin)
		return;
	if (g->typenames[ty->tid]) {
		g->typenames[t->tid] = g->typenames[ty->tid];
		return;
	}

	snprintf(buf, sizeof buf, ":t%d", ty->tid);
	g->typenames[ty->tid] = strdup(buf);

	switch (tt) {
	case Tyarray:	outqtype(g, ty->sub[0]);	break;
	case Tystruct:	outstructtype(g, ty);		break;
	case Tytuple:	outtupletype(g, ty);		break;
	case Tyunion:	outuniontype(g, ty);		break;
	case Tyname:	outqtype(g, ty->sub[0]);	break;
	default:
		break;
	}

	fprintf(g->f, "type %s = align %zd {\n", g->typenames[ty->tid], tyalign(ty));
	if (tt != Tyname)
		outtypebody(g, ty);
	else
		outtypebody(g, ty->sub[0]);
	fprintf(g->f, "}\n\n");
}


static void genqtypes(Gen *g)
{
	size_t i;

	g->typenames = zalloc(ntypes * sizeof(Type *));
	g->typenames[Tyvalist] = ":.vablob";
	fprintf(g->f, "type :.vablob = align 8 { 8 }\n");
	for (i = Ntypes; i < ntypes; i++) {
		outqtype(g, types[i]);
	}
}

static void genglobl(Gen *g, Node *dcl)
{
	char *s, *x;
	Node *e;
	Blob *b;
	Type *t;


	e = dcl->decl.init;
	e = fold(e, 1);
	if (e) {
		dcl->decl.init = e;
		b = mkblobseq(NULL, 0);
		blobrec(g, b, e);
	} else {
		t = decltype(dcl);
		b = mkblobpad(tysize(t));
	}


	s = asmname(dcl->decl.name, '$');
	x = isexport(dcl) ? "export " : "";
	fprintf(g->f, "%sdata %s =  {\n", x, s);
	genblob(g, b);
	fprintf(g->f, "}\n\n");
	free(s);
}

void gen(Node *file, char *out)
{
	Htab *globls;
	Node **locals;
	size_t nlocals;
	Node *n;
	Gen *g;
	size_t i;

	/* generate the code */
	g = zalloc(sizeof(Gen));
	g->f = fopen(out, "w");
	if (!g->f)
		die("Couldn't open fd %s", out);
	/* set up code gen state */
	globls = mkht(varhash, vareq);
	initconsts(g, globls);
	fillglobls(file->file.globls, globls);

	g->globls = mkht(varhash, vareq);

	genqtypes(g);
	pushstab(file->file.globls);
	for (i = 0; i < file->file.nstmts; i++) {
		n = file->file.stmts[i];
		switch (n->type) {
		case Nuse:
			case Nimpl
				/* nothing to do */ :
				break;
		case Ndecl:
			if (n->decl.isgeneric)
				break;
			if (isconstfn(n)) {
				n = flattenfn(n, &locals, &nlocals);
				g->local = locals;
				g->nlocal = nlocals;
				genfn(g, n);
			} else {
				genglobl(g, n);
			}
			break;
		default:
			die("Bad node %s in toplevel", nodestr[n->type]);
			break;
		}
	}
	popstab();
	gendata(g);
	gentydesc(g);
	fclose(g->f);
}