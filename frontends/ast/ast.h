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
 *  ---
 *
 *  This is the AST frontend library.
 *
 *  The AST frontend library is not a frontend on it's own but provides a
 *  generic abstract syntax tree (AST) abstraction for HDL code and can be
 *  used by HDL frontends. See "ast.h" for an overview of the API and the
 *  Verilog frontend for an usage example.
 *
 */

#ifndef AST_H
#define AST_H

#include "kernel/rtlil.h"
#include <stdint.h>
#include <set>

namespace AST
{
	// all node types, type2str() must be extended
	// whenever a new node type is added here
	enum AstNodeType
	{
		AST_NONE,
		AST_DESIGN,
		AST_MODULE,
		AST_TASK,
		AST_FUNCTION,

		AST_WIRE,
		AST_MEMORY,
		AST_AUTOWIRE,
		AST_PARAMETER,
		AST_LOCALPARAM,
		AST_DEFPARAM,
		AST_PARASET,
		AST_ARGUMENT,
		AST_RANGE,
		AST_CONSTANT,
		AST_CELLTYPE,
		AST_IDENTIFIER,
		AST_PREFIX,

		AST_FCALL,
		AST_TO_SIGNED,
		AST_TO_UNSIGNED,
		AST_CONCAT,
		AST_REPLICATE,
		AST_BIT_NOT,
		AST_BIT_AND,
		AST_BIT_OR,
		AST_BIT_XOR,
		AST_BIT_XNOR,
		AST_REDUCE_AND,
		AST_REDUCE_OR,
		AST_REDUCE_XOR,
		AST_REDUCE_XNOR,
		AST_REDUCE_BOOL,
		AST_SHIFT_LEFT,
		AST_SHIFT_RIGHT,
		AST_SHIFT_SLEFT,
		AST_SHIFT_SRIGHT,
		AST_LT,
		AST_LE,
		AST_EQ,
		AST_NE,
		AST_GE,
		AST_GT,
		AST_ADD,
		AST_SUB,
		AST_MUL,
		AST_DIV,
		AST_MOD,
		AST_POW,
		AST_POS,
		AST_NEG,
		AST_LOGIC_AND,
		AST_LOGIC_OR,
		AST_LOGIC_NOT,
		AST_TERNARY,
		AST_MEMRD,
		AST_MEMWR,

		AST_TCALL,
		AST_ASSIGN,
		AST_CELL,
		AST_PRIMITIVE,
		AST_ALWAYS,
		AST_INITIAL,
		AST_BLOCK,
		AST_ASSIGN_EQ,
		AST_ASSIGN_LE,
		AST_CASE,
		AST_COND,
		AST_DEFAULT,
		AST_FOR,

		AST_GENVAR,
		AST_GENFOR,
		AST_GENIF,
		AST_GENBLOCK,

		AST_POSEDGE,
		AST_NEGEDGE,
		AST_EDGE
	};

	// convert an node type to a string (e.g. for debug output)
	std::string type2str(AstNodeType type);

	// The AST is built using instances of this struct
	struct AstNode
	{
		// this nodes type
		AstNodeType type;

		// the list of child nodes for this node
		std::vector<AstNode*> children;

		// the list of attributes assigned to this node
		std::map<RTLIL::IdString, AstNode*> attributes;
		bool get_bool_attribute(RTLIL::IdString id);

		// node content - most of it is unused in most node types
		std::string str;
		std::vector<RTLIL::State> bits;
		bool is_input, is_output, is_reg, is_signed, range_valid;
		int port_id, range_left, range_right;
		uint32_t integer;

		// this is set by simplify and used during RTLIL generation
		AstNode *id2ast;

		// this is the original sourcecode location that resulted in this AST node
		// it is automatically set by the constructor using AST::current_filename and
		// the AST::get_line_num() callback function.
		std::string filename;
		int linenum;

		// creating and deleting nodes
		AstNode(AstNodeType type = AST_NONE, AstNode *child1 = NULL, AstNode *child2 = NULL);
		AstNode *clone();
		void cloneInto(AstNode *other);
		void delete_children();
		~AstNode();

