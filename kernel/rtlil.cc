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
#include "kernel/log.h"
#include "frontends/verilog/verilog_frontend.h"
#include <assert.h>
#include <algorithm>

int RTLIL::autoidx = 1;

RTLIL::Const::Const(std::string str) : str(str)
{
	for (size_t i = 0; i < str.size(); i++) {
		unsigned char ch = str[i];
		for (int j = 0; j < 8; j++) {
			bits.push_back((ch & 1) != 0 ? RTLIL::S1 : RTLIL::S0);
			ch = ch >> 1;
		}
	}
}

RTLIL::Const::Const(int val, int width)
{
	for (int i = 0; i < width; i++) {
		bits.push_back((val & 1) != 0 ? RTLIL::S1 : RTLIL::S0);
		val = val >> 1;
	}
}

RTLIL::Const::Const(RTLIL::State bit, int width)
{
	for (int i = 0; i < width; i++)
		bits.push_back(bit);
}

bool RTLIL::Const::operator <(const RTLIL::Const &other) const
{
	if (bits.size() != other.bits.size())
		return bits.size() < other.bits.size();
	for (size_t i = 0; i < bits.size(); i++)
		if (bits[i] != other.bits[i])
			return bits[i] < other.bits[i];
	return false;
}

bool RTLIL::Const::operator ==(const RTLIL::Const &other) const
{
	return bits == other.bits;
}

bool RTLIL::Const::operator !=(const RTLIL::Const &other) const
{
	return bits != other.bits;
}

bool RTLIL::Const::as_bool() const
{
	for (size_t i = 0; i < bits.size(); i++)
		if (bits[i] == RTLIL::S1)
			return true;
	return false;
}

int RTLIL::Const::as_int() const
{
	int ret = 0;
	for (size_t i = 0; i < bits.size() && i < 32; i++)
		if (bits[i] == RTLIL::S1)
			ret |= 1 << i;
	return ret;
}

std::string RTLIL::Const::as_string() const
{
	std::string ret;
	for (size_t i = bits.size(); i > 0; i--)
		switch (bits[i-1]) {
			case S0: ret += "0"; break;
			case S1: ret += "1"; break;
			case Sx: ret += "x"; break;
			case Sz: ret += "z"; break;
			case Sa: ret += "-"; break;
			case Sm: ret += "m"; break;
		}
	return ret;
}

bool RTLIL::Selection::selected_module(RTLIL::IdString mod_name) const
{
	if (full_selection)
		return true;
	if (selected_modules.count(mod_name) > 0)
		return true;
	if (selected_members.count(mod_name) > 0)
		return true;
	return false;
}

bool RTLIL::Selection::selected_whole_module(RTLIL::IdString mod_name) const
{
	if (full_selection)
		return true;
	if (selected_modules.count(mod_name) > 0)
		return true;
	return false;
}

bool RTLIL::Selection::selected_member(RTLIL::IdString mod_name, RTLIL::IdString memb_name) const
{
	if (full_selection)
		return true;
	if (selected_modules.count(mod_name) > 0)
		return true;
	if (selected_members.count(mod_name) > 0)
		if (selected_members.at(mod_name).count(memb_name) > 0)
			return true;
	return false;
}

void RTLIL::Selection::optimize(RTLIL::Design *design)
{
	if (full_selection) {
		selected_modules.clear();
		selected_members.clear();
		return;
	}

	std::vector<RTLIL::IdString> del_list, add_list;

	del_list.clear();
	for (auto mod_name : selected_modules) {
		if (design->modules.count(mod_name) == 0)
			del_list.push_back(mod_name);
		selected_members.erase(mod_name);
	}
	for (auto mod_name : del_list)
		selected_modules.erase(mod_name);

	del_list.clear();
	for (auto &it : selected_members)
		if (design->modules.count(it.first) == 0)
			del_list.push_back(it.first);
	for (auto mod_name : del_list)
		selected_members.erase(mod_name);

	for (auto &it : selected_members) {
		del_list.clear();
		for (auto memb_name : it.second)
			if (design->modules[it.first]->count_id(memb_name) == 0)
				del_list.push_back(memb_name);
		for (auto memb_name : del_list)
			it.second.erase(memb_name);
	}

	del_list.clear();
	add_list.clear();
	for (auto &it : selected_members)
		if (it.second.size() == 0)
			del_list.push_back(it.first);
		else if (it.second.size() == design->modules[it.first]->wires.size() + design->modules[it.first]->memories.size() +
				design->modules[it.first]->cells.size() + design->modules[it.first]->processes.size())
			add_list.push_back(it.first);
	for (auto mod_name : del_list)
		selected_members.erase(mod_name);
	for (auto mod_name : add_list) {
		selected_members.erase(mod_name);
		selected_modules.insert(mod_name);
	}

	if (selected_modules.size() == design->modules.size()) {
		full_selection = true;
		selected_modules.clear();
		selected_members.clear();
	}
}

