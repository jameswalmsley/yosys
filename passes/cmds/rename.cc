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
#include "kernel/rtlil.h"
#include "kernel/log.h"

static void rename_in_module(RTLIL::Module *module, std::string from_name, std::string to_name)
{
	from_name = RTLIL::escape_id(from_name);
	to_name = RTLIL::escape_id(to_name);

	if (module->count_id(to_name))
		log_cmd_error("There is already an object `%s' in module `%s'.\n", to_name.c_str(), module->name.c_str());

	for (auto &it : module->wires)
		if (it.first == from_name) {
			RTLIL::Wire *wire = it.second;
			log("Renaming wire %s to %s in module %s.\n", wire->name.c_str(), to_name.c_str(), module->name.c_str());
			module->wires.erase(wire->name);
			wire->name = to_name;
			module->add(wire);
			return;
		}

	for (auto &it : module->cells)
		if (it.first == from_name) {
			RTLIL::Cell *cell = it.second;
			log("Renaming cell %s to %s in module %s.\n", cell->name.c_str(), to_name.c_str(), module->name.c_str());
			module->cells.erase(cell->name);
			cell->name = to_name;
			module->add(cell);
			return;
		}

	log_cmd_error("Object `%s' not found!\n", from_name.c_str());
}

struct RenamePass : public Pass {
	RenamePass() : Pass("rename", "rename object in the design") { }
	virtual void help()
	{
		//   |---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|---v---|
		log("\n");
		log("    rename old_name new_name\n");
		log("\n");
		log("Rename the specified object. Note that selection patterns are not supported\n");
		log("by this command.\n");
		log("\n");
		log("\n");
		log("    rename -enumerate [selection]\n");
		log("\n");
		log("Assign short auto-generated names to all selected wires and cells with private\n");
		log("names.\n");
		log("\n");
	}
	virtual void execute(std::vector<std::string> args, RTLIL::Design *design)
	{
		bool flag_enumerate = false;

		size_t argidx;
		for (argidx = 1; argidx < args.size(); argidx++)
		{
			std::string arg = args[argidx];
			if (arg == "-enumerate") {
				flag_enumerate = true;
				continue;
			}
			break;
		}

		if (flag_enumerate)
		{
			extra_args(args, argidx, design);

			for (auto &mod : design->modules)
			{
				int counter = 0;

				RTLIL::Module *module = mod.second;
				if (!design->selected(module))
					continue;

				std::map<RTLIL::IdString, RTLIL::Wire*> new_wires;
				for (auto &it : module->wires) {
					if (it.first[0] == '$' && design->selected(module, it.second))
						do it.second->name = stringf("\\_%d_", counter++);
						while (module->count_id(it.second->name) > 0);
					new_wires[it.second->name] = it.second;
				}
				module->wires.swap(new_wires);

				std::map<RTLIL::IdString, RTLIL::Cell*> new_cells;
				for (auto &it : module->cells) {
					if (it.first[0] == '$' && design->selected(module, it.second))
						do it.second->name = stringf("\\_%d_", counter++);
						while (module->count_id(it.second->name) > 0);
					new_cells[it.second->name] = it.second;
				}
				module->cells.swap(new_cells);
			}
		}
		else
		{
			if (argidx+2 != args.size())
				log_cmd_error("Invalid number of arguments!\n");

			std::string from_name = args[argidx++];
			std::string to_name = args[argidx++];

			if (!design->selected_active_module.empty())
			{
				if (design->modules.count(design->selected_active_module) > 0)
					rename_in_module(design->modules.at(design->selected_active_module), from_name, to_name);
			}
			else
			{
				for (auto &mod : design->modules) {
					if (mod.first == from_name || RTLIL::unescape_id(mod.first) == from_name) {
						to_name = RTLIL::escape_id(to_name);
						log("Renaming module %s to %s.\n", mod.first.c_str(), to_name.c_str());
						RTLIL::Module *module = mod.second;
						design->modules.erase(module->name);
						module->name = to_name;
						design->modules[module->name] = module;
						goto rename_ok;
					}
				}

				log_cmd_error("Object `%s' not found!\n", from_name.c_str());
			rename_ok:;
			}
		}
	}
} RenamePass;
 
