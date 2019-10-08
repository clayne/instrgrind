/*--------------------------------------------------------------------*/
/*--- instrgrind                                                   ---*/
/*---                                                  smarthash.c ---*/
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

#if SMART_LIST_MODE == 1
  #define USING_CHAIN_SMART_LIST
#elif SMART_LIST_MODE == 2
  #define USING_REALLOC_SMART_LIST
#else
  #error "Invalid smart list mode"
#endif

typedef struct _SmartNode	SmartNode;
struct _SmartNode {
	void** list;

#ifdef USING_CHAIN_SMART_LIST
	Int size;
	SmartNode* next;
#endif
};

struct _SmartSeek {
	SmartList* slist;
	Int index;

#ifdef USING_CHAIN_SMART_LIST
	SmartNode** current;
#endif
};

struct _SmartList {
	Int elements;
	Int size;
	Bool fixed;
	Float growth_rate;
	SmartNode* data;
};

static
void grow_smart_list(SmartList* slist) {
	IGD_ASSERT(slist != 0);
	IGD_ASSERT(slist->data != 0);

	if (slist->fixed)
		tl_assert("Not allowed to enlarge this smart list.");

	IGD_DEBUG(3, "[smartlist] Growing smart list from: %d to ", slist->size);

#ifdef USING_CHAIN_SMART_LIST
	{
		SmartNode** snode;

		Int last_size;
		Int accumulated_size;

		accumulated_size = 0;
		snode = &(slist->data);
		while (*snode) {
			last_size = (*snode)->size;
			accumulated_size += last_size;

			snode = &((*snode)->next);
		}

		// Ensure that everything is safe so far.
		IGD_ASSERT(slist->size == accumulated_size);

		// Grow the list.
		last_size = (Int) (last_size * slist->growth_rate);
		IGD_ASSERT(last_size > 0);
		slist->size += last_size;

		IGD_ASSERT(slist->size > 0);
		IGD_DEBUG(3, "%u\n", slist->size);

		*snode = (SmartNode*) IGD_MALLOC("igd.smartlist.gsl.1", sizeof(SmartNode));
		(*snode)->size = last_size;
		(*snode)->list = (void**) IGD_MALLOC("igd.smartlist.gsl.2", (last_size * sizeof(void*)));
		VG_(memset)((*snode)->list, 0, (last_size * sizeof(void*)));
		(*snode)->next = 0;
	}
#else
	{
		Int new_size = (Int) (slist->size * slist->growth_rate);
		IGD_ASSERT(new_size > slist->size);
		IGD_DEBUG(3, "%d\n", new_size);

		slist->data->list = IGD_REALLOC("igd.smartlist.gsl.1",
				slist->data->list, (new_size * sizeof(void*)));
		VG_(memset)((slist->data->list + slist->size), 0,
				((new_size - slist->size) * sizeof(void*)));
		slist->size = new_size;
	}
#endif
}

static
SmartList* create_smart_list(Int size, Bool fixed) {
	SmartList* slist;

	IGD_ASSERT(size > 0);

	IGD_DEBUG(3, "[smartlist] new smart list\n");

	slist = (SmartList*) IGD_MALLOC("igd.smartlist.nsl.1", sizeof(SmartList));
	slist->elements = 0;
	slist->size = size;
	slist->fixed = fixed;
	slist->growth_rate = 2.0f; // default: double the list.
	slist->data = (SmartNode*) IGD_MALLOC("igd.smartlist.nsl.2", sizeof(SmartNode));

#ifdef USING_CHAIN_SMART_LIST
	slist->data->list = (void**) IGD_MALLOC("igd.smartlist.nsl.3", (size * sizeof(void*)));
	VG_(memset)(slist->data->list, 0, (size * sizeof(void*)));
	slist->data->size = size;
	slist->data->next = 0;
#else
	slist->data->list = (void**) IGD_MALLOC("igd.smartlist.nsl.3", (size * sizeof(void*)));
	VG_(memset)(slist->data->list, 0, (size * sizeof(void*)));
#endif

	return slist;
}