RTLIL::Design::~Design()
{
	for (auto it = modules.begin(); it != modules.end(); it++)
		delete it->second;
}

void RTLIL::Design::check()
{
#ifndef NDEBUG
	for (auto &it : modules) {
		assert(it.first == it.second->name);
		assert(it.first.size() > 0 && (it.first[0] == '\\' || it.first[0] == '$'));
		it.second->check();
	}
#endif
}

void RTLIL::Design::optimize()
{
	for (auto &it : modules)
		it.second->optimize();
	for (auto &it : selection_stack)
		it.optimize(this);
	for (auto &it : selection_vars)
		it.second.optimize(this);
}

bool RTLIL::Design::selected_module(RTLIL::IdString mod_name) const
{
	if (!selected_active_module.empty() && mod_name != selected_active_module)
		return false;
	if (selection_stack.size() == 0)
		return true;
	return selection_stack.back().selected_module(mod_name);
}

bool RTLIL::Design::selected_whole_module(RTLIL::IdString mod_name) const
{
	if (!selected_active_module.empty() && mod_name != selected_active_module)
		return false;
	if (selection_stack.size() == 0)
		return true;
	return selection_stack.back().selected_whole_module(mod_name);
}

bool RTLIL::Design::selected_member(RTLIL::IdString mod_name, RTLIL::IdString memb_name) const
{
	if (!selected_active_module.empty() && mod_name != selected_active_module)
		return false;
	if (selection_stack.size() == 0)
		return true;
	return selection_stack.back().selected_member(mod_name, memb_name);
}

RTLIL::Module::~Module()
{
	for (auto it = wires.begin(); it != wires.end(); it++)
		delete it->second;
	for (auto it = memories.begin(); it != memories.end(); it++)
		delete it->second;
	for (auto it = cells.begin(); it != cells.end(); it++)
		delete it->second;
	for (auto it = processes.begin(); it != processes.end(); it++)
		delete it->second;
}

RTLIL::IdString RTLIL::Module::derive(RTLIL::Design*, std::map<RTLIL::IdString, RTLIL::Const>)
{
	log_error("Module `%s' is used with parameters but is not parametric!\n", id2cstr(name));
}

void RTLIL::Module::update_auto_wires(std::map<RTLIL::IdString, int>)
{
	log_error("Module `%s' has automatic wires bu no HDL backend to handle it!\n", id2cstr(name));
}

size_t RTLIL::Module::count_id(RTLIL::IdString id)
{
	return wires.count(id) + memories.count(id) + cells.count(id) + processes.count(id);
}

void RTLIL::Module::check()
{
#ifndef NDEBUG
	for (auto &it : wires) {
		assert(it.first == it.second->name);
		assert(it.first.size() > 0 && (it.first[0] == '\\' || it.first[0] == '$'));
		assert(it.second->width >= 0);
		assert(it.second->port_id >= 0);
		for (auto &it2 : it.second->attributes) {
			assert(it2.first.size() > 0 && (it2.first[0] == '\\' || it2.first[0] == '$'));
		}
	}

	for (auto &it : memories) {
		assert(it.first == it.second->name);
		assert(it.first.size() > 0 && (it.first[0] == '\\' || it.first[0] == '$'));
		assert(it.second->width >= 0);
		assert(it.second->size >= 0);
		for (auto &it2 : it.second->attributes) {
			assert(it2.first.size() > 0 && (it2.first[0] == '\\' || it2.first[0] == '$'));
		}
	}

	for (auto &it : cells) {
		assert(it.first == it.second->name);
		assert(it.first.size() > 0 && (it.first[0] == '\\' || it.first[0] == '$'));
		assert(it.second->type.size() > 0 && (it.second->type[0] == '\\' || it.second->type[0] == '$'));
		for (auto &it2 : it.second->connections) {
			assert(it2.first.size() > 0 && (it2.first[0] == '\\' || it2.first[0] == '$'));
			it2.second.check();
		}
		for (auto &it2 : it.second->attributes) {
			assert(it2.first.size() > 0 && (it2.first[0] == '\\' || it2.first[0] == '$'));
		}
		for (auto &it2 : it.second->parameters) {
			assert(it2.first.size() > 0 && (it2.first[0] == '\\' || it2.first[0] == '$'));
		}
	}

	for (auto &it : processes) {
		assert(it.first == it.second->name);
		assert(it.first.size() > 0 && (it.first[0] == '\\' || it.first[0] == '$'));
		// FIXME: More checks here..
	}

	for (auto &it : connections) {
		assert(it.first.width == it.second.width);
		it.first.check();
		it.second.check();
	}

	for (auto &it : attributes) {
		assert(it.first.size() > 0 && (it.first[0] == '\\' || it.first[0] == '$'));
	}
#endif
}

