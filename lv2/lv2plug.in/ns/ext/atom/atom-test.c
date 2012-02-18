/*
  Copyright 2012 David Robillard <http://drobilla.net>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "lv2/lv2plug.in/ns/ext/atom/atom-helpers.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"

char** uris   = NULL;
size_t n_uris = 0;

char*
copy_string(const char* str)
{
	const size_t len = strlen(str);
	char*        dup = (char*)malloc(len + 1);
	memcpy(dup, str, len + 1);
	return dup;
}

LV2_URID
urid_map(LV2_URID_Map_Handle handle, const char* uri)
{
	for (size_t i = 0; i < n_uris; ++i) {
		if (!strcmp(uris[i], uri)) {
			return i + 1;
		}
	}

	uris = (char**)realloc(uris, ++n_uris * sizeof(char*));
	uris[n_uris - 1] = copy_string(uri);
	return n_uris;
}

int
test_fail(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fprintf(stderr, "error: ");
	vfprintf(stderr, fmt, args);
	va_end(args);
	return 1;
}

int
main()
{
	LV2_URID_Map   map = { NULL, urid_map };
	LV2_Atom_Forge forge;
	lv2_atom_forge_init(&forge, &map);

	LV2_URID eg_Object  = urid_map(NULL, "http://example.org/Object");
	LV2_URID eg_one     = urid_map(NULL, "http://example.org/one");
	LV2_URID eg_two     = urid_map(NULL, "http://example.org/two");
	LV2_URID eg_three   = urid_map(NULL, "http://example.org/three");
	LV2_URID eg_four    = urid_map(NULL, "http://example.org/four");
	LV2_URID eg_true    = urid_map(NULL, "http://example.org/true");
	LV2_URID eg_false   = urid_map(NULL, "http://example.org/false");
	LV2_URID eg_path    = urid_map(NULL, "http://example.org/path");
	LV2_URID eg_uri     = urid_map(NULL, "http://example.org/uri");
	LV2_URID eg_urid    = urid_map(NULL, "http://example.org/urid");
	LV2_URID eg_string  = urid_map(NULL, "http://example.org/string");
	LV2_URID eg_literal = urid_map(NULL, "http://example.org/literal");
	LV2_URID eg_tuple   = urid_map(NULL, "http://example.org/tuple");
	LV2_URID eg_vector  = urid_map(NULL, "http://example.org/vector");
	LV2_URID eg_seq     = urid_map(NULL, "http: //example.org/seq");

#define BUF_SIZE  1024
#define NUM_PROPS 14

	uint8_t buf[BUF_SIZE];
	lv2_atom_forge_set_buffer(&forge, buf, BUF_SIZE);

	LV2_Atom_Forge_Frame obj_frame;
	LV2_Atom* obj = (LV2_Atom*)lv2_atom_forge_resource(
		&forge, &obj_frame, 0, eg_Object);

	// eg_one = (Int32)1
	lv2_atom_forge_property_head(&forge, eg_one, 0);
	LV2_Atom_Int32* one = lv2_atom_forge_int32(&forge, 1);
	if (one->value != 1) {
		return test_fail("%d != 1\n", one->value);
	}

	// eg_two = (Int64)2
	lv2_atom_forge_property_head(&forge, eg_two, 0);
	LV2_Atom_Int64* two = lv2_atom_forge_int64(&forge, 2);
	if (two->value != 2) {
		return test_fail("%ld != 2\n", two->value);
	}

	// eg_three = (Float)3.0
	lv2_atom_forge_property_head(&forge, eg_three, 0);
	LV2_Atom_Float* three = lv2_atom_forge_float(&forge, 3.0f);
	if (three->value != 3) {
		return test_fail("%f != 3\n", three->value);
	}

	// eg_four = (Double)4.0
	lv2_atom_forge_property_head(&forge, eg_four, 0);
	LV2_Atom_Double* four = lv2_atom_forge_double(&forge, 4.0);
	if (four->value != 4) {
		return test_fail("%ld != 4\n", four->value);
	}

	// eg_true = (Bool)1
	lv2_atom_forge_property_head(&forge, eg_true, 0);
	LV2_Atom_Bool* t = lv2_atom_forge_bool(&forge, true);
	if (t->value != 1) {
		return test_fail("%ld != 1 (true)\n", t->value);
	}

	// eg_false = (Bool)0
	lv2_atom_forge_property_head(&forge, eg_false, 0);
	LV2_Atom_Bool* f = lv2_atom_forge_bool(&forge, false);
	if (f->value != 0) {
		return test_fail("%ld != 0 (false)\n", f->value);
	}

	// eg_path = (Path)"/foo/bar"
	const uint8_t* pstr     = (const uint8_t*)"/foo/bar";
	const size_t   pstr_len = strlen((const char*)pstr);
	lv2_atom_forge_property_head(&forge, eg_path, 0);
	LV2_Atom_String* path  = lv2_atom_forge_uri(&forge, pstr, pstr_len);
	uint8_t*         pbody = (uint8_t*)LV2_ATOM_BODY(path);
	if (strcmp((const char*)pbody, (const char*)pstr)) {
		return test_fail("%s != \"%s\"\n",
		                 (const char*)pbody, (const char*)pstr);
	}

	// eg_uri = (URI)"http://example.org/value"
	const uint8_t* ustr     = (const uint8_t*)"http://example.org/value";
	const size_t   ustr_len = strlen((const char*)ustr);
	lv2_atom_forge_property_head(&forge, eg_uri, 0);
	LV2_Atom_String* uri   = lv2_atom_forge_uri(&forge, ustr, ustr_len);
	uint8_t*         ubody = (uint8_t*)LV2_ATOM_BODY(uri);
	if (strcmp((const char*)ubody, (const char*)ustr)) {
		return test_fail("%s != \"%s\"\n",
		                 (const char*)ubody, (const char*)ustr);
	}

	// eg_urid = (URID)"http://example.org/value"
	LV2_URID eg_value = urid_map(NULL, "http://example.org/value");
	lv2_atom_forge_property_head(&forge, eg_urid, 0);
	LV2_Atom_URID* urid = lv2_atom_forge_urid(&forge, eg_value);
	if (urid->id != eg_value) {
		return test_fail("%u != %u\n", urid->id, eg_value);
	}

	// eg_string = (String)"hello"
	lv2_atom_forge_property_head(&forge, eg_string, 0);
	LV2_Atom_String* string = lv2_atom_forge_string(
		&forge, (const uint8_t*)"hello", strlen("hello"));
	uint8_t* sbody = (uint8_t*)LV2_ATOM_BODY(string);
	if (strcmp((const char*)sbody, "hello")) {
		return test_fail("%s != \"hello\"\n", (const char*)sbody);
	}

	// eg_literal = (Literal)"hello"@fr
	lv2_atom_forge_property_head(&forge, eg_literal, 0);
	LV2_Atom_Literal* literal = lv2_atom_forge_literal(
		&forge, (const uint8_t*)"bonjour", strlen("bonjour"),
		0, urid_map(NULL, "http://lexvo.org/id/term/fr"));
	uint8_t* lbody = (uint8_t*)LV2_ATOM_CONTENTS(LV2_Atom_Literal, literal);
	if (strcmp((const char*)lbody, "bonjour")) {
		return test_fail("%s != \"bonjour\"\n", (const char*)lbody);
	}

	// eg_tuple = "foo",true
	lv2_atom_forge_property_head(&forge, eg_tuple, 0);
	LV2_Atom_Forge_Frame tuple_frame;
	LV2_Atom_Tuple*      tuple = (LV2_Atom_Tuple*)lv2_atom_forge_tuple(
		&forge, &tuple_frame);
	LV2_Atom_String* tup0 = lv2_atom_forge_string(
		&forge, (const uint8_t*)"foo", strlen("foo"));
	LV2_Atom_Bool* tup1 = lv2_atom_forge_bool(&forge, true);
	lv2_atom_forge_pop(&forge, &tuple_frame);
	LV2_Atom_Tuple_Iter i = lv2_tuple_begin(tuple);
	if (lv2_tuple_is_end(tuple, i)) {
		return test_fail("Tuple iterator is empty\n");
	}
	LV2_Atom* tup0i = (LV2_Atom*)lv2_tuple_iter_get(i);
	if (!lv2_atom_equals((LV2_Atom*)tup0, tup0i)) {
		return test_fail("Corrupt tuple element 0\n");
	}
	i = lv2_tuple_iter_next(i);
	if (lv2_tuple_is_end(tuple, i)) {
		return test_fail("Premature end of tuple iterator\n");
	}
	LV2_Atom* tup1i = lv2_tuple_iter_get(i);
	if (!lv2_atom_equals((LV2_Atom*)tup1, tup1i)) {
		return test_fail("Corrupt tuple element 1\n");
	}
	i = lv2_tuple_iter_next(i);
	if (!lv2_tuple_is_end(tuple, i)) {
		return test_fail("Tuple iter is not at end\n");
	}

	// eg_vector = (Vector<Int32>)1,2,3,4
	lv2_atom_forge_property_head(&forge, eg_vector, 0);
	int32_t elems[] = { 1, 2, 3, 4 };
	LV2_Atom_Vector* vector = lv2_atom_forge_vector(
		&forge, 4, forge.Int32, sizeof(int32_t), elems);
	void* vec_body = LV2_ATOM_CONTENTS(LV2_Atom_Vector, vector);
	if (memcmp(elems, vec_body, sizeof(elems))) {
		return test_fail("Corrupt vector\n");
	}

	// eg_seq = (Sequence)1, 2
	lv2_atom_forge_property_head(&forge, eg_seq, 0);
	LV2_Atom_Forge_Frame seq_frame;
	LV2_Atom_Sequence* seq = (LV2_Atom_Sequence*)lv2_atom_forge_sequence_head(&forge, &seq_frame, 0);
	lv2_atom_forge_audio_time(&forge, 0, 0);
	lv2_atom_forge_int32(&forge, 1);
	lv2_atom_forge_audio_time(&forge, 1, 0);
	lv2_atom_forge_int32(&forge, 2);
	lv2_atom_forge_pop(&forge, &seq_frame);

	lv2_atom_forge_pop(&forge, &obj_frame);

	// Test equality
	LV2_Atom_Int32 itwo = { { forge.Int32, sizeof(int32_t) }, 2 };
	if (lv2_atom_equals((LV2_Atom*)one, (LV2_Atom*)two)) {
		return test_fail("1 == 2.0\n");
	} else if (lv2_atom_equals((LV2_Atom*)one, (LV2_Atom*)&itwo)) {
		return test_fail("1 == 2\n");
	}

	unsigned n_events = 0;
	LV2_SEQUENCE_FOREACH(seq, i) {
		LV2_Atom_Event* ev = lv2_sequence_iter_get(i);
		if (ev->time.audio.frames != n_events
		    || ev->time.audio.subframes != 0) {
			return test_fail("Corrupt event %u has bad time\n", n_events);
		} else if (ev->body.type != forge.Int32) {
			return test_fail("Corrupt event %u has bad type\n", n_events);
		} else if (((LV2_Atom_Int32*)&ev->body)->value != (int)n_events + 1) {
			return test_fail("Event %u != %d\n", n_events, n_events + 1);
		}
		++n_events;
	}
	
	unsigned n_props = 0;
	LV2_OBJECT_FOREACH((LV2_Atom_Object*)obj, i) {
		LV2_Atom_Property_Body* prop = lv2_object_iter_get(i);
		if (!prop->key) {
			return test_fail("Corrupt property %u has no key\n", n_props);
		} else if (prop->context) {
			return test_fail("Corrupt property %u has context\n", n_props);
		}
		++n_props;
	}

	if (n_props != NUM_PROPS) {
		return test_fail("Corrupt object has %u properties != %u\n",
		                 n_props, NUM_PROPS);
	}

	struct {
		const LV2_Atom* one;
		const LV2_Atom* two;
		const LV2_Atom* three;
		const LV2_Atom* four;
		const LV2_Atom* affirmative;
		const LV2_Atom* negative;
		const LV2_Atom* path;
		const LV2_Atom* uri;
		const LV2_Atom* urid;
		const LV2_Atom* string;
		const LV2_Atom* literal;
		const LV2_Atom* tuple;
		const LV2_Atom* vector;
		const LV2_Atom* seq;
	} matches;

	memset(&matches, 0, sizeof(matches));

	LV2_Atom_Object_Query q[] = {
		{ eg_one,     &matches.one },
		{ eg_two,     &matches.two },
		{ eg_three,   &matches.three },
		{ eg_four,    &matches.four },
		{ eg_true,    &matches.affirmative },
		{ eg_false,   &matches.negative },
		{ eg_path,    &matches.path },
		{ eg_uri,     &matches.uri },
		{ eg_urid,    &matches.urid },
		{ eg_string,  &matches.string },
		{ eg_literal, &matches.literal },
		{ eg_tuple,   &matches.tuple },
		{ eg_vector,  &matches.vector },
		{ eg_seq,     &matches.seq },
		LV2_OBJECT_QUERY_END
	};

	unsigned n_matches = lv2_object_get((LV2_Atom_Object*)obj, q);
	for (int i = 0; i < 2; ++i) {
		if (n_matches != n_props) {
			return test_fail("Query failed, %u matches != %u\n",
			                 n_matches, n_props);
		} else if (!lv2_atom_equals((LV2_Atom*)one, matches.one)) {
			return test_fail("Bad match one\n");
		} else if (!lv2_atom_equals((LV2_Atom*)two, matches.two)) {
			return test_fail("Bad match two\n");
		} else if (!lv2_atom_equals((LV2_Atom*)three, matches.three)) {
			return test_fail("Bad match three\n");
		} else if (!lv2_atom_equals((LV2_Atom*)four, matches.four)) {
			return test_fail("Bad match four\n");
		} else if (!lv2_atom_equals((LV2_Atom*)t, matches.affirmative)) {
			return test_fail("Bad match true\n");
		} else if (!lv2_atom_equals((LV2_Atom*)f, matches.negative)) {
			return test_fail("Bad match false\n");
		} else if (!lv2_atom_equals((LV2_Atom*)path, matches.path)) {
			return test_fail("Bad match path\n");
		} else if (!lv2_atom_equals((LV2_Atom*)uri, matches.uri)) {
			return test_fail("Bad match URI\n");
		} else if (!lv2_atom_equals((LV2_Atom*)string, matches.string)) {
			return test_fail("Bad match string\n");
		} else if (!lv2_atom_equals((LV2_Atom*)literal, matches.literal)) {
			return test_fail("Bad match literal\n");
		} else if (!lv2_atom_equals((LV2_Atom*)tuple, matches.tuple)) {
			return test_fail("Bad match tuple\n");
		} else if (!lv2_atom_equals((LV2_Atom*)vector, matches.vector)) {
			return test_fail("Bad match vector\n");
		} else if (!lv2_atom_equals((LV2_Atom*)seq, matches.seq)) {
			return test_fail("Bad match sequence\n");
		}
		memset(&matches, 0, sizeof(matches));
		n_matches = lv2_object_getv((LV2_Atom_Object*)obj,
		                            eg_one,     &matches.one,
		                            eg_two,     &matches.two,
		                            eg_three,   &matches.three,
		                            eg_four,    &matches.four,
		                            eg_true,    &matches.affirmative,
		                            eg_false,   &matches.negative,
		                            eg_path,    &matches.path,
		                            eg_uri,     &matches.uri,
		                            eg_urid,    &matches.urid,
		                            eg_string,  &matches.string,
		                            eg_literal, &matches.literal,
		                            eg_tuple,   &matches.tuple,
		                            eg_vector,  &matches.vector,
		                            eg_seq,     &matches.seq,
		                            0);
	}

	printf("All tests passed.\n");
	return 0;
}