SmartList* IGD_(new_smart_list)(Int size) {
	return create_smart_list(size, False);
}

SmartList* IGD_(new_fixed_smart_list)(Int size) {
	return create_smart_list(size, True);
}

SmartList* IGD_(clone_smart_list)(SmartList* slist) {
	Int i;
	SmartList* new_slist;

	IGD_ASSERT(slist != 0);

	new_slist = create_smart_list(slist->size, slist->fixed);
	for (i = 0; i < slist->size; i++) {
		void* element;

		if ((element = IGD_(smart_list_at)(slist, i)))
			IGD_(smart_list_set)(new_slist, i, element);
	}

	return new_slist;
}

void IGD_(delete_smart_list)(SmartList* slist) {
	SmartNode* snode;

	IGD_ASSERT(slist != 0);

	// We can only delete a smart list with no elements.
	// If the list is not clean, you must explicit call
	// a function to clean it.
	IGD_ASSERT(slist->elements == 0);

	IGD_DEBUG(3, "[smartlist] delete smart list\n");

	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		SmartNode* tmp = snode->next;

		IGD_DATA_FREE(snode->list, (snode->size * sizeof(void*)));
		IGD_DATA_FREE(snode, sizeof(SmartNode));

		snode = tmp;
	}
#else
	IGD_DATA_FREE(snode->list, (slist->size * sizeof(void*)));
	IGD_DATA_FREE(snode, sizeof(SmartNode));
#endif

	IGD_DATA_FREE(slist, sizeof(SmartList));
}

void IGD_(smart_list_clear)(SmartList* slist, void (*remove_element)(void*)) {
	SmartNode* snode;

	IGD_ASSERT(slist != 0);

	IGD_DEBUG(3, "[smartlist] clean smart list%s\n", (remove_element ? " with data" : ""));

	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		SmartNode* tmp = snode->next;
		if (remove_element) {
			Int i;
			for (i = 0; i < snode->size; i++) {
				if (snode->list[i]) {
					remove_element(snode->list[i]);
					snode->list[i] = 0;
				}
			}
		} else {
			VG_(memset)(snode->list, 0, (snode->size * sizeof(void*)));
		}

		snode = tmp;
	}
#else
	if (remove_element) {
		Int i;
		for (i = 0; i < slist->size; i++) {
			if (snode->list[i])
				remove_element(snode->list[i]);

			snode->list[i] = 0;
		}
	} else {
		VG_(memset)(snode->list, 0, (slist->size * sizeof(void*)));
	}
#endif

	slist->elements = 0;
}

Int IGD_(smart_list_size)(SmartList* slist) {
	IGD_ASSERT(slist != 0);
	return slist->size;
}

Int IGD_(smart_list_count)(SmartList* slist) {
	IGD_ASSERT(slist != 0);
	return slist->elements;
}

Bool IGD_(smart_list_is_empty)(SmartList* slist) {
	IGD_ASSERT(slist != 0);
	return slist->elements == 0;
}

void* IGD_(smart_list_at)(SmartList* slist, Int index) {
	SmartNode* snode;

	IGD_ASSERT(slist != 0);
	IGD_ASSERT(index >= 0);

	// If the index is out of bounds, return not found.
	if (index < 0 || index >= slist->size)
		return 0;

	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		if (index < snode->size)
			return snode->list[index];

		index -= snode->size;
		snode = snode->next;
	}

	VG_(tool_panic)("instrgrind: unable to get smart list element");
	return 0;
#else
	return snode->list[index];
#endif
}

void* IGD_(smart_list_head)(SmartList* slist) {
	IGD_ASSERT(slist != 0);
	IGD_ASSERT(IGD_(smart_list_count)(slist) > 0);

	return IGD_(smart_list_at)(slist, 0);
}