void RTLIL::Module::optimize()
{
	for (auto &it : cells)
		it.second->optimize();
	for (auto &it : processes)
		it.second->optimize();
	for (auto &it : connections) {
		it.first.optimize();
		it.second.optimize();
	}
}

void RTLIL::Module::cloneInto(RTLIL::Module *new_mod) const
{
	new_mod->name = name;
	new_mod->connections = connections;
	new_mod->attributes = attributes;

	for (auto &it : wires)
		new_mod->wires[it.first] = new RTLIL::Wire(*it.second);

	for (auto &it : memories)
		new_mod->memories[it.first] = new RTLIL::Memory(*it.second);

	for (auto &it : cells)
		new_mod->cells[it.first] = new RTLIL::Cell(*it.second);

	for (auto &it : processes)
		new_mod->processes[it.first] = it.second->clone();

	struct RewriteSigSpecWorker
	{
		RTLIL::Module *mod;
		void operator()(RTLIL::SigSpec &sig)
		{
			for (auto &c : sig.chunks)
				if (c.wire != NULL)
					c.wire = mod->wires.at(c.wire->name);
		}
	};

	RewriteSigSpecWorker rewriteSigSpecWorker;
	rewriteSigSpecWorker.mod = new_mod;
	new_mod->rewrite_sigspecs(rewriteSigSpecWorker);
}

RTLIL::Module *RTLIL::Module::clone() const
{
	RTLIL::Module *new_mod = new RTLIL::Module;
	cloneInto(new_mod);
	return new_mod;
}

RTLIL::Wire *RTLIL::Module::new_wire(int width, RTLIL::IdString name)
{
	RTLIL::Wire *wire = new RTLIL::Wire;
	wire->width = width;
	wire->name = name;
	add(wire);
	return wire;
}

void RTLIL::Module::add(RTLIL::Wire *wire)
{
	assert(!wire->name.empty());
	assert(count_id(wire->name) == 0);
	wires[wire->name] = wire;
}

void RTLIL::Module::add(RTLIL::Cell *cell)
{
	assert(!cell->name.empty());
	assert(count_id(cell->name) == 0);
	cells[cell->name] = cell;
}

static bool fixup_ports_compare(const RTLIL::Wire *a, const RTLIL::Wire *b)
{
	if (a->port_id && !b->port_id)
		return true;
	if (!a->port_id && b->port_id)
		return false;

	if (a->port_id == b->port_id)
		return a->name < b->name;
	return a->port_id < b->port_id;
}

void RTLIL::Module::fixup_ports()
{
	std::vector<RTLIL::Wire*> all_ports;

	for (auto &w : wires)
		if (w.second->port_input || w.second->port_output)
			all_ports.push_back(w.second);
		else
			w.second->port_id = 0;

	std::sort(all_ports.begin(), all_ports.end(), fixup_ports_compare);
	for (size_t i = 0; i < all_ports.size(); i++)
		all_ports[i]->port_id = i+1;
}

RTLIL::Wire::Wire()
{
	width = 1;
	start_offset = 0;
	port_id = 0;
	port_input = false;
	port_output = false;
	auto_width = false;
}

RTLIL::Memory::Memory()
{
	width = 1;
	size = 0;
}

void RTLIL::Cell::optimize()
{
	for (auto &it : connections)
		it.second.optimize();
}

RTLIL::SigChunk::SigChunk()
{
	wire = NULL;
	width = 0;
	offset = 0;
}

RTLIL::SigChunk::SigChunk(const RTLIL::Const &data)
{
	wire = NULL;
	this->data = data;
	width = data.bits.size();
	offset = 0;
}

RTLIL::SigChunk::SigChunk(RTLIL::Wire *wire, int width, int offset)
{
	this->wire = wire;
	this->width = width >= 0 ? width : wire->width;
	this->offset = offset;
}

RTLIL::SigChunk::SigChunk(const std::string &str)
{
	wire = NULL;
	data = RTLIL::Const(str);
	width = data.bits.size();
	offset = 0;
}

RTLIL::SigChunk::SigChunk(int val, int width)
{
	wire = NULL;
	data = RTLIL::Const(val, width);
	this->width = data.bits.size();
	offset = 0;
}

RTLIL::SigChunk::SigChunk(RTLIL::State bit, int width)
{
	wire = NULL;
	data = RTLIL::Const(bit, width);
	this->width = data.bits.size();
	offset = 0;
}

RTLIL::SigChunk RTLIL::SigChunk::extract(int offset, int length) const
{
	RTLIL::SigChunk ret;
	if (wire) {
		ret.wire = wire;
		ret.offset = this->offset + offset;
		ret.width = length;
	} else {
		for (int i = 0; i < length; i++)
			ret.data.bits.push_back(data.bits[offset+i]);
		ret.width = length;
	}
	return ret;
}

