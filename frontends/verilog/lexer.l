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
 *  The Verilog frontend.
 *
 *  This frontend is using the AST frontend library (see frontends/ast/).
 *  Thus this frontend does not generate RTLIL code directly but creates an
 *  AST directly from the Verilog parse tree and then passes this AST to
 *  the AST frontend library.
 *
 *  ---
 *
 *  A simple lexer for Verilog code. Non-preprocessor compiler directives are
 *  handled here. The preprocessor stuff is handled in preproc.cc. Everything
 *  else is left to the bison parser (see parser.y).
 *
 */

%{

#include "kernel/log.h"
#include "verilog_frontend.h"
#include "frontends/ast/ast.h"
#include "parser.tab.h"

using namespace AST;
using namespace VERILOG_FRONTEND;

namespace VERILOG_FRONTEND {
	std::vector<std::string> fn_stack;
	std::vector<int> ln_stack;
	bool lexer_feature_defattr;
}

%}

%option yylineno
%option noyywrap
%option nounput
%option prefix="frontend_verilog_yy"

%x COMMENT
%x STRING
%x SYNOPSYS_TRANSLATE_OFF
%x SYNOPSYS_FLAGS

%%

"`file_push "[^\n]* {
	fn_stack.push_back(current_filename);
	ln_stack.push_back(frontend_verilog_yyget_lineno());
	current_filename = yytext+11;
	frontend_verilog_yyset_lineno(0);
}

"`file_pop"[^\n]*\n {
	current_filename = fn_stack.back();
	fn_stack.pop_back();
	frontend_verilog_yyset_lineno(ln_stack.back());
	ln_stack.pop_back();
}

"`file_notfound "[^\n]* {
	log_error("Can't open include file `%s'!\n", yytext + 15);
}

"`timescale"[ \t]+[^ \t\r\n/]+[ \t]*"/"[ \t]*[^ \t\r\n]* /* ignore timescale directive */

"`yosys_enable_defattr" lexer_feature_defattr = true;
"`yosys_disable_defattr" lexer_feature_defattr = false;

"`"[a-zA-Z_$][a-zA-Z0-9_$]* {
	frontend_verilog_yyerror("Unimplemented compiler directive or undefined macro %s.", yytext);
}

"module"       { return TOK_MODULE; }
"endmodule"    { return TOK_ENDMODULE; }
"function"     { return TOK_FUNCTION; }
"endfunction"  { return TOK_ENDFUNCTION; }
"task"         { return TOK_TASK; }
"endtask"      { return TOK_ENDTASK; }
"parameter"    { return TOK_PARAMETER; }
"localparam"   { return TOK_LOCALPARAM; }
"defparam"     { return TOK_DEFPARAM; }
"assign"       { return TOK_ASSIGN; }
"always"       { return TOK_ALWAYS; }
"initial"      { return TOK_INITIAL; }
"begin"	       { return TOK_BEGIN; }
"end"          { return TOK_END; }
"if"           { return TOK_IF; }
"else"         { return TOK_ELSE; }
"for"          { return TOK_FOR; }
"posedge"      { return TOK_POSEDGE; }
"negedge"      { return TOK_NEGEDGE; }
"or"           { return TOK_OR; }
"case"         { return TOK_CASE; }
"casex"        { return TOK_CASEX; }
"casez"        { return TOK_CASEZ; }
"endcase"      { return TOK_ENDCASE; }
"default"      { return TOK_DEFAULT; }
"generate"     { return TOK_GENERATE; }
"endgenerate"  { return TOK_ENDGENERATE; }

"input"   { return TOK_INPUT; }
"output"  { return TOK_OUTPUT; }
"inout"   { return TOK_INOUT; }
"wire"    { return TOK_WIRE; }
"reg"     { return TOK_REG; }
"integer" { return TOK_INTEGER; }
"signed"  { return TOK_SIGNED; }
"genvar"  { return TOK_GENVAR; }

[0-9]+ {
	frontend_verilog_yylval.string = new std::string(yytext);
	return TOK_CONST;
}

[0-9]*[ \t]*\'s?[bodh][ \t\r\n]*[0-9a-fA-FzxZX?_]+ {
	frontend_verilog_yylval.string = new std::string(yytext);
	return TOK_CONST;
}