void* IGD_(smart_list_tail)(SmartList* slist) {
	Int last;

	IGD_ASSERT(slist != 0);

	last = IGD_(smart_list_count)(slist) - 1;
	IGD_ASSERT(last >= 0);

	return IGD_(smart_list_at)(slist, last);
}

void IGD_(smart_list_set)(SmartList* slist, Int index, void* value) {
	SmartNode* snode;

	IGD_ASSERT(slist != 0);
	IGD_ASSERT(index >= 0);

	// Grow the smart list if the size is smaller than the index.
	while (index >= slist->size)
		grow_smart_list(slist);

	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		if (index < snode->size) {
			if (snode->list[index])
				slist->elements--;

			snode->list[index] = value;

			if (value)
				slist->elements++;

			return;
		}

		index -= snode->size;
		snode = snode->next;
	}

	VG_(tool_panic)("instrgrind: unable to set smart list element");
#else
	if (snode->list[index])
		slist->elements--;

	snode->list[index] = value;

	if (value)
		slist->elements++;
#endif
}

void IGD_(smart_list_del)(SmartList* slist, Int index, Bool remove_contents) {
	SmartNode* snode;

	IGD_ASSERT(slist != 0);
	IGD_ASSERT(index >= 0 && index < slist->size);

	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		if (index < snode->size) {
			if (snode->list[index]) {
				if (remove_contents)
					IGD_FREE(snode->list[index]);

				snode->list[index] = 0;
				slist->elements--;
			}

			return;
		}

		index -= snode->size;
		snode = snode->next;
	}

	VG_(tool_panic)("instrgrind: unable to del smart list element");
#else
	if (snode->list[index]) {
		if (remove_contents)
			IGD_FREE(snode->list[index]);

		snode->list[index] = 0;
		slist->elements--;
	}
#endif
}

void IGD_(smart_list_add)(SmartList* slist, void* value) {
	SmartNode* snode;

	IGD_ASSERT(slist != 0);

	// If we are adding a new value, let it not be empty.
	IGD_ASSERT(value != 0);

	// If the list is full, grow it.
	if (slist->elements == slist->size)
		grow_smart_list(slist);

	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	{
		Int index = slist->elements;
		while (snode) {
			if (index < snode->size) {
				IGD_ASSERT(snode->list[index] == 0);
				snode->list[index] = value;
				slist->elements++;

				return;
			}

			index -= snode->size;
			snode = snode->next;
		}

		VG_(tool_panic)("instrgrind: unable to add smart list element");
	}
#else
	IGD_ASSERT(snode->list[slist->elements] == 0);
	snode->list[slist->elements++] = value;
#endif
}

void IGD_(smart_list_copy)(SmartList* dst, SmartList* src) {
	Int i, size;

	IGD_ASSERT(dst != 0);
	IGD_ASSERT(src != 0);

	size = IGD_(smart_list_count)(src);
	for (i = 0; i < size; i++)
		IGD_(smart_list_set)(dst, i, IGD_(smart_list_at)(src, i));

	size = IGD_(smart_list_count)(dst);
	for (; i < size; i++)
		IGD_(smart_list_set)(dst, i, 0);
}

void IGD_(smart_list_forall)(SmartList* slist, Bool (*func)(void*, void*), void* arg) {
	Int index, count;
	SmartNode* snode;

	IGD_ASSERT(slist != 0);
	IGD_ASSERT(func != 0);

	// Fast check if the list is empty.
	if (slist->elements == 0)
		return;

	snode = slist->data;

	count = slist->elements;
#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		index = 0;
		while (count > 0 && index < snode->size) {
			if (snode->list[index]) {
				if ((*func)(snode->list[index], arg)) {
					snode->list[index] = 0;
					--slist->elements;
				}

				--count;
			}

			index++;
		}

		snode = snode->next;
	}
#else
	index = 0;
	while (count > 0 && index < slist->size) {
		if (snode->list[index]) {
			if ((*func)(snode->list[index], arg)) {
				snode->list[index] = 0;
				--slist->elements;
			}

			--count;
		}

		index++;
	}