bool RTLIL::SigChunk::operator <(const RTLIL::SigChunk &other) const
{
	if (wire && other.wire)
		if (wire->name != other.wire->name)
			return wire->name < other.wire->name;
	if (wire != other.wire)
		return wire < other.wire;

	if (offset != other.offset)
		return offset < other.offset;

	if (width != other.width)
		return width < other.width;

	if (data.bits != other.data.bits)
		return data.bits < other.data.bits;
	
	return false;
}

bool RTLIL::SigChunk::operator ==(const RTLIL::SigChunk &other) const
{
	if (wire != other.wire || width != other.width || offset != other.offset)
		return false;
	if (data.bits != other.data.bits)
		return false;
	return true;
}

bool RTLIL::SigChunk::operator !=(const RTLIL::SigChunk &other) const
{
	if (*this == other)
		return false;
	return true;
}

RTLIL::SigSpec::SigSpec()
{
	width = 0;
}

RTLIL::SigSpec::SigSpec(const RTLIL::Const &data)
{
	chunks.push_back(RTLIL::SigChunk(data));
	width = chunks.back().width;
	check();
}

RTLIL::SigSpec::SigSpec(const RTLIL::SigChunk &chunk)
{
	chunks.push_back(chunk);
	width = chunks.back().width;
	check();
}

RTLIL::SigSpec::SigSpec(RTLIL::Wire *wire, int width, int offset)
{
	chunks.push_back(RTLIL::SigChunk(wire, width, offset));
	this->width = chunks.back().width;
	check();
}

RTLIL::SigSpec::SigSpec(const std::string &str)
{
	chunks.push_back(RTLIL::SigChunk(str));
	width = chunks.back().width;
	check();
}

RTLIL::SigSpec::SigSpec(int val, int width)
{
	chunks.push_back(RTLIL::SigChunk(val, width));
	this->width = chunks.back().width;
	check();
}

RTLIL::SigSpec::SigSpec(RTLIL::State bit, int width)
{
	chunks.push_back(RTLIL::SigChunk(bit, width));
	this->width = chunks.back().width;
	check();
}

void RTLIL::SigSpec::expand()
{
	std::vector<RTLIL::SigChunk> new_chunks;
	for (size_t i = 0; i < chunks.size(); i++) {
		assert(chunks[i].data.str.empty());
		for (int j = 0; j < chunks[i].width; j++)
			new_chunks.push_back(chunks[i].extract(j, 1));
	}
	chunks.swap(new_chunks);
	check();
}

void RTLIL::SigSpec::optimize()
{
	for (size_t i = 0; i < chunks.size(); i++) {
		if (chunks[i].wire && chunks[i].wire->auto_width)
			continue;
		if (chunks[i].width == 0)
			chunks.erase(chunks.begin()+i--);
	}
	for (size_t i = 1; i < chunks.size(); i++) {
		RTLIL::SigChunk &ch1 = chunks[i-1];
		RTLIL::SigChunk &ch2 = chunks[i];
		if (ch1.wire && ch1.wire->auto_width)
			continue;
		if (ch2.wire && ch2.wire->auto_width)
			continue;
		if (ch1.wire == ch2.wire) {
			if (ch1.wire != NULL && ch1.offset+ch1.width == ch2.offset) {
				ch1.width += ch2.width;
				goto merged_with_next_chunk;
			}
			if (ch1.wire == NULL && ch1.data.str.empty() == ch2.data.str.empty()) {
				ch1.data.str = ch2.data.str + ch1.data.str;
				ch1.data.bits.insert(ch1.data.bits.end(), ch2.data.bits.begin(), ch2.data.bits.end());
				ch1.width += ch2.width;
				goto merged_with_next_chunk;
			}
		}
		if (0) {
	merged_with_next_chunk:
			chunks.erase(chunks.begin()+i);
			i--;
		}
	}
	check();
}

bool RTLIL::SigChunk::compare(const RTLIL::SigChunk &a, const RTLIL::SigChunk &b)
{
	if (a.wire != b.wire) {
		if (a.wire == NULL || b.wire == NULL)
			return a.wire < b.wire;
		else if (a.wire->name != b.wire->name)
			return a.wire->name < b.wire->name;
		else
			return a.wire < b.wire;
	}
	if (a.offset != b.offset)
		return a.offset < b.offset;
	if (a.width != b.width)
		return a.width < b.width;
	return a.data.bits < b.data.bits;
}

void RTLIL::SigSpec::sort()
{
	expand();
	std::sort(chunks.begin(), chunks.end(), RTLIL::SigChunk::compare);
	optimize();
}

void RTLIL::SigSpec::sort_and_unify()
{
	expand();
	std::sort(chunks.begin(), chunks.end(), RTLIL::SigChunk::compare);
	for (size_t i = 1; i < chunks.size(); i++) {
		RTLIL::SigChunk &ch1 = chunks[i-1];
		RTLIL::SigChunk &ch2 = chunks[i];
		if (!RTLIL::SigChunk::compare(ch1, ch2) && !RTLIL::SigChunk::compare(ch2, ch1)) {
			chunks.erase(chunks.begin()+i);
			width -= chunks[i].width;
			i--;
		}
	}
	optimize();
}

