/*
 *  yosys -- Yosys Open SYnthesis Suite
 *
 *  Copyright (C) 2012  Clifford Wolf <clifford@clifford.at>
 *  
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *  
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "kernel/register.h"
#include "kernel/log.h"
#include <stdlib.h>
#include <stdio.h>

struct ProcPass : public Pass {
	ProcPass() : Pass("proc", "translate processes to netlists") { }
	virtual void help()
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    proc [selection]\n");
		log("\n");
		log("This pass calls all the other proc_* passes in the most common order.\n");
		log("\n");
		log("    proc_clean\n");
		log("    proc_rmdead\n");
		log("    proc_arst\n");
		log("    proc_mux\n");
		log("    proc_dff\n");
		log("    proc_clean\n");
		log("\n");
		log("This replaces the processes in the design with multiplexers and flip-flops.\n");
		log("\n");
	}
	virtual void execute(std::vector<std::string> args, RTLIL::Design *design)
	{
		log_header("Executing PROC pass (convert processes to netlists).\n");
		log_push();

		extra_args(args, 1, design);

		Pass::call(design, "proc_clean");
		Pass::call(design, "proc_rmdead");
		Pass::call(design, "proc_arst");
		Pass::call(design, "proc_mux");
		Pass::call(design, "proc_dff");
		Pass::call(design, "proc_clean");

		log_pop();
	}
} ProcPass;
 
