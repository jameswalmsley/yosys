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

#include "kernel/rtlil.h"
#include "kernel/register.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"
#include <string>
#include <assert.h>

#define EDIF_NAME(_id) edif_names(RTLIL::unescape_id(_id)).c_str()

namespace
{
	struct EdifNames
	{
		int counter;
		std::set<std::string> generated_names, used_names;
		std::map<std::string, std::string> name_map;

		EdifNames() : counter(1) { }

		std::string operator()(std::string id)
		{
			if (name_map.count(id) > 0)
				return name_map.at(id);
			if (generated_names.count(id) > 0)
				goto do_rename;
			if (id == "GND" || id == "VCC")
				goto do_rename;

			for (size_t i = 0; i < id.size(); i++) {
				if ('A' <= id[i] && id[i] <= 'Z')
					continue;
				if ('a' <= id[i] && id[i] <= 'z')
					continue;
				if ('0' <= id[i] && id[i] <= '9' && i > 0)
					continue;
				if (id[i] == '_' && i > 0 && i != id.size()-1)
					continue;
				goto do_rename;
			}

			used_names.insert(id);
			return id;

		do_rename:;
			std::string gen_name;
			while (1) {
				gen_name = stringf("id%05d", counter++);
				if (generated_names.count(gen_name) == 0 &&
						used_names.count(gen_name) == 0)
					break;
			}
			generated_names.insert(gen_name);
			name_map[id] = gen_name;
			return stringf("(rename %s \"%s\")", gen_name.c_str(), id.c_str());
		}
	};
}