void RTLIL::SigSpec::replace(const RTLIL::SigSpec &pattern, const RTLIL::SigSpec &with)
{
	replace(pattern, with, this);
}

void RTLIL::SigSpec::replace(const RTLIL::SigSpec &pattern, const RTLIL::SigSpec &with, RTLIL::SigSpec *other) const
{
	int pos = 0, restart_pos = 0;
	assert(other == NULL || width == other->width);
	for (size_t i = 0; i < chunks.size(); i++) {
restart:
		const RTLIL::SigChunk &ch1 = chunks[i];
		if (chunks[i].wire != NULL && pos >= restart_pos)
			for (size_t j = 0, poff = 0; j < pattern.chunks.size(); j++) {
				const RTLIL::SigChunk &ch2 = pattern.chunks[j];
				assert(ch2.wire != NULL);
				if (ch1.wire == ch2.wire) {
					int lower = std::max(ch1.offset, ch2.offset);
					int upper = std::min(ch1.offset + ch1.width, ch2.offset + ch2.width);
					if (lower < upper) {
						restart_pos = pos+upper-ch1.offset;
						other->replace(pos+lower-ch1.offset, with.extract(poff+lower-ch2.offset, upper-lower));
						goto restart;
					}
				}
				poff += ch2.width;
			}
		pos += chunks[i].width;
	}
	check();
}

void RTLIL::SigSpec::remove(const RTLIL::SigSpec &pattern)
{
	remove2(pattern, NULL);
}

void RTLIL::SigSpec::remove(const RTLIL::SigSpec &pattern, RTLIL::SigSpec *other) const
{
	RTLIL::SigSpec tmp = *this;
	tmp.remove2(pattern, other);
}

void RTLIL::SigSpec::remove2(const RTLIL::SigSpec &pattern, RTLIL::SigSpec *other)
{
	int pos = 0;
	assert(other == NULL || width == other->width);
	for (size_t i = 0; i < chunks.size(); i++) {
restart:
		const RTLIL::SigChunk &ch1 = chunks[i];
		if (chunks[i].wire != NULL)
			for (size_t j = 0; j < pattern.chunks.size(); j++) {
				const RTLIL::SigChunk &ch2 = pattern.chunks[j];
				assert(ch2.wire != NULL);
				if (ch1.wire == ch2.wire) {
					int lower = std::max(ch1.offset, ch2.offset);
					int upper = std::min(ch1.offset + ch1.width, ch2.offset + ch2.width);
					if (lower < upper) {
						if (other)
							other->remove(pos+lower-ch1.offset, upper-lower);
						remove(pos+lower-ch1.offset, upper-lower);
						if (i == chunks.size())
							break;
						goto restart;
					}
				}
			}
		pos += chunks[i].width;
	}
	check();
}

RTLIL::SigSpec RTLIL::SigSpec::extract(RTLIL::SigSpec pattern, RTLIL::SigSpec *other) const
{
	int pos = 0;
	RTLIL::SigSpec ret;
	pattern.sort_and_unify();
	assert(other == NULL || width == other->width);
	for (size_t i = 0; i < chunks.size(); i++) {
		const RTLIL::SigChunk &ch1 = chunks[i];
		if (chunks[i].wire != NULL)
			for (size_t j = 0; j < pattern.chunks.size(); j++) {
				RTLIL::SigChunk &ch2 = pattern.chunks[j];
				assert(ch2.wire != NULL);
				if (ch1.wire == ch2.wire) {
					int lower = std::max(ch1.offset, ch2.offset);
					int upper = std::min(ch1.offset + ch1.width, ch2.offset + ch2.width);
					if (lower < upper) {
						if (other)
							ret.append(other->extract(pos+lower-ch1.offset, upper-lower));
						else
							ret.append(extract(pos+lower-ch1.offset, upper-lower));
					}
				}
			}
		pos += chunks[i].width;
	}
	ret.check();
	return ret;
}

void RTLIL::SigSpec::replace(int offset, const RTLIL::SigSpec &with)
{
	int pos = 0;
	assert(offset >= 0);
	assert(with.width >= 0);
	assert(offset+with.width <= width);
	remove(offset, with.width);
	for (size_t i = 0; i < chunks.size(); i++) {
		if (pos == offset) {
			chunks.insert(chunks.begin()+i, with.chunks.begin(), with.chunks.end());
			width += with.width;
			check();
			return;
		}
		pos += chunks[i].width;
	}
	assert(pos == offset);
	chunks.insert(chunks.end(), with.chunks.begin(), with.chunks.end());
	width += with.width;
	check();
}