		// simplify() creates a simpler AST by unrolling for-loops, expanding generate blocks, etc.
		// it also sets the id2ast pointers so that identifier lookups are fast in genRTLIL()
		bool simplify(bool const_fold, bool at_zero, bool in_lvalue, int stage);
		void expand_genblock(std::string index_var, std::string prefix, std::map<std::string, std::string> &name_map);
		void replace_ids(std::map<std::string, std::string> &rules);
		void mem2reg_as_needed_pass1(std::set<AstNode*> &mem2reg_set, std::set<AstNode*> &mem2reg_candidates, bool sync_proc, bool async_proc, bool force_mem2reg);
		void mem2reg_as_needed_pass2(std::set<AstNode*> &mem2reg_set, AstNode *mod, AstNode *block);
		void meminfo(int &mem_width, int &mem_size, int &addr_bits);

		// create a human-readable text representation of the AST (for debugging)
		void dumpAst(FILE *f, std::string indent);
		void dumpVlog(FILE *f, std::string indent);

		// used by genRTLIL() for detecting expression width and sign
		void detectSignWidthWorker(int &width_hint, bool &sign_hint);
		void detectSignWidth(int &width_hint, bool &sign_hint);

		// create RTLIL code for this AST node
		// for expressions the resulting signal vector is returned
		// all generated cell instances, etc. are written to the RTLIL::Module pointed to by AST_INTERNAL::current_module
		RTLIL::SigSpec genRTLIL(int width_hint = -1, bool sign_hint = false);
		RTLIL::SigSpec genWidthRTLIL(int width, RTLIL::SigSpec *subst_from = NULL,  RTLIL::SigSpec *subst_to = NULL);

		// compare AST nodes
		bool operator==(const AstNode &other) const;
		bool operator!=(const AstNode &other) const;
		bool contains(const AstNode *other) const;

		// helper functions for creating AST nodes for constants
		static AstNode *mkconst_int(uint32_t v, bool is_signed, int width = 32);
		static AstNode *mkconst_bits(const std::vector<RTLIL::State> &v, bool is_signed);
	};

	// process an AST tree (ast must point to an AST_DESIGN node) and generate RTLIL code
	void process(RTLIL::Design *design, AstNode *ast, bool dump_ast1 = false, bool dump_ast2 = false, bool dump_vlog = false, bool nolatches = false, bool nomem2reg = false, bool mem2reg = false, bool lib = false, bool noopt = false);

	// parametric modules are supported directly by the AST library
	// therfore we need our own derivate of RTLIL::Module with overloaded virtual functions
	struct AstModule : RTLIL::Module {
		AstNode *ast;
		bool nolatches, nomem2reg, mem2reg, lib, noopt;
		virtual ~AstModule();
		virtual RTLIL::IdString derive(RTLIL::Design *design, std::map<RTLIL::IdString, RTLIL::Const> parameters);
		virtual void update_auto_wires(std::map<RTLIL::IdString, int> auto_sizes);
		virtual RTLIL::Module *clone() const;
	};

	// this must be set by the language frontend before parsing the sources
	// the AstNode constructor then uses current_filename and get_line_num()
	// to initialize the filename and linenum properties of new nodes
	extern std::string current_filename;
	extern void (*set_line_num)(int);
	extern int (*get_line_num)();

	// set set_line_num and get_line_num to internal dummy functions
	// (done by simplify(), AstModule::derive and AstModule::update_auto_wires to control
	// the filename and linenum properties of new nodes not generated by a frontend parser)
	void use_internal_line_num();
}

namespace AST_INTERNAL
{
	// internal state variables
	extern bool flag_dump_ast1, flag_dump_ast2, flag_nolatches, flag_nomem2reg, flag_mem2reg, flag_lib, flag_noopt;
	extern AST::AstNode *current_ast, *current_ast_mod;
	extern std::map<std::string, AST::AstNode*> current_scope;
	extern RTLIL::SigSpec *genRTLIL_subst_from, *genRTLIL_subst_to, ignoreThisSignalsInInitial;
	extern AST::AstNode *current_top_block, *current_block, *current_block_child;
	extern AST::AstModule *current_module;
	struct ProcessGenerator;
}

#endif