struct EdifBackend : public Backend {
	EdifBackend() : Backend("edif", "write design to EDIF netlist file") { }
	virtual void help()
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    write_edif [options] [filename]\n");
		log("\n");
		log("Write the current design to an EDIF netlist file.\n");
		log("\n");
		log("    -top top_module\n");
		log("        set the specified module as design top module\n");
		log("\n");
		log("Unfortunately there are different \"flavors\" of the EDIF file format. This\n");
		log("command generates EDIF files for the Xilinx place&route tools. It might be\n");
		log("necessary to make small modifications to this command when a different tool\n");
		log("is targeted.\n");
		log("\n");
	}
	virtual void execute(FILE *&f, std::string filename, std::vector<std::string> args, RTLIL::Design *design)
	{
		log_header("Executing EDIF backend.\n");

		std::string top_module_name;
		std::map<std::string, std::set<std::string>> lib_cell_ports;
		CellTypes ct(design);
		EdifNames edif_names;

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++)
		{
			if (args[argidx] == "-top" && argidx+1 < args.size()) {
				top_module_name = args[++argidx];
				continue;
			}
			break;
		}
		extra_args(f, filename, args, argidx);

		for (auto module_it : design->modules)
		{
			RTLIL::Module *module = module_it.second;
			if (module->get_bool_attribute("\\placeholder"))
				continue;

			if (top_module_name.empty())
				top_module_name = module->name;

			if (module->processes.size() != 0)
				log_error("Found unmapped processes in module %s: unmapped processes are not supported in EDIF backend!\n", RTLIL::id2cstr(module->name));
			if (module->memories.size() != 0)
				log_error("Found munmapped emories in module %s: unmapped memories are not supported in EDIF backend!\n", RTLIL::id2cstr(module->name));

			for (auto cell_it : module->cells)
			{
				RTLIL::Cell *cell = cell_it.second;
				if (!design->modules.count(cell->type) || design->modules.at(cell->type)->get_bool_attribute("\\placeholder")) {
					lib_cell_ports[cell->type];
					for (auto p : cell->connections) {
						if (p.second.width > 1)
							log_error("Found multi-bit port %s on library cell %s.%s (%s): not supported in EDIF backend!\n",
									RTLIL::id2cstr(p.first), RTLIL::id2cstr(module->name), RTLIL::id2cstr(cell->name), RTLIL::id2cstr(cell->type));
						lib_cell_ports[cell->type].insert(p.first);
					}
				}
			}
		}

		if (top_module_name.empty())
			log_error("No module found in design!\n");

		fprintf(f, "(edif %s\n", EDIF_NAME(top_module_name));
		fprintf(f, "  (edifVersion 2 0 0)\n");
		fprintf(f, "  (edifLevel 0)\n");
		fprintf(f, "  (keywordMap (keywordLevel 0))\n");

		fprintf(f, "  (external LIB\n");
		fprintf(f, "    (edifLevel 0)\n");
		fprintf(f, "    (technology (numberDefinition))\n");

		fprintf(f, "    (cell GND\n");
		fprintf(f, "      (cellType GENERIC)\n");
		fprintf(f, "      (view VIEW_NETLIST\n");
		fprintf(f, "        (viewType NETLIST)\n");
		fprintf(f, "        (interface (port G (direction OUTPUT)))\n");
		fprintf(f, "      )\n");
		fprintf(f, "    )\n");

		fprintf(f, "    (cell VCC\n");
		fprintf(f, "      (cellType GENERIC)\n");
		fprintf(f, "      (view VIEW_NETLIST\n");
		fprintf(f, "        (viewType NETLIST)\n");
		fprintf(f, "        (interface (port P (direction OUTPUT)))\n");
		fprintf(f, "      )\n");
		fprintf(f, "    )\n");

		for (auto &cell_it : lib_cell_ports) {
			fprintf(f, "    (cell %s\n", EDIF_NAME(cell_it.first));
			fprintf(f, "      (cellType GENERIC)\n");
			fprintf(f, "      (view VIEW_NETLIST\n");
			fprintf(f, "        (viewType NETLIST)\n");
			fprintf(f, "        (interface\n");
			for (auto &port_it : cell_it.second) {
				const char *dir = "INOUT";
				if (ct.cell_known(cell_it.first)) {
					if (!ct.cell_output(cell_it.first, port_it))
						dir = "INPUT";
					else if (!ct.cell_input(cell_it.first, port_it))
						dir = "OUTPUT";
				}
				fprintf(f, "          (port %s (direction %s))\n", EDIF_NAME(port_it), dir);
			}
			fprintf(f, "        )\n");
			fprintf(f, "      )\n");
			fprintf(f, "    )\n");
		}
		fprintf(f, "  )\n");

		fprintf(f, "  (library DESIGN\n");
		fprintf(f, "    (edifLevel 0)\n");
		fprintf(f, "    (technology (numberDefinition))\n");
		for (auto module_it : design->modules)
		{
			RTLIL::Module *module = module_it.second;
			if (module->get_bool_attribute("\\placeholder"))
				continue;

			SigMap sigmap(module);
			std::map<RTLIL::SigSpec, std::set<std::string>> net_join_db;

			fprintf(f, "    (cell %s\n", EDIF_NAME(module->name));
			fprintf(f, "      (cellType GENERIC)\n");
			fprintf(f, "      (view VIEW_NETLIST\n");
			fprintf(f, "        (viewType NETLIST)\n");
			fprintf(f, "        (interface\n");
			for (auto &wire_it : module->wires) {
				RTLIL::Wire *wire = wire_it.second;
				if (wire->port_id == 0)
					continue;
				const char *dir = "INOUT";
				if (!wire->port_output)
					dir = "INPUT";
				else if (!wire->port_input)
					dir = "OUTPUT";
				if (wire->width == 1) {
					fprintf(f, "          (port %s (direction %s))\n", EDIF_NAME(wire->name), dir);
					RTLIL::SigSpec sig = sigmap(RTLIL::SigSpec(wire));
					net_join_db[sig].insert(stringf("(portRef %s)", EDIF_NAME(wire->name)));
				} else {
					fprintf(f, "          (port (array %s %d) (direction %s))\n", EDIF_NAME(wire->name), wire->width, dir);
					for (int i = 0; i < wire->width; i++) {
						RTLIL::SigSpec sig = sigmap(RTLIL::SigSpec(wire, 1, i));
						net_join_db[sig].insert(stringf("(portRef (member %s %d))", EDIF_NAME(wire->name), i));
					}
				}
			}
			fprintf(f, "        )\n");
			fprintf(f, "        (contents\n");
			fprintf(f, "          (instance GND (viewRef VIEW_NETLIST (cellRef GND (libraryRef LIB))))\n");
			fprintf(f, "          (instance VCC (viewRef VIEW_NETLIST (cellRef VCC (libraryRef LIB))))\n");
			for (auto &cell_it : module->cells) {
				RTLIL::Cell *cell = cell_it.second;
				fprintf(f, "          (instance %s\n", EDIF_NAME(cell->name));
				fprintf(f, "            (viewRef VIEW_NETLIST (cellRef %s%s))", EDIF_NAME(cell->type),
						lib_cell_ports.count(cell->type) > 0 ? " (libraryRef LIB)" : "");
				for (auto &p : cell->parameters)
					if (!p.second.str.empty())
						fprintf(f, "\n            (property %s (string \"%s\"))", EDIF_NAME(p.first), p.second.str.c_str());
					else if (p.second.bits.size() <= 32 && RTLIL::SigSpec(p.second).is_fully_def())
						fprintf(f, "\n            (property %s (integer %u))", EDIF_NAME(p.first), p.second.as_int());
					else {
						std::string hex_string = "";
						for (size_t i = 0; i < p.second.bits.size(); i += 4) {
							int digit_value = 0;
							if (i+0 < p.second.bits.size() && p.second.bits.at(i+0) == RTLIL::State::S1) digit_value |= 1;
							if (i+1 < p.second.bits.size() && p.second.bits.at(i+1) == RTLIL::State::S1) digit_value |= 2;
							if (i+2 < p.second.bits.size() && p.second.bits.at(i+2) == RTLIL::State::S1) digit_value |= 4;
							if (i+3 < p.second.bits.size() && p.second.bits.at(i+3) == RTLIL::State::S1) digit_value |= 8;
							char digit_str[2] = { "0123456789abcdef"[digit_value], 0 };
							hex_string = std::string(digit_str) + hex_string;
						}
						fprintf(f, "\n            (property %s (string \"%s\"))", EDIF_NAME(p.first), hex_string.c_str());
					}
				fprintf(f, ")\n");
				for (auto &p : cell->connections) {
					RTLIL::SigSpec sig = sigmap(p.second);
					sig.expand();
					for (int i = 0; i < sig.width; i++) {
						RTLIL::SigSpec sigbit(sig.chunks.at(i));
						std::string portname = sig.width > 1 ? stringf("%s[%d]", RTLIL::id2cstr(p.first), i) : RTLIL::id2cstr(p.first);
						net_join_db[sigbit].insert(stringf("(portRef %s (instanceRef %s))", edif_names(portname).c_str(), EDIF_NAME(cell->name)));
					}
				}
			}
			for (auto &it : net_join_db) {
				RTLIL::SigSpec sig = it.first;
				sig.optimize();
				log_assert(sig.width == 1);
				if (sig.chunks.at(0).wire == NULL) {
					if (sig.chunks.at(0).data.bits.at(0) != RTLIL::State::S0 && sig.chunks.at(0).data.bits.at(0) != RTLIL::State::S1)
						continue;
				}
				std::string netname = log_signal(sig);
				for (size_t i = 0; i < netname.size(); i++)
					if (netname[i] == ' ' || netname[i] == '\\')
						netname.erase(netname.begin() + i--);
				fprintf(f, "          (net %s (joined\n", edif_names(netname).c_str());
				for (auto &ref : it.second)
					fprintf(f, "            %s\n", ref.c_str());
				if (sig.chunks.at(0).wire == NULL) {
					if (sig.chunks.at(0).data.bits.at(0) == RTLIL::State::S0)
						fprintf(f, "            (portRef G (instanceRef GND))\n");
					if (sig.chunks.at(0).data.bits.at(0) == RTLIL::State::S1)
						fprintf(f, "            (portRef P (instanceRef VCC))\n");
				}
				fprintf(f, "          ))\n");
			}
			fprintf(f, "        )\n");
			fprintf(f, "      )\n");
			fprintf(f, "    )\n");
		}
		fprintf(f, "  )\n");

		fprintf(f, "  (design %s\n", EDIF_NAME(top_module_name));
		fprintf(f, "    (cellRef %s (libraryRef DESIGN))\n", EDIF_NAME(top_module_name));
		fprintf(f, "  )\n");

		fprintf(f, ")\n");
	}
} EdifBackend;