void RTLIL::SigSpec::remove_const()
{
	for (size_t i = 0; i < chunks.size(); i++) {
		if (chunks[i].wire != NULL)
			continue;
		width -= chunks[i].width;
		chunks.erase(chunks.begin() + (i--));
	}
	check();
}

void RTLIL::SigSpec::remove(int offset, int length)
{
	int pos = 0;
	assert(offset >= 0);
	assert(length >= 0);
	assert(offset+length <= width);
	for (size_t i = 0; i < chunks.size(); i++) {
		int orig_width = chunks[i].width;
		if (pos+chunks[i].width > offset && pos < offset+length) {
			int off = offset - pos;
			int len = length;
			if (off < 0) {
				len += off;
				off = 0;
			}
			if (len > chunks[i].width-off)
				len = chunks[i].width-off;
			RTLIL::SigChunk lsb_chunk = chunks[i].extract(0, off);
			RTLIL::SigChunk msb_chunk = chunks[i].extract(off+len, chunks[i].width-off-len);
			if (lsb_chunk.width == 0 && msb_chunk.width == 0) {
				chunks.erase(chunks.begin()+i);
				i--;
			} else if (lsb_chunk.width == 0 && msb_chunk.width != 0) {
				chunks[i] = msb_chunk;
			} else if (lsb_chunk.width != 0 && msb_chunk.width == 0) {
				chunks[i] = lsb_chunk;
			} else if (lsb_chunk.width != 0 && msb_chunk.width != 0) {
				chunks[i] = lsb_chunk;
				chunks.insert(chunks.begin()+i+1, msb_chunk);
				i++;
			} else
				assert(0);
			width -= len;
		}
		pos += orig_width;
	}
	check();
}

RTLIL::SigSpec RTLIL::SigSpec::extract(int offset, int length) const
{
	int pos = 0;
	RTLIL::SigSpec ret;
	assert(offset >= 0);
	assert(length >= 0);
	assert(offset+length <= width);
	for (size_t i = 0; i < chunks.size(); i++) {
		if (pos+chunks[i].width > offset && pos < offset+length) {
			int off = offset - pos;
			int len = length;
			if (off < 0) {
				len += off;
				off = 0;
			}
			if (len > chunks[i].width-off)
				len = chunks[i].width-off;
			ret.chunks.push_back(chunks[i].extract(off, len));
			ret.width += len;
			offset += len;
			length -= len;
		}
		pos += chunks[i].width;
	}
	assert(length == 0);
	ret.check();
	return ret;
}

void RTLIL::SigSpec::append(const RTLIL::SigSpec &signal)
{
	for (size_t i = 0; i < signal.chunks.size(); i++) {
		chunks.push_back(signal.chunks[i]);
		width += signal.chunks[i].width;
	}
	check();
}

bool RTLIL::SigSpec::combine(RTLIL::SigSpec signal, RTLIL::State freeState, bool override)
{
	bool no_collisions = true;

	assert(width == signal.width);
	expand();
	signal.expand();

	for (size_t i = 0; i < chunks.size(); i++) {
		bool self_free = chunks[i].wire == NULL && chunks[i].data.bits[0] == freeState;
		bool other_free = signal.chunks[i].wire == NULL && signal.chunks[i].data.bits[0] == freeState;
		if (!self_free && !other_free) {
			if (override)
				chunks[i] = signal.chunks[i];
			else
				chunks[i] = RTLIL::SigChunk(RTLIL::State::Sx, 1);
			no_collisions = false;
		}
		if (self_free && !other_free)
			chunks[i] = signal.chunks[i];
	}

	optimize();
	return no_collisions;
}

void RTLIL::SigSpec::extend(int width, bool is_signed)
{
	if (this->width > width)
		remove(width, this->width - width);
	
	if (this->width < width) {
		RTLIL::SigSpec padding = this->width > 0 ? extract(this->width - 1, 1) : RTLIL::SigSpec(RTLIL::State::S0);
		if (!is_signed && padding != RTLIL::SigSpec(RTLIL::State::Sx) && padding != RTLIL::SigSpec(RTLIL::State::Sz) &&
				padding != RTLIL::SigSpec(RTLIL::State::Sa) && padding != RTLIL::SigSpec(RTLIL::State::Sm))
			padding = RTLIL::SigSpec(RTLIL::State::S0);
		while (this->width < width)
			append(padding);
	}

	optimize();
}

void RTLIL::SigSpec::check() const
{
	int w = 0;
	for (size_t i = 0; i < chunks.size(); i++) {
		const RTLIL::SigChunk chunk = chunks[i];
		if (chunk.wire == NULL) {
			assert(chunk.offset == 0);
			assert(chunk.data.bits.size() == (size_t)chunk.width);
			assert(chunk.data.str.size() == 0 || chunk.data.str.size()*8 == chunk.data.bits.size());
		} else {
			assert(chunk.offset >= 0);
			assert(chunk.width >= 0);
			assert(chunk.offset + chunk.width <= chunk.wire->width);
			assert(chunk.data.bits.size() == 0);
			assert(chunk.data.str.size() == 0);
		}
		w += chunk.width;
	}
	assert(w == width);
}