#endif
}

Bool IGD_(smart_list_contains)(SmartList* slist, void* value, Bool (*cmp)(void*, void*)) {
	Int index, count;
	SmartNode* snode;

	IGD_ASSERT(slist != 0);

	snode = slist->data;

	count = slist->elements;
#ifdef USING_CHAIN_SMART_LIST
	while (snode) {
		index = 0;
		while (count > 0 && index < snode->size) {
			if (snode->list[index]) {
				if (cmp ? (*cmp)(value, snode->list[index]) : value == snode->list[index])
					return True;

				--count;
			}

			index++;
		}

		snode = snode->next;
	}
#else
	index = 0;
	while (count > 0 && index < slist->size) {
		if (snode->list[index]) {
			if (cmp ? (*cmp)(value, snode->list[index]) : value == snode->list[index])
				return True;

			--count;
		}

		index++;
	}
#endif

	return False;
}

Float IGD_(smart_list_growth_rate)(SmartList* slist) {
	IGD_ASSERT(slist != 0);
	return slist->growth_rate;
}

void IGD_(smart_list_set_growth_rate)(SmartList* slist, Float rate) {
	IGD_ASSERT(slist != 0);
	IGD_ASSERT(rate > 1.0f);

	slist->growth_rate = rate;
}

SmartValue* IGD_(smart_list_find)(SmartList* slist, Bool (*cmp)(void*, void*), void* arg) {
	Int index;
	SmartValue *ret;
	SmartValue **next;
	SmartNode* snode;

	IGD_ASSERT(slist != 0);
	IGD_ASSERT(cmp != 0);

	ret = 0;
	next = &(ret);
	snode = slist->data;

#ifdef USING_CHAIN_SMART_LIST
	index = 0;
	while (snode) {
		Int tmp;
		for (tmp = 0; tmp < snode->size; tmp++, index++) {
			if (snode->list[tmp] && (*cmp)(snode->list[tmp], arg)) {
				*next = (SmartValue*) IGD_MALLOC("igd.smartlist.slf.1", sizeof(SmartValue));
				(*next)->index = index;
				(*next)->value = snode->list[tmp];
				(*next)->next = 0;

				next = &((*next)->next);
			}
		}

		snode = snode->next;
	}
#else
	for (index = 0; index < slist->size; index++) {
		if (snode->list[index] && (*cmp)(snode->list[index], arg)) {
			*next = (SmartValue*) IGD_MALLOC("igd.smartlist.slf.1", sizeof(SmartValue));
			(*next)->index = index;
			(*next)->value = snode->list[index];
			(*next)->next = 0;

			next = &((*next)->next);
		}
	}
#endif

	return ret;
}

void IGD_(smart_list_delete_value)(SmartValue* sv) {
	IGD_ASSERT(sv != 0);

	while (sv) {
		SmartValue* tmp = sv->next;
		IGD_DATA_FREE(sv, sizeof(SmartValue));
		sv = tmp;
	}
}

SmartSeek* IGD_(smart_list_seek)(SmartList* slist) {
	SmartSeek* ss;

	IGD_ASSERT(slist != 0);

	ss = (SmartSeek*) IGD_MALLOC("igd.smartlist.slr.1", sizeof(SmartSeek));
	VG_(memset)(ss, 0, sizeof(SmartSeek));

	ss->slist = slist;
	IGD_(smart_list_rewind)(ss);

	return ss;
}

void IGD_(smart_list_delete_seek)(SmartSeek* ss) {
	IGD_ASSERT(ss != 0);

	IGD_DATA_FREE(ss, sizeof(SmartSeek));
}

void IGD_(smart_list_rewind)(SmartSeek* ss) {
	IGD_ASSERT(ss != 0);

	ss->index = 0;

#ifdef USING_CHAIN_SMART_LIST
	ss->current = &(ss->slist->data);
#endif
}

