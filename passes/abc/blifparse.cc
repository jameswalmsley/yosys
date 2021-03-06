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

#include "blifparse.h"
#include "kernel/log.h"
#include <stdio.h>
#include <string.h>

RTLIL::Design *abc_parse_blif(FILE *f)
{
	RTLIL::Design *design = new RTLIL::Design;
	RTLIL::Module *module = new RTLIL::Module;

	RTLIL::Const *lutptr = NULL;
	RTLIL::State lut_default_state = RTLIL::State::Sx;

	int port_count = 0;
	module->name = "\\logic";
	design->modules[module->name] = module;

	char buffer[4096];
	int line_count = 0;

	while (1)
	{
		buffer[0] = 0;

		while (1)
		{
			int buffer_len = strlen(buffer);
			while (buffer_len > 0 && (buffer[buffer_len-1] == ' ' || buffer[buffer_len-1] == '\t' ||
					buffer[buffer_len-1] == '\r' || buffer[buffer_len-1] == '\n'))
				buffer[--buffer_len] = 0;

			if (buffer_len == 0 || buffer[buffer_len-1] == '\\') {
				if (buffer[buffer_len-1] == '\\')
					buffer[--buffer_len] = 0;
				line_count++;
				if (fgets(buffer+buffer_len, 4096-buffer_len, f) == NULL)
					goto error;
			} else
				break;
		}

		if (buffer[0] == '#')
			continue;

		if (buffer[0] == '.')
		{
			if (lutptr) {
				for (auto &bit : lutptr->bits)
					if (bit == RTLIL::State::Sx)
						bit = lut_default_state;
				lutptr = NULL;
				lut_default_state = RTLIL::State::Sx;
			}

			char *cmd = strtok(buffer, " \t\r\n");

			if (!strcmp(cmd, ".model"))
				continue;

			if (!strcmp(cmd, ".end"))
				return design;

			if (!strcmp(cmd, ".inputs") || !strcmp(cmd, ".outputs")) {
				char *p;
				while ((p = strtok(NULL, " \t\r\n")) != NULL) {
					RTLIL::Wire *wire = new RTLIL::Wire;
					wire->name = stringf("\\%s", p);
					wire->port_id = ++port_count;
					if (!strcmp(cmd, ".inputs"))
						wire->port_input = true;
					else
						wire->port_output = true;
					module->add(wire);
				}
				continue;
			}

			if (!strcmp(cmd, ".names"))
			{
				char *p;
				RTLIL::SigSpec input_sig, output_sig;
				while ((p = strtok(NULL, " \t\r\n")) != NULL) {
					RTLIL::Wire *wire;
					if (module->wires.count(stringf("\\%s", p)) > 0) {
						wire = module->wires.at(stringf("\\%s", p));
					} else {
						wire = new RTLIL::Wire;
						wire->name = stringf("\\%s", p);
						module->add(wire);
					}
					input_sig.append(wire);
				}
				output_sig = input_sig.extract(input_sig.width-1, 1);
				input_sig = input_sig.extract(0, input_sig.width-1);

				input_sig.optimize();
				output_sig.optimize();

				RTLIL::Cell *cell = new RTLIL::Cell;
				cell->name = NEW_ID;
				cell->type = "$lut";
				cell->parameters["\\WIDTH"] = RTLIL::Const(input_sig.width);
				cell->parameters["\\LUT"] = RTLIL::Const(RTLIL::State::Sx, 1 << input_sig.width);
				cell->connections["\\I"] = input_sig;
				cell->connections["\\O"] = output_sig;
				lutptr = &cell->parameters.at("\\LUT");
				lut_default_state = RTLIL::State::Sx;
				module->add(cell);
				continue;
			}

			goto error;
		}

		if (lutptr == NULL)
			goto error;

		char *input = strtok(buffer, " \t\r\n");
		char *output = strtok(NULL, " \t\r\n");

		if (input == NULL || output == NULL || (strcmp(output, "0") && strcmp(output, "1")))
			goto error;

		int input_len = strlen(input);
		if (input_len > 8)
			goto error;

		for (int i = 0; i < (1 << input_len); i++) {
			for (int j = 0; j < input_len; j++) {
				char c1 = input[j];
				if (c1 != '-') {
					char c2 = (i & (1 << j)) != 0 ? '1' : '0';
					if (c1 != c2)
						goto try_next_value;
				}
			}
			lutptr->bits.at(i) = !strcmp(output, "0") ? RTLIL::State::S0 : RTLIL::State::S1;
		try_next_value:;
		}

		lut_default_state = !strcmp(output, "0") ? RTLIL::State::S1 : RTLIL::State::S0;
	}

error:
	log_error("Syntax error in line %d!\n", line_count);
	// delete design;
	// return NULL;
}