bool RTLIL::SigSpec::operator <(const RTLIL::SigSpec &other) const
{
	if (width != other.width)
		return width < other.width;

	RTLIL::SigSpec a = *this, b = other;
	a.optimize();
	b.optimize();

	if (a.chunks.size() != b.chunks.size())
		return a.chunks.size() < b.chunks.size();

	for (size_t i = 0; i < a.chunks.size(); i++)
		if (a.chunks[i] != b.chunks[i])
			return a.chunks[i] < b.chunks[i];

	return false;
}

bool RTLIL::SigSpec::operator ==(const RTLIL::SigSpec &other) const
{
	if (width != other.width)
		return false;

	RTLIL::SigSpec a = *this, b = other;
	a.optimize();
	b.optimize();

	if (a.chunks.size() != b.chunks.size())
		return false;

	for (size_t i = 0; i < a.chunks.size(); i++)
		if (a.chunks[i] != b.chunks[i])
			return false;

	return true;
}

bool RTLIL::SigSpec::operator !=(const RTLIL::SigSpec &other) const
{
	if (*this == other)
		return false;
	return true;
}

bool RTLIL::SigSpec::is_fully_const() const
{
	for (auto it = chunks.begin(); it != chunks.end(); it++)
		if (it->width > 0 && it->wire != NULL)
			return false;
	return true;
}

bool RTLIL::SigSpec::is_fully_def() const
{
	for (auto it = chunks.begin(); it != chunks.end(); it++) {
		if (it->width > 0 && it->wire != NULL)
			return false;
		for (size_t i = 0; i < it->data.bits.size(); i++)
			if (it->data.bits[i] != RTLIL::State::S0 && it->data.bits[i] != RTLIL::State::S1)
				return false;
	}
	return true;
}

bool RTLIL::SigSpec::is_fully_undef() const
{
	for (auto it = chunks.begin(); it != chunks.end(); it++) {
		if (it->width > 0 && it->wire != NULL)
			return false;
		for (size_t i = 0; i < it->data.bits.size(); i++)
			if (it->data.bits[i] != RTLIL::State::Sx && it->data.bits[i] != RTLIL::State::Sz)
				return false;
	}
	return true;
}

bool RTLIL::SigSpec::has_marked_bits() const
{
	for (auto it = chunks.begin(); it != chunks.end(); it++)
		if (it->width > 0 && it->wire == NULL) {
			for (size_t i = 0; i < it->data.bits.size(); i++)
				if (it->data.bits[i] == RTLIL::State::Sm)
					return true;
		}
	return false;
}

bool RTLIL::SigSpec::as_bool() const
{
	assert(is_fully_const());
	SigSpec sig = *this;
	sig.optimize();
	if (sig.width)
		return sig.chunks[0].data.as_bool();
	return false;
}

int RTLIL::SigSpec::as_int() const
{
	assert(is_fully_const());
	SigSpec sig = *this;
	sig.optimize();
	if (sig.width)
		return sig.chunks[0].data.as_int();
	return 0;
}

std::string RTLIL::SigSpec::as_string() const
{
	std::string str;
	for (size_t i = chunks.size(); i > 0; i--) {
		const RTLIL::SigChunk &chunk = chunks[i-1];
		if (chunk.wire != NULL)
			for (int j = 0; j < chunk.width; j++)
				str += "?";
		else
			str += chunk.data.as_string();
	}
	return str;
}

RTLIL::Const RTLIL::SigSpec::as_const() const
{
	assert(is_fully_const());
	SigSpec sig = *this;
	sig.optimize();
	if (sig.width)
		return sig.chunks[0].data;
	return RTLIL::Const();
}

bool RTLIL::SigSpec::match(std::string pattern) const
{
	std::string str = as_string();
	assert(pattern.size() == str.size());

	for (size_t i = 0; i < pattern.size(); i++) {
		if (pattern[i] == ' ')
			continue;
		if (pattern[i] == '*') {
			if (str[i] != 'z' && str[i] != 'x')
				return false;
			continue;
		}
		if (pattern[i] != str[i])
			return false;
	}

	return true;
}

static void sigspec_parse_split(std::vector<std::string> &tokens, const std::string &text, char sep)
{
	size_t start = 0, end = 0;
	while ((end = text.find(sep, start)) != std::string::npos) {
		tokens.push_back(text.substr(start, end - start));
		start = end + 1;
	}
	tokens.push_back(text.substr(start));
}

static int sigspec_parse_get_dummy_line_num()
{
	return 0;
}

