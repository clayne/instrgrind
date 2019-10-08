/*--------------------------------------------------------------------*/
/*--- instrgrind                                                   ---*/
/*---                                                       main.c ---*/
/*--------------------------------------------------------------------*/

/*
   This file is part of instrgrind, a dynamic instruction tracker tool.

   Copyright (C) 2019, Andrei Rimsa (andrei@cefetmg.br)

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include "global.h"

struct {
	const HChar* instrs_outfile;
} IGD_(clo);

#if defined(VG_BIGENDIAN)
#define IGD_Endness Iend_BE
#elif defined(VG_LITTLEENDIAN)
#define IGD_Endness Iend_LE
#else
#error "Unknown endianness"
#endif

#if INSTR_INC_MODE == 1
  #define USING_INSTR_CALLBACK
#elif INSTR_INC_MODE == 2
  #define USING_INSTR_EXPR
#else
  #error "Invalid instruction increment mode"
#endif

#if defined(USING_INSTR_CALLBACK)
static VG_REGPARM(1)
void IGD_(count_instr)(UniqueInstr* instr) {
	++instr->exec_count;
}

static
void IGD_(add_increment_callback)(IRSB* sbOut, UniqueInstr* instr) {
	addStmtToIRSB(sbOut, IRStmt_Dirty(unsafeIRDirty_0_N(1, "count_instr",
					VG_(fnptr_to_fnentry)(IGD_(count_instr)),
					mkIRExprVec_1(mkIRExpr_HWord((HWord) instr)))));
}
#elif defined(USING_INSTR_EXPR)
static
void IGD_(add_increment_expr)(IRSB* sbOut, IRType tyW, ULong* ptr) {
	IROp addOp;
	IRTemp v1, v2;
	IRExpr* ptrValue;
	IRExpr* oneValue;
	IRExpr* memValue;
	IRExpr* incValue;

	if (tyW == Ity_I32) {
		addOp = Iop_Add32;
		ptrValue = IRExpr_Const(IRConst_U32((HWord) ptr));
		oneValue =  IRExpr_Const(IRConst_U32(1));
	} else {
		addOp = Iop_Add64;
		ptrValue = IRExpr_Const(IRConst_U64((HWord) ptr));
		oneValue = IRExpr_Const(IRConst_U64(1));
	}

	v1 = newIRTemp(sbOut->tyenv, tyW);
	memValue = IRExpr_Load(IGD_Endness, tyW, ptrValue);
	addStmtToIRSB(sbOut, IRStmt_WrTmp(v1, memValue));

	v2 = newIRTemp(sbOut->tyenv, tyW);
	incValue = IRExpr_Binop(addOp, IRExpr_RdTmp(v1), oneValue);
	addStmtToIRSB(sbOut, IRStmt_WrTmp(v2, incValue));

	addStmtToIRSB(sbOut, IRStmt_Store(IGD_Endness, ptrValue, IRExpr_RdTmp(v2)));
}
#endif

static
void IGD_(clo_set_defaults)() {
	IGD_(clo).instrs_outfile = 0;
}

static
Bool IGD_(process_cmd_line_option)(const HChar* arg) {
	if (False) {}
	else if VG_STR_CLO(arg, "--instrs-outfile", IGD_(clo).instrs_outfile) {}
	else
		return False;

	return True;
}

static
void IGD_(print_usage)(void) {
	VG_(printf)(
"\n   instruction options:\n"
"    --instrs-outfile=<f>             Output file with instructions execution count\n"
	);
}

static
void IGD_(print_debug_usage)(void) {
   VG_(printf)(
"    (none)\n"
   );
}

static
void IGD_(post_clo_init)(void) {
	IGD_(init_instrs_pool)();
}

static
IRSB* IGD_(instrument)(VgCallbackClosure* closure, IRSB* sbIn,
         const VexGuestLayout* layout,  const VexGuestExtents* vge,
         const VexArchInfo* archinfo_host, IRType gWordTy, IRType hWordTy) {
	Int i;
	IRSB* sbOut;

	// We don't currently support this case
	if (gWordTy != hWordTy)
		VG_(tool_panic)("host/guest word size mismatch");

	// Set up SB
	sbOut = deepCopyIRSBExceptStmts(sbIn);

	// Copy verbatim any IR preamble preceding the first IMark
	i = 0;
	while (i < sbIn->stmts_used && sbIn->stmts[i]->tag != Ist_IMark) {
		addStmtToIRSB(sbOut, sbIn->stmts[i]);
		i++;
	}

	// Copy instructions to new superblock
	for (/*use current i*/; i < sbIn->stmts_used; i++) {
		IRStmt* st;
		UniqueInstr* instr;

		st = sbIn->stmts[i];
		if (!st || st->tag == Ist_NoOp)
			continue;

		addStmtToIRSB(sbOut, st);

		switch (st->tag) {
			case Ist_IMark:
				instr = IGD_(get_instr)(st->Ist.IMark.addr, st->Ist.IMark.len);
#if defined(USING_INSTR_CALLBACK)
				IGD_(add_increment_callback)(sbOut, instr);
#elif defined(USING_INSTR_EXPR)
				IGD_(add_increment_expr)(sbOut, hWordTy, &(instr->exec_count));
#endif
				break;
			default:
				break;
		}
	}

	return sbOut;
}

static void IGD_(fini)(Int exitcode) {
	if (IGD_(clo).instrs_outfile)
		IGD_(dump_instrs)(IGD_(clo).instrs_outfile);

	IGD_(destroy_instrs_pool)();
}

static void IGD_(pre_clo_init)(void) {
	VG_(details_name)            ("instrgrind");
	VG_(details_version)         (NULL);
	VG_(details_description)     ("a dynamic instruction tracker tool");
	VG_(details_copyright_author)(
	"Copyright (C) 2019, and GNU GPL'd, by Andrei Rimsa.");
	VG_(details_bug_reports_to)  (VG_BUGS_TO);

	VG_(details_avg_translation_sizeB) ( 275 );

	VG_(basic_tool_funcs)        (IGD_(post_clo_init),
								  IGD_(instrument),
								  IGD_(fini));

	VG_(needs_command_line_options)(IGD_(process_cmd_line_option),
                   IGD_(print_usage), IGD_(print_debug_usage));

	IGD_(clo_set_defaults)();
}

VG_DETERMINE_INTERFACE_VERSION(IGD_(pre_clo_init))