Int IGD_(smart_list_get_index)(SmartSeek* ss) {
	IGD_ASSERT(ss != 0);

#ifdef USING_CHAIN_SMART_LIST
	{
		Int index;
		SmartNode* snode;

		index = 0;
		snode = ss->slist->data;

		IGD_ASSERT(snode != 0);
		IGD_ASSERT(ss->current != 0);
		while (snode != *(ss->current)) {
			index += snode->size;
			snode = snode->next;
			IGD_ASSERT(snode != 0);
		}

		index += ss->index;
		return index;
	}
#else
	return ss->index;
#endif
}

void IGD_(smart_list_set_index)(SmartSeek* ss, Int index) {
	IGD_ASSERT(ss != 0);
	IGD_ASSERT(index >= 0 && index < ss->slist->size);

#ifdef USING_CHAIN_SMART_LIST
	{
		ss->current = &(ss->slist->data);
		while (*(ss->current)) {
			if (index < (*(ss->current))->size) {
				ss->index = index;
				return;
			}

			index -= (*(ss->current))->size;
			ss->current = &((*(ss->current))->next);
		}

		VG_(tool_panic)("instrgrind: unable to seek to index");
	}
#else
	ss->index = index;
#endif
}

static
Bool seek_until_valid(SmartSeek* ss) {
	IGD_ASSERT(ss != 0);

#ifdef USING_CHAIN_SMART_LIST
	IGD_ASSERT(ss->current != 0);
	while (*(ss->current)) {
		IGD_ASSERT(ss->index < (*(ss->current))->size);
		if ((*(ss->current))->list[ss->index])
			return True;

		ss->index++;
		if (ss->index == (*(ss->current))->size) {
			ss->index = 0;
			ss->current = &((*(ss->current))->next);
		}
	}
#else
	while (ss->index < ss->slist->size) {
		if (ss->slist->data->list[ss->index])
			return True;

		ss->index++;
	}
#endif

	return False;
}

Bool IGD_(smart_list_has_next)(SmartSeek* ss) {
	return seek_until_valid(ss);
}

void IGD_(smart_list_next)(SmartSeek* ss) {
	IGD_ASSERT(ss != 0);

#ifdef USING_CHAIN_SMART_LIST
	IGD_ASSERT(ss->current != 0);
	if (*(ss->current) && ss->index < (*(ss->current))->size) {
		ss->index++;
		if (ss->index == (*(ss->current))->size) {
			ss->index = 0;
			ss->current = &((*(ss->current))->next);
		}
	}
#else
	if (ss->index < ss->slist->size)
		ss->index++;
#endif

	seek_until_valid(ss);
}

void* IGD_(smart_list_get_value)(SmartSeek* ss) {
	IGD_ASSERT(ss != 0);

#ifdef USING_CHAIN_SMART_LIST
	IGD_ASSERT(ss->current != 0);
	if (*(ss->current) && ss->index < (*(ss->current))->size)
		return (*(ss->current))->list[ss->index];
#else
	if (ss->index < ss->slist->size)
		return ss->slist->data->list[ss->index];
#endif

	return 0;
}

void IGD_(smart_list_set_value)(SmartSeek* ss, void* value) {
	IGD_ASSERT(ss != 0);

#ifdef USING_CHAIN_SMART_LIST
	IGD_ASSERT(ss->current != 0);
	if (*(ss->current) && ss->index < (*(ss->current))->size) {
		if ((*(ss->current))->list[ss->index])
			ss->slist->elements--;

		(*(ss->current))->list[ss->index] = value;

		if (value)
			ss->slist->elements++;

		return;
	}
#else
	if (ss->index < ss->slist->size) {
		if (ss->slist->data->list[ss->index])
			ss->slist->elements--;

		ss->slist->data->list[ss->index] = value;

		if (value)
			ss->slist->elements++;

		return;
	}
#endif

	VG_(tool_panic)("instrgrind: unable set current value to smartlist");
}