\"		{ BEGIN(STRING); }
<STRING>\\.	{ yymore(); }
<STRING>\"	{
	BEGIN(0);
	char *yystr = strdup(yytext);
	yystr[strlen(yytext) - 1] = 0;
	int i = 0, j = 0;
	while (yystr[i]) {
		if (yystr[i] == '\\' && yystr[i + 1]) {
			i++;
			if (yystr[i] == 'n')
				yystr[i] = '\n';
			else if (yystr[i] == 't')
				yystr[i] = '\t';
			else if ('0' <= yystr[i] && yystr[i] <= '7') {
				yystr[i] = yystr[i] - '0';
				if ('0' <= yystr[i + 1] && yystr[i + 1] <= '7') {
					yystr[i + 1] = yystr[i] * 8 + yystr[i + 1] - '0';
					i++;
				}
				if ('0' <= yystr[i + 1] && yystr[i + 1] <= '7') {
					yystr[i + 1] = yystr[i] * 8 + yystr[i + 1] - '0';
					i++;
				}
			}
		}
		yystr[j++] = yystr[i++];
	}
	yystr[j] = 0;
	frontend_verilog_yylval.string = new std::string(yystr);
	free(yystr);
	return TOK_STRING;
}
<STRING>.	{ yymore(); }

and|nand|or|nor|xor|xnor|not|buf|bufif0|bufif1|notif0|notif1 {
	frontend_verilog_yylval.string = new std::string(yytext);
	return TOK_PRIMITIVE;
}

supply0 { return TOK_SUPPLY0; }
supply1 { return TOK_SUPPLY1; }

"$"(display|time|stop|finish) {
	frontend_verilog_yylval.string = new std::string(yytext);
	return TOK_ID;
}

"$signed"   { return TOK_TO_SIGNED; }
"$unsigned" { return TOK_TO_UNSIGNED; }

[a-zA-Z_$][a-zA-Z0-9_$]* {
	frontend_verilog_yylval.string = new std::string(std::string("\\") + yytext);
	return TOK_ID;
}

"/*"[ \t]*synopsys[ \t]*translate_off[ \t]*"*/" {
	log("Warning: Found one of those horrible `synopsys translate_off' comments.\n");
	log("It is strongly suggested to use `ifdef constructs instead!\n");
	BEGIN(SYNOPSYS_TRANSLATE_OFF);
}
<SYNOPSYS_TRANSLATE_OFF>.    /* ignore synopsys translate_off body */
<SYNOPSYS_TRANSLATE_OFF>\n   /* ignore synopsys translate_off body */
<SYNOPSYS_TRANSLATE_OFF>"/*"[ \t]*"synopsys"[ \t]*"translate_on"[ \t]*"*/" { BEGIN(0); }

"/*"[ \t]*"synopsys"[ \t]+ {
	BEGIN(SYNOPSYS_FLAGS);
}
<SYNOPSYS_FLAGS>full_case {
	log("Warning: Found one of those horrible `synopsys full_case' comments.\n");
	log("It is strongly suggested to use verilog x-values and default branches instead!\n");
	return TOK_SYNOPSYS_FULL_CASE;
}
<SYNOPSYS_FLAGS>parallel_case {
	log("Warning: Found one of those horrible `synopsys parallel_case' comments.\n");
	log("It is strongly suggested to use verilog `parallel_case' attributes instead!\n");
	return TOK_SYNOPSYS_PARALLEL_CASE;
}
<SYNOPSYS_FLAGS>. /* ignore everything else */
<SYNOPSYS_FLAGS>"*/" { BEGIN(0); }

"\\"[^ \t\r\n]+ {
	frontend_verilog_yylval.string = new std::string(yytext);
	return TOK_ID;
}

"(*" { return ATTR_BEGIN; }
"*)" { return ATTR_END; }

"{*"  { if (lexer_feature_defattr) return DEFATTR_BEGIN; else REJECT; }
"*}"  { if (lexer_feature_defattr) return DEFATTR_END; else REJECT; }

"**" { return OP_POW; }
"||" { return OP_LOR; }
"&&" { return OP_LAND; }
"==" { return OP_EQ; }
"!=" { return OP_NE; }
"<=" { return OP_LE; }
">=" { return OP_GE; }

"===" { return OP_EQ; }
"!==" { return OP_NE; }

"~&" { return OP_NAND; }
"~|" { return OP_NOR;  }
"~^" { return OP_XNOR; }
"^~" { return OP_XNOR; }

"<<"  { return OP_SHL; }
">>"  { return OP_SHR; }
"<<<" { return OP_SSHL; }
">>>" { return OP_SSHR; }

"/*" { BEGIN(COMMENT); }
<COMMENT>.    /* ignore comment body */
<COMMENT>\n   /* ignore comment body */
<COMMENT>"*/" { BEGIN(0); }

[ \t\r\n]		/* ignore whitespaces */
\\[\r\n]		/* ignore continuation sequence */
"//"[^\r\n]*		/* ignore one-line comments */
"#"[$a-zA-Z_0-9\.]+	/* ignore simulation timings */

. { return *yytext; }

%%

// this is a hack to avoid the 'yyinput defined but not used' error msgs
void *frontend_verilog_avoid_input_warnings() {
	return (void*)&yyinput;
}