bool RTLIL::SigSpec::parse(RTLIL::SigSpec &sig, RTLIL::Module *module, std::string str)
{
	std::vector<std::string> tokens;
	sigspec_parse_split(tokens, str, ',');

	sig = RTLIL::SigSpec();
	for (auto &tok : tokens)
	{
		std::string netname = tok;
		std::string indices;

		if (netname.size() == 0)
			continue;

		if ('0' <= netname[0] && netname[0] <= '9') {
			AST::get_line_num = sigspec_parse_get_dummy_line_num;
			AST::AstNode *ast = VERILOG_FRONTEND::const2ast(netname);
			if (ast == NULL)
				return false;
			sig.append(RTLIL::Const(ast->bits));
			delete ast;
			continue;
		}

		if (netname[0] != '$' && netname[0] != '\\')
			netname = "\\" + netname;

		if (module->wires.count(netname) == 0) {
			size_t indices_pos = netname.size()-1;
			if (indices_pos > 2 && netname[indices_pos] == ']')
			{
				indices_pos--;
				while (indices_pos > 0 && ('0' <= netname[indices_pos] && netname[indices_pos] <= '9')) indices_pos--;
				if (indices_pos > 0 && netname[indices_pos] == ':') {
					indices_pos--;
					while (indices_pos > 0 && ('0' <= netname[indices_pos] && netname[indices_pos] <= '9')) indices_pos--;
				}
				if (indices_pos > 0 && netname[indices_pos] == '[') {
					indices = netname.substr(indices_pos);
					netname = netname.substr(0, indices_pos);
				}
			}
		}

		if (module->wires.count(netname) == 0)
			return false;

		RTLIL::Wire *wire = module->wires.at(netname);
		if (!indices.empty()) {
			std::vector<std::string> index_tokens;
			sigspec_parse_split(index_tokens, indices.substr(1, indices.size()-2), ':');
			if (index_tokens.size() == 1)
				sig.append(RTLIL::SigSpec(wire, 1, atoi(index_tokens.at(0).c_str())));
			else {
				int a = atoi(index_tokens.at(0).c_str());
				int b = atoi(index_tokens.at(1).c_str());
				if (a > b) {
					int tmp = a;
					a = b, b = tmp;
				}
				sig.append(RTLIL::SigSpec(wire, b-a+1, a));
			}
		} else
			sig.append(wire);
	}

	return true;
}

RTLIL::CaseRule::~CaseRule()
{
	for (auto it = switches.begin(); it != switches.end(); it++)
		delete *it;
}

void RTLIL::CaseRule::optimize()
{
	for (auto it : switches)
		it->optimize();
	for (auto &it : compare)
		it.optimize();
	for (auto &it : actions) {
		it.first.optimize();
		it.second.optimize();
	}
}

RTLIL::CaseRule *RTLIL::CaseRule::clone() const
{
	RTLIL::CaseRule *new_caserule = new RTLIL::CaseRule;
	new_caserule->compare = compare;
	new_caserule->actions = actions;
	for (auto &it : switches)
		new_caserule->switches.push_back(it->clone());
	return new_caserule;
}

RTLIL::SwitchRule::~SwitchRule()
{
	for (auto it = cases.begin(); it != cases.end(); it++)
		delete *it;
}

void RTLIL::SwitchRule::optimize()
{
	signal.optimize();
	for (auto it : cases)
		it->optimize();
}

RTLIL::SwitchRule *RTLIL::SwitchRule::clone() const
{
	RTLIL::SwitchRule *new_switchrule = new RTLIL::SwitchRule;
	new_switchrule->signal = signal;
	new_switchrule->attributes = attributes;
	for (auto &it : cases)
		new_switchrule->cases.push_back(it->clone());
	return new_switchrule;
	
}

void RTLIL::SyncRule::optimize()
{
	signal.optimize();
	for (auto &it : actions) {
		it.first.optimize();
		it.second.optimize();
	}
}

RTLIL::SyncRule *RTLIL::SyncRule::clone() const
{
	RTLIL::SyncRule *new_syncrule = new RTLIL::SyncRule;
	new_syncrule->type = type;
	new_syncrule->signal = signal;
	new_syncrule->actions = actions;
	return new_syncrule;
}

RTLIL::Process::~Process()
{
	for (auto it = syncs.begin(); it != syncs.end(); it++)
		delete *it;
}

void RTLIL::Process::optimize()
{
	root_case.optimize();
	for (auto it : syncs)
		it->optimize();
}

RTLIL::Process *RTLIL::Process::clone() const
{
	RTLIL::Process *new_proc = new RTLIL::Process;

	new_proc->name = name;
	new_proc->attributes = attributes;

	RTLIL::CaseRule *rc_ptr = root_case.clone();
	new_proc->root_case = *rc_ptr;
	rc_ptr->switches.clear();
	delete rc_ptr;

	for (auto &it : syncs)
		new_proc->syncs.push_back(it->clone());
	
	return new_proc;
}

