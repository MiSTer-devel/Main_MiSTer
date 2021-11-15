/*
	Copyright (c) 2010, Matthieu Labas
	All rights reserved.

	Redistribution and use in source and binary forms, with or without modification,
	are permitted provided that the following conditions are met:

	1. Redistributions of source code must retain the above copyright notice,
	   this list of conditions and the following disclaimer.

	2. Redistributions in binary form must reproduce the above copyright notice,
	   this list of conditions and the following disclaimer in the documentation
	   and/or other materials provided with the distribution.

	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
	ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
	WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
	IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
	INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
	NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
	PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
	WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
	ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
	OF SUCH DAMAGE.

	The views and conclusions contained in the software and documentation are those of the
	authors and should not be interpreted as representing official policies, either expressed
	or implied, of the FreeBSD Project.
*/
#if defined(WIN32) || defined(WIN64)
#pragma warning(disable : 4996)
#else
#ifndef strdup
#define _GNU_SOURCE
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "sxmlc.h"

/*
 Struct defining "special" tags such as "<? ?>" or "<![CDATA[ ]]/>".
 These tags are considered having a start and an end with some data in between that will
 be stored in the 'tag' member of an XMLNode.
 The 'tag_type' member is a constant that is associated to such tag.
 All 'len_*' members are basically the "sx_strlen()" of 'start' and 'end' members.
 */
typedef struct _Tag {
	TagType tag_type;
	SXML_CHAR* start;
	int len_start;
	SXML_CHAR* end;
	int len_end;
} _TAG;

typedef struct _SpecialTag {
	_TAG *tags;
	int n_tags;
} SPECIAL_TAG;

/*
 List of "special" tags handled by sxmlc.
 NB the "<!DOCTYPE" tag has a special handling because its 'end' changes according
 to its content ('>' or ']>').
 */
static _TAG _spec[] = {
		{ TAG_INSTR, C2SX("<?"), 2, C2SX("?>"), 2 },
		{ TAG_COMMENT, C2SX("<!--"), 4, C2SX("-->"), 3 },
		{ TAG_CDATA, C2SX("<![CDATA["), 9, C2SX("]]>"), 3 }
};
static int NB_SPECIAL_TAGS = (int)(sizeof(_spec) / sizeof(_TAG)); /* Auto computation of number of special tags */

/*
 User-registered tags.
 */
static SPECIAL_TAG _user_tags = { NULL, 0 };

int XML_register_user_tag(TagType tag_type, SXML_CHAR* start, SXML_CHAR* end)
{
	_TAG* p;
	int i, n, le;

	if (tag_type < TAG_USER)
		return -1;

	if (start == NULL || end == NULL || *start != C2SX('<'))
		return -1;

	le = sx_strlen(end);
	if (end[le-1] != C2SX('>'))
		return -1;

	i = _user_tags.n_tags;
	n = i + 1;
	p = (_TAG*)__realloc(_user_tags.tags, n * sizeof(_TAG));
	if (p == NULL)
		return -1;

	p[i].tag_type = tag_type;
	p[i].start = start;
	p[i].end = end;
	p[i].len_start = sx_strlen(start);
	p[i].len_end = le;
	_user_tags.tags = p;
	_user_tags.n_tags = n;

	return i;
}

int XML_unregister_user_tag(int i_tag)
{
	_TAG* pt;

	if (i_tag < 0 || i_tag >= _user_tags.n_tags)
 		return -1;

	if (_user_tags.n_tags == 1)
		pt = NULL;
	else {
		pt = (_TAG*)__malloc((_user_tags.n_tags - 1) * sizeof(_TAG));
		if (pt == NULL)
			return -1;
	}

	if (pt != NULL) {
		memcpy(pt, _user_tags.tags, i_tag * sizeof(_TAG));
		memcpy(&pt[i_tag], &_user_tags.tags[i_tag + 1], (_user_tags.n_tags - i_tag - 1) * sizeof(_TAG));
	}
	if (_user_tags.tags != NULL)
		__free(_user_tags.tags);
	_user_tags.tags = pt;
	_user_tags.n_tags--;

	return _user_tags.n_tags;
}

int XML_get_nb_registered_user_tags(void)
{
	return _user_tags.n_tags;
}

int XML_get_registered_user_tag(TagType tag_type)
{
	int i;

	for (i = 0; i < _user_tags.n_tags; i++)
		if (_user_tags.tags[i].tag_type == tag_type)
			return i;

	return -1;
}

/* --- XMLNode methods --- */

/*
 Add 'node' to given '*children_array' of '*len_array' elements.
 '*len_array' is overwritten with the number of elements in '*children_array' after its reallocation.
 Return the index of the newly added 'node' in '*children_array', or '-1' for memory error.
 */
static int _add_node(XMLNode*** children_array, int* len_array, XMLNode* node)
{
	XMLNode** pt = (XMLNode**)__realloc(*children_array, (*len_array+1) * sizeof(XMLNode*));

	if (pt == NULL)
		return -1;

	pt[*len_array] = node;
	*children_array = pt;

	return (*len_array)++;
}

int XMLNode_init(XMLNode* node)
{
	if (node == NULL)
		return false;

	if (node->init_value == XML_INIT_DONE)
		return true; /*(void)XMLNode_free(node);*/

	node->tag = NULL;
	node->text = NULL;

	node->attributes = NULL;
	node->n_attributes = 0;

	node->father = NULL;
	node->children = NULL;
	node->n_children = 0;

	node->tag_type = TAG_NONE;
	node->active = true;

	node->init_value = XML_INIT_DONE;

	return true;
}

XMLNode* XMLNode_allocN(int n)
{
	int i;
	XMLNode* p;

	if (n <= 0)
		return NULL;

	p = (XMLNode*)__calloc(n, sizeof(XMLNode));
	if (p == NULL)
		return NULL;

	for (i = 0; i < n; i++)
		(void)XMLNode_init(&p[i]);

	return p;
}

XMLNode* XMLNode_dup(const XMLNode* node, int copy_children)
{
	XMLNode* n;

	if (node == NULL)
		return NULL;

	n = (XMLNode*)__calloc(1, sizeof(XMLNode));
	if (n == NULL)
		return NULL;

	XMLNode_init(n);
	if (!XMLNode_copy(n, node, copy_children)) {
		XMLNode_free(n);

		return NULL;
	}

	return n;
}

int XMLNode_free(XMLNode* node)
{
	if (node == NULL || node->init_value != XML_INIT_DONE)
		return false;

	if (node->tag != NULL) {
		__free(node->tag);
		node->tag = NULL;
	}

	XMLNode_remove_text(node);
	XMLNode_remove_all_attributes(node);
	XMLNode_remove_children(node);

	node->tag_type = TAG_NONE;

	return true;
}

int XMLNode_copy(XMLNode* dst, const XMLNode* src, int copy_children)
{
	int i;

	if (dst == NULL || (src != NULL && src->init_value != XML_INIT_DONE))
		return false;

	(void)XMLNode_free(dst); /* 'dst' is freed first */

	/* NULL 'src' resets 'dst' */
	if (src == NULL)
		return true;

	/* Tag */
	if (src->tag != NULL) {
		dst->tag = sx_strdup(src->tag);
		if (dst->tag == NULL) goto copy_err;
	}

	/* Text */
	if (dst->text != NULL) {
		dst->text = sx_strdup(src->text);
		if (dst->text == NULL) goto copy_err;
	}

	/* Attributes */
	if (src->n_attributes > 0) {
		dst->attributes = (XMLAttribute*)__calloc(src->n_attributes, sizeof(XMLAttribute));
		if (dst->attributes== NULL) goto copy_err;
		dst->n_attributes = src->n_attributes;
		for (i = 0; i < src->n_attributes; i++) {
			dst->attributes[i].name = sx_strdup(src->attributes[i].name);
			dst->attributes[i].value = sx_strdup(src->attributes[i].value);
			if (dst->attributes[i].name == NULL || dst->attributes[i].value == NULL) goto copy_err;
			dst->attributes[i].active = src->attributes[i].active;
		}
	}

	dst->tag_type = src->tag_type;
	dst->father = src->father;
	dst->user = src->user;
	dst->active = src->active;

	/* Copy children if required (and there are any) */
	if (copy_children && src->n_children > 0) {
		dst->children = (XMLNode**)__calloc(src->n_children, sizeof(XMLNode*));
		if (dst->children == NULL) goto copy_err;
		dst->n_children = src->n_children;
		for (i = 0; i < src->n_children; i++) {
			if (!XMLNode_copy(dst->children[i], src->children[i], true)) goto copy_err;
		}
	}

	return true;

copy_err:
	(void)XMLNode_free(dst);

	return false;
}

int XMLNode_set_active(XMLNode* node, int active)
{
	if (node == NULL || node->init_value != XML_INIT_DONE)
		return false;

	node->active = active;

	return true;
}

int XMLNode_set_tag(XMLNode* node, const SXML_CHAR* tag)
{
	SXML_CHAR* newtag;
	if (node == NULL || tag == NULL || node->init_value != XML_INIT_DONE)
		return false;

	newtag = sx_strdup(tag);
	if (newtag == NULL)
		return false;
	if (node->tag != NULL) __free(node->tag);
	node->tag = newtag;

	return true;
}

int XMLNode_set_type(XMLNode* node, const TagType tag_type)
{
	if (node == NULL || node->init_value != XML_INIT_DONE)
		return false;

	switch (tag_type) {
		case TAG_ERROR:
		case TAG_END:
		case TAG_PARTIAL:
		case TAG_NONE:
			return false;

		default:
			node->tag_type = tag_type;
			return true;
	}
}

int XMLNode_set_attribute(XMLNode* node, const SXML_CHAR* attr_name, const SXML_CHAR* attr_value)
{
	XMLAttribute* pt;
	int i;

	if (node == NULL || attr_name == NULL || attr_name[0] == NULC || node->init_value != XML_INIT_DONE)
		return -1;

	i = XMLNode_search_attribute(node, attr_name, 0);
	if (i >= 0) { /* Attribute found: update it */
		SXML_CHAR* value = NULL;
		if (attr_value != NULL && (value = sx_strdup(attr_value)) == NULL)
			return -1;
		pt = node->attributes;
		if (pt[i].value != NULL)
			__free(pt[i].value);
		pt[i].value = value;
	} else { /* Attribute not found: add it */
		SXML_CHAR* name = sx_strdup(attr_name);
		SXML_CHAR* value = (attr_value == NULL ? NULL : sx_strdup(attr_value));
		if (name == NULL || (value == NULL && attr_value != NULL)) {
			if (value != NULL)
				__free(value);
			if (name != NULL)
				__free(name);
 			return -1;
		}
		i = node->n_attributes;
		pt = (XMLAttribute*)__realloc(node->attributes, (i+1) * sizeof(XMLAttribute));
		if (pt == NULL) {
			if (value != NULL)
				__free(value);
			__free(name);
			return -1;
		}

		pt[i].name = name;
		pt[i].value = value;
		pt[i].active = true;
		node->attributes = pt;
		node->n_attributes = i + 1;
	}

	return node->n_attributes;
}

int XMLNode_get_attribute_with_default(XMLNode* node, const SXML_CHAR* attr_name, const SXML_CHAR** attr_value, const SXML_CHAR* default_attr_value)
{
	XMLAttribute* pt;
	int i;

	if (node == NULL || attr_name == NULL || attr_name[0] == NULC || attr_value == NULL || node->init_value != XML_INIT_DONE)
		return false;

	i = XMLNode_search_attribute(node, attr_name, 0);
	if (i >= 0) {
		pt = node->attributes;
		if (pt[i].value != NULL) {
			*attr_value = sx_strdup(pt[i].value);
			if (*attr_value == NULL)
				return false;
		} else
			*attr_value = NULL; /* NULL but returns 'true' as 'NULL' is the actual attribute value */
	} else if (default_attr_value != NULL) {
		*attr_value = sx_strdup(default_attr_value);
		if (*attr_value == NULL)
			return false;
	} else
		*attr_value = NULL;

	return true;
}

int XMLNode_get_attribute_count(const XMLNode* node)
{
	int i, n;

	if (node == NULL || node->init_value != XML_INIT_DONE)
		return -1;

	for (i = n = 0; i < node->n_attributes; i++)
		if (node->attributes[i].active) n++;

	return n;
}

int XMLNode_search_attribute(const XMLNode* node, const SXML_CHAR* attr_name, int i_search)
{
	int i;

	if (node == NULL || attr_name == NULL || attr_name[0] == NULC || i_search < 0 || i_search >= node->n_attributes)
		return -1;

	for (i = i_search; i < node->n_attributes; i++)
		if (node->attributes[i].active && !sx_strcmp(node->attributes[i].name, attr_name))
			return i;

	return -1;
}

int XMLNode_remove_attribute(XMLNode* node, int i_attr)
{
	XMLAttribute* pt;
	if (node == NULL || node->init_value != XML_INIT_DONE || i_attr < 0 || i_attr >= node->n_attributes)
		return -1;

	/* Before modifying first see if we run out of memory */
	if (node->n_attributes == 1)
		pt = NULL;
	else {
		pt = (XMLAttribute*)__malloc((node->n_attributes - 1) * sizeof(XMLAttribute));
		if (pt == NULL)
			return -1;
	}

	/* Can't fail anymore, free item */
	if (node->attributes[i_attr].name != NULL) __free(node->attributes[i_attr].name);
	if (node->attributes[i_attr].value != NULL) __free(node->attributes[i_attr].value);

	if (pt != NULL) {
		memcpy(pt, node->attributes, i_attr * sizeof(XMLAttribute));
		memcpy(&pt[i_attr], &node->attributes[i_attr + 1], (node->n_attributes - i_attr - 1) * sizeof(XMLAttribute));
	}
	if (node->attributes != NULL)
		__free(node->attributes);
	node->attributes = pt;
	node->n_attributes--;

	return node->n_attributes;
}

int XMLNode_remove_all_attributes(XMLNode* node)
{
	int i;

	if (node == NULL || node->init_value != XML_INIT_DONE)
		return false;

	if (node->attributes != NULL) {
		for (i = 0; i < node->n_attributes; i++) {
			if (node->attributes[i].name != NULL)
				__free(node->attributes[i].name);
			if (node->attributes[i].value != NULL)
				__free(node->attributes[i].value);
		}
		__free(node->attributes);
		node->attributes = NULL;
	}
	node->n_attributes = 0;

	return true;
}

int XMLNode_set_text(XMLNode* node, const SXML_CHAR* text)
{
	SXML_CHAR* p;
	if (node == NULL || node->init_value != XML_INIT_DONE)
		return false;

	if (text == NULL) { /* We want to remove it => free node text */
		if (node->text != NULL) {
			__free(node->text);
			node->text = NULL;
		}

		return true;
	}

	p = (SXML_CHAR*)__realloc(node->text, (sx_strlen(text) + 1)*sizeof(SXML_CHAR)); /* +1 for '\0' */
	if (p == NULL)
		return false;
	node->text = p;

	sx_strcpy(node->text, text);

	return true;
}

int XMLNode_add_child(XMLNode* node, XMLNode* child)
{
	if (node == NULL || child == NULL || node->init_value != XML_INIT_DONE || child->init_value != XML_INIT_DONE)
		return false;

	if (_add_node(&node->children, &node->n_children, child) >= 0) {
		node->tag_type = TAG_FATHER;
		child->father = node;
		return true;
	} else
		return false;
}

int XMLNode_get_children_count(const XMLNode* node)
{
	int i, n;

	if (node == NULL || node->init_value != XML_INIT_DONE)
		return -1;

	for (i = n = 0; i < node->n_children; i++)
		if (node->children[i]->active) n++;

	return n;
}

XMLNode* XMLNode_get_child(const XMLNode* node, int i_child)
{
	int i;

	if (node == NULL || node->init_value != XML_INIT_DONE || i_child < 0 || i_child >= node->n_children)
		return NULL;

	for (i = 0; i < node->n_children; i++) {
		if (!node->children[i]->active)
			i_child++;
		else if (i == i_child)
			return node->children[i];
	}

	return NULL;
}

int XMLNode_remove_child(XMLNode* node, int i_child, int free_child)
{
	int i;
	XMLNode** pt;

	if (node == NULL || node->init_value != XML_INIT_DONE || i_child < 0 || i_child >= node->n_children)
		return -1;

	/* Lookup 'i_child'th active child */
	for (i = 0; i < node->n_children; i++) {
		if (!node->children[i]->active)
			i_child++;
		else if (i == i_child)
			break;
	}
	if (i >= node->n_children)
		return -1; /* Children is not found */

	/* Before modifying first see if we run out of memory */
	if (node->n_children == 1)
		pt = NULL;
	else {
		pt = (XMLNode**)__malloc((node->n_children - 1) * sizeof(XMLNode*));
		if (pt == NULL)
			return -1;
	}

	/* Can't fail anymore, free item */
	(void)XMLNode_free(node->children[i_child]);
	if (free_child)
		__free(node->children[i_child]);

	if (pt != NULL) {
		memcpy(pt, node->children, i_child * sizeof(XMLNode*));
		memcpy(&pt[i_child], &node->children[i_child + 1], (node->n_children - i_child - 1) * sizeof(XMLNode*));
	}
	if (node->children != NULL)
		__free(node->children);
	node->children = pt;
	node->n_children--;
	if (node->n_children == 0)
		node->tag_type = TAG_SELF;

	return node->n_children;
}

int XMLNode_remove_children(XMLNode* node)
{
	int i;

	if (node == NULL || node->init_value != XML_INIT_DONE)
		return false;

	if (node->children != NULL) {
		for (i = 0; i < node->n_children; i++)
			if (node->children[i] != NULL) {
				(void)XMLNode_free(node->children[i]);
				__free(node->children[i]);
			}
		__free(node->children);
		node->children = NULL;
	}
	node->n_children = 0;

	return true;
}

int XMLNode_equal(const XMLNode* node1, const XMLNode* node2)
{
	int i, j;

	if (node1 == node2)
		return true;

	if (node1 == NULL || node2 == NULL || node1->init_value != XML_INIT_DONE || node2->init_value != XML_INIT_DONE)
		return false;

	if (sx_strcmp(node1->tag, node2->tag))
		return false;

	/* Test all attributes from 'node1' */
	for (i = 0; i < node1->n_attributes; i++) {
		if (!node1->attributes[i].active)
			continue;
		j = XMLNode_search_attribute(node2, node1->attributes[i].name, 0);
		if (j < 0)
			return false;
		if (sx_strcmp(node1->attributes[i].value, node2->attributes[j].value))
			return false;
	}

	/* Test other attributes from 'node2' that might not be in 'node1' */
	for (i = 0; i < node2->n_attributes; i++) {
		if (!node2->attributes[i].active)
			continue;
		j = XMLNode_search_attribute(node1, node2->attributes[i].name, 0);
		if (j < 0)
			return false;
		if (sx_strcmp(node2->attributes[i].name, node1->attributes[j].name))
			return false;
	}

	return true;
}

XMLNode* XMLNode_next_sibling(const XMLNode* node)
{
	int i;
	XMLNode* father;

	if (node == NULL || node->init_value != XML_INIT_DONE || node->father == NULL)
		return NULL;

	father = node->father;
	for (i = 0; i < father->n_children && father->children[i] != node; i++) ;
	i++; /* father->children[i] is now 'node' next sibling */

	return i < father->n_children ? father->children[i] : NULL;
}

static XMLNode* _XMLNode_next(const XMLNode* node, int in_children)
{
	XMLNode* node2;

	if (node == NULL || node->init_value != XML_INIT_DONE)
		return NULL;

	/* Check first child */
	if (in_children && node->n_children > 0)
		return node->children[0];

	/* Check next sibling */
	if ((node2 = XMLNode_next_sibling(node)) != NULL)
		return node2;

	/* Check next uncle */
	return _XMLNode_next(node->father, false);
}

XMLNode* XMLNode_next(const XMLNode* node)
{
	return _XMLNode_next(node, true);
}

/* --- XMLDoc methods --- */

int XMLDoc_init(XMLDoc* doc)
{
	if (doc == NULL)
		return false;

	doc->filename[0] = NULC;
#ifdef SXMLC_UNICODE
	memset(&doc->bom, 0, sizeof(doc->bom));
#endif
	doc->nodes = NULL;
	doc->n_nodes = 0;
	doc->i_root = -1;
	doc->init_value = XML_INIT_DONE;

	return true;
}

int XMLDoc_free(XMLDoc* doc)
{
	int i;

	if (doc == NULL || doc->init_value != XML_INIT_DONE)
		return false;

	for (i = 0; i < doc->n_nodes; i++) {
		(void)XMLNode_free(doc->nodes[i]);
		__free(doc->nodes[i]);
	}
	__free(doc->nodes);
	doc->nodes = NULL;
	doc->n_nodes = 0;
	doc->i_root = -1;

	return true;
}

int XMLDoc_set_root(XMLDoc* doc, int i_root)
{
	if (doc == NULL || doc->init_value != XML_INIT_DONE || i_root < 0 || i_root >= doc->n_nodes)
		return false;

	doc->i_root = i_root;

	return true;
}

int XMLDoc_add_node(XMLDoc* doc, XMLNode* node)
{
	if (doc == NULL || node == NULL || doc->init_value != XML_INIT_DONE)
		return -1;

	if (_add_node(&doc->nodes, &doc->n_nodes, node) < 0)
		return -1;

	if (node->tag_type == TAG_FATHER)
		doc->i_root = doc->n_nodes - 1; /* Main root node is the last father node */

	return doc->n_nodes;
}

int XMLDoc_remove_node(XMLDoc* doc, int i_node, int free_node)
{
	XMLNode** pt;
	if (doc == NULL || doc->init_value != XML_INIT_DONE || i_node < 0 || i_node > doc->n_nodes)
		return false;

	/* Before modifying first see if we run out of memory */
	if (doc->n_nodes == 1)
		pt = NULL;
	else {
		pt = (XMLNode**)__malloc((doc->n_nodes - 1) * sizeof(XMLNode*));
		if (pt == NULL)
			return false;
	}

	/* Can't fail anymore, free item */
	(void)XMLNode_free(doc->nodes[i_node]);
	if (free_node) __free(doc->nodes[i_node]);

	if (pt != NULL) {
		memcpy(pt, &doc->nodes[i_node], i_node * sizeof(XMLNode*));
		memcpy(&pt[i_node], &doc->nodes[i_node + 1], (doc->n_nodes - i_node - 1) * sizeof(XMLNode*));
	}

	if (doc->nodes != NULL)
		__free(doc->nodes);
	doc->nodes = pt;
	doc->n_nodes--;

	return true;
}

/*
 Helper functions to print formatting before a new tag.
 Returns the new number of characters in the line.
 */
static int _count_new_char_line(const SXML_CHAR* str, int nb_char_tab, int cur_sz_line)
{
	for (; *str; str++) {
		if (*str == C2SX('\n'))
			cur_sz_line = 0;
		else if (*str == C2SX('\t'))
			cur_sz_line += nb_char_tab;
		else
			cur_sz_line++;
	}

	return cur_sz_line;
}
static int _print_formatting(const XMLNode* node, FILE* f, const SXML_CHAR* tag_sep, const SXML_CHAR* child_sep, int nb_char_tab, int cur_sz_line)
{
	if (tag_sep != NULL) {
		sx_fprintf(f, tag_sep);
		cur_sz_line = _count_new_char_line(tag_sep, nb_char_tab, cur_sz_line);
	}
	if (child_sep != NULL) {
		for (node = node->father; node != NULL; node = node->father) {
			sx_fprintf(f, child_sep);
			cur_sz_line = _count_new_char_line(child_sep, nb_char_tab, cur_sz_line);
		}
	}

	return cur_sz_line;
}

static int _XMLNode_print_header(const XMLNode* node, FILE* f, const SXML_CHAR* tag_sep, const SXML_CHAR* child_sep, const SXML_CHAR* attr_sep, int sz_line, int cur_sz_line, int nb_char_tab)
{
	int i;
	SXML_CHAR* p;

	if (node == NULL || f == NULL || !node->active || node->tag == NULL || node->tag[0] == NULC)
		return -1;

	/* Special handling of DOCTYPE */
	if (node->tag_type == TAG_DOCTYPE) {
		/* Search for an unescaped '[' in the DOCTYPE definition, in which case the end delimiter should be ']>' instead of '>' */
		for (p = sx_strchr(node->tag, C2SX('[')); p != NULL && *(p-1) == C2SX('\\'); p = sx_strchr(p+1, C2SX('['))) ;
		cur_sz_line += sx_fprintf(f, C2SX("<!DOCTYPE%s%s>"), node->tag, p != NULL ? C2SX("]") : C2SX(""));
		return cur_sz_line;
	}

	/* Check for special tags first */
	for (i = 0; i < NB_SPECIAL_TAGS; i++) {
		if (node->tag_type == _spec[i].tag_type) {
			sx_fprintf(f, C2SX("%s%s%s"), _spec[i].start, node->tag, _spec[i].end);
			cur_sz_line += sx_strlen(_spec[i].start) + sx_strlen(node->tag) + sx_strlen(_spec[i].end);
			return cur_sz_line;
		}
	}

	/* Check for user tags */
	for (i = 0; i < _user_tags.n_tags; i++) {
		if (node->tag_type == _user_tags.tags[i].tag_type) {
			sx_fprintf(f, C2SX("%s%s%s"), _user_tags.tags[i].start, node->tag, _user_tags.tags[i].end);
			cur_sz_line += sx_strlen(_user_tags.tags[i].start) + sx_strlen(node->tag) + sx_strlen(_user_tags.tags[i].end);
			return cur_sz_line;
		}
	}

	/* Print tag name */
	cur_sz_line += sx_fprintf(f, C2SX("<%s"), node->tag);

	/* Print attributes */
	for (i = 0; i < node->n_attributes; i++) {
		if (!node->attributes[i].active)
			continue;
		cur_sz_line += sx_strlen(node->attributes[i].name) + sx_strlen(node->attributes[i].value) + 3;
		if (sz_line > 0 && cur_sz_line > sz_line) {
			cur_sz_line = _print_formatting(node, f, tag_sep, child_sep, nb_char_tab, cur_sz_line);
			/* Add extra separator, as if new line was a child of the previous one */
			if (child_sep != NULL) {
				sx_fprintf(f, child_sep);
				cur_sz_line = _count_new_char_line(child_sep, nb_char_tab, cur_sz_line);
			}
		}
		/* Attribute name */
		if (attr_sep != NULL) {
			sx_fprintf(f, attr_sep);
			cur_sz_line = _count_new_char_line(attr_sep, nb_char_tab, cur_sz_line);
			sx_fprintf(f, C2SX("%s="), node->attributes[i].name);
		} else
			sx_fprintf(f, C2SX(" %s="), node->attributes[i].name);

		/* Attribute value */
		(void)sx_fputc(XML_DEFAULT_QUOTE, f);
		cur_sz_line += fprintHTML(f, node->attributes[i].value) + 2;
		(void)sx_fputc(XML_DEFAULT_QUOTE, f);
	}

	/* End the tag if there are no children and no text */
	if (node->n_children == 0 && (node->text == NULL || node->text[0] == NULC)) {
		cur_sz_line += sx_fprintf(f, C2SX("/>"));
	} else {
		(void)sx_fputc(C2SX('>'), f);
		cur_sz_line++;
	}

	return cur_sz_line;
}

int XMLNode_print_header(const XMLNode* node, FILE* f, int sz_line, int nb_char_tab)
{
	return _XMLNode_print_header(node, f, NULL, NULL, NULL, sz_line, 0, nb_char_tab) < 0 ? false : true;
}

static int _XMLNode_print(const XMLNode* node, FILE* f, const SXML_CHAR* tag_sep, const SXML_CHAR* child_sep, const SXML_CHAR* attr_sep, int keep_text_spaces, int sz_line, int cur_sz_line, int nb_char_tab, int depth)
{
	int i;
	SXML_CHAR* p;

	if (node != NULL && node->tag_type==TAG_TEXT) { /* Text has to be printed: check if it is only spaces */
		if (!keep_text_spaces) {
			for (p = node->text; *p != NULC && sx_isspace(*p); p++) ; /* 'p' points to first non-space character, or to '\0' if only spaces */
		} else
			p = node->text; /* '*p' won't be '\0' */
		if (*p != NULC)
			cur_sz_line += fprintHTML(f, node->text);
		return cur_sz_line;
	}

	if (node == NULL || f == NULL || !node->active || node->tag == NULL || node->tag[0] == NULC)
		return -1;

	if (nb_char_tab <= 0)
		nb_char_tab = 1;

	/* Print formatting */
	if (depth < 0) /* UGLY HACK: 'depth' forced negative on very first line so we don't print an extra 'tag_sep' (usually "\n" when pretty-printing) */
		depth = 0;
	else
		cur_sz_line = _print_formatting(node, f, tag_sep, child_sep, nb_char_tab, cur_sz_line);

	_XMLNode_print_header(node, f, tag_sep, child_sep, attr_sep, sz_line, cur_sz_line, nb_char_tab);

	if (node->text != NULL && node->text[0] != NULC) {
		/* Text has to be printed: check if it is only spaces */
		if (!keep_text_spaces) {
			for (p = node->text; *p != NULC && sx_isspace(*p); p++) ; /* 'p' points to first non-space character, or to '\0' if only spaces */
		} else
			p = node->text; /* '*p' won't be '\0' */
		if (*p != NULC) cur_sz_line += fprintHTML(f, node->text);
	} else if (node->n_children <= 0) /* Everything has already been printed */
		return cur_sz_line;

	/* Recursively print children */
	for (i = 0; i < node->n_children; i++)
		(void)_XMLNode_print(node->children[i], f, tag_sep, child_sep, attr_sep, keep_text_spaces, sz_line, cur_sz_line, nb_char_tab, depth+1);

	/* Print tag end after children */
		/* Print formatting */
	if (node->n_children > 0)
		cur_sz_line = _print_formatting(node, f, tag_sep, child_sep, nb_char_tab, cur_sz_line);
	cur_sz_line += sx_fprintf(f, C2SX("</%s>"), node->tag);

	return cur_sz_line;
}

int XMLNode_print_attr_sep(const XMLNode* node, FILE* f, const SXML_CHAR* tag_sep, const SXML_CHAR* child_sep, const SXML_CHAR* attr_sep, int keep_text_spaces, int sz_line, int nb_char_tab)
{
	return _XMLNode_print(node, f, tag_sep, child_sep, attr_sep, keep_text_spaces, sz_line, 0, nb_char_tab, 0);
}

int XMLDoc_print_attr_sep(const XMLDoc* doc, FILE* f, const SXML_CHAR* tag_sep, const SXML_CHAR* child_sep, const SXML_CHAR* attr_sep, int keep_text_spaces, int sz_line, int nb_char_tab)
{
	int i, depth, cur_sz_line;

	if (doc == NULL || f == NULL || doc->init_value != XML_INIT_DONE)
		return false;

#ifdef SXMLC_UNICODE
	/* Write BOM if it exist */
	if (doc->sz_bom > 0) fwrite(doc->bom, sizeof(unsigned char), doc->sz_bom, f);
#endif

	depth = -1; /* UGLY HACK: 'depth' forced negative on very first line so we don't print an extra 'tag_sep' (usually "\n") */
	for (i = 0, cur_sz_line = 0; i < doc->n_nodes; i++) {
		cur_sz_line = _XMLNode_print(doc->nodes[i], f, tag_sep, child_sep, attr_sep, keep_text_spaces, sz_line, cur_sz_line, nb_char_tab, depth);
		depth = 0;
	}
	/* TODO: Find something more graceful than 'depth=-1', even though everyone knows I probably never will ;) */

	return true;
}

/* --- */

int XML_parse_attribute_to(const SXML_CHAR* str, int to, XMLAttribute* xmlattr)
{
	const SXML_CHAR *p;
	int i, n0, n1, remQ = 0;
	int ret = 1;
	SXML_CHAR quote = '\0';

	if (str == NULL || xmlattr == NULL)
		return 0;

	if (to < 0)
		to = sx_strlen(str) - 1;

	/* Search for the '=' */
	/* 'n0' is where the attribute name stops, 'n1' is where the attribute value starts */
	for (n0 = 0; n0 != to && str[n0] != C2SX('=') && !sx_isspace(str[n0]); n0++) ; /* Search for '=' or a space */
	for (n1 = n0; n1 != to && sx_isspace(str[n1]); n1++) ; /* Search for something not a space */
	if (str[n1] != C2SX('='))
		return 0; /* '=' not found: malformed string */
	for (n1++; n1 != to && sx_isspace(str[n1]); n1++) ; /* Search for something not a space */
	if (isquote(str[n1])) { /* Remove quotes */
		quote = str[n1];
		remQ = 1;
	}

	xmlattr->name = (SXML_CHAR*)__malloc((n0+1)*sizeof(SXML_CHAR));
	xmlattr->value = (SXML_CHAR*)__malloc((to+1 - n1 - remQ + 1) * sizeof(SXML_CHAR));
	xmlattr->active = true;
	if (xmlattr->name != NULL && xmlattr->value != NULL) {
		/* Copy name */
		sx_strncpy(xmlattr->name, str, n0);
		xmlattr->name[n0] = NULC;
		/* (void)str_unescape(xmlattr->name); do not unescape the name */
		/* Copy value (p starts after the quote (if any) and stops at the end of 'str'
		  (skipping the quote if any, hence the '*(p+remQ)') */
		for (i = 0, p = str + n1 + remQ; i + n1 + remQ < to && *(p+remQ) != NULC; i++, p++)
			xmlattr->value[i] = *p;
		xmlattr->value[i] = NULC;
		(void)html2str(xmlattr->value, NULL); /* Convert HTML escape sequences, do not str_unescape(xmlattr->value) */
		if (remQ && *p != quote)
			ret = 2; /* Quote at the beginning but not at the end: probable presence of '>' inside attribute value, so we need to read more data! */
	} else
		ret = 0;

	if (ret == 0) {
		if (xmlattr->name != NULL) {
			__free(xmlattr->name);
			xmlattr->name = NULL;
		}
		if (xmlattr->value != NULL) {
			__free(xmlattr->value);
			xmlattr->value = NULL;
		}
	}

	return ret;
}

static TagType _parse_special_tag(const SXML_CHAR* str, int len, _TAG* tag, XMLNode* node)
{
	if (sx_strncmp(str, tag->start, tag->len_start))
		return TAG_NONE;

	if (sx_strncmp(str + len - tag->len_end, tag->end, tag->len_end)) /* There probably is a '>' inside the tag */
		return TAG_PARTIAL;

	node->tag = (SXML_CHAR*)__malloc((len - tag->len_start - tag->len_end + 1)*sizeof(SXML_CHAR));
	if (node->tag == NULL)
		return TAG_NONE;
	sx_strncpy(node->tag, str + tag->len_start, len - tag->len_start - tag->len_end);
	node->tag[len - tag->len_start - tag->len_end] = NULC;
	node->tag_type = tag->tag_type;

	return node->tag_type;
}

/*
 Reads a string that is supposed to be an xml tag like '<tag (attribName="attribValue")* [/]>' or '</tag>'.
 Fills the 'xmlnode' structure with the tag name and its attributes.
 Returns 'TAG_ERROR' if an error occurred (malformed 'str' or memory). 'TAG_*' when string is recognized.
 */
TagType XML_parse_1string(const SXML_CHAR* str, XMLNode* xmlnode)
{
	SXML_CHAR *p;
	XMLAttribute* pt;
	int n, nn, len, rc, tag_end = 0;

	if (str == NULL || xmlnode == NULL)
		return TAG_ERROR;
	len = sx_strlen(str);

	/* Check for malformed string */
	if (str[0] != C2SX('<') || str[len-1] != C2SX('>'))
		return TAG_ERROR;

	for (nn = 0; nn < NB_SPECIAL_TAGS; nn++) {
		n = (int)_parse_special_tag(str, len, &_spec[nn], xmlnode);
		switch (n) {
			case TAG_NONE:	break;				/* Nothing found => do nothing */
			default:		return (TagType)n;	/* Tag found => return it */
		}
	}

	/* "<!DOCTYPE" requires a special handling because it can end with "]>" instead of ">" if a '[' is found inside */
	if (str[1] == C2SX('!')) {
		/* DOCTYPE */
		if (!sx_strncmp(str, C2SX("<!DOCTYPE"), 9)) { /* 9 = sizeof("<!DOCTYPE") */
			for (n = 9; str[n] && str[n] != C2SX('['); n++) ; /* Look for a '[' inside the DOCTYPE, which would mean that we should be looking for a "]>" tag end */
			nn = 0;
			if (str[n]) { /* '[' was found */
				if (sx_strncmp(str+len-2, C2SX("]>"), 2)) /* There probably is a '>' inside the DOCTYPE */
					return TAG_PARTIAL;
				nn = 1;
			}
			xmlnode->tag = (SXML_CHAR*)__malloc((len - 9 - nn)*sizeof(SXML_CHAR)); /* 'len' - "<!DOCTYPE" and ">" + '\0' */
			if (xmlnode->tag == NULL)
				return TAG_ERROR;
			sx_strncpy(xmlnode->tag, &str[9], len - 10 - nn);
			xmlnode->tag[len - 10 - nn] = NULC;
			xmlnode->tag_type = TAG_DOCTYPE;

			return TAG_DOCTYPE;
		}
	}

	/* Test user tags */
	for (nn = 0; nn < _user_tags.n_tags; nn++) {
		n = _parse_special_tag(str, len, &_user_tags.tags[nn], xmlnode);
		switch (n) {
			case TAG_ERROR:	return TAG_NONE;	/* Error => exit */
			case TAG_NONE:	break;				/* Nothing found => do nothing */
			default:		return (TagType)n;	/* Tag found => return it */
		}
	}

	if (str[1] == C2SX('/'))
		tag_end = 1;

	/* tag starts at index 1 (or 2 if tag end) and ends at the first space or '/>' */
	for (n = 1 + tag_end; str[n] != NULC && str[n] != C2SX('>') && str[n] != C2SX('/') && !sx_isspace(str[n]); n++) ;
	xmlnode->tag = (SXML_CHAR*)__malloc((n - tag_end)*sizeof(SXML_CHAR));
	if (xmlnode->tag == NULL)
		return TAG_ERROR;
	sx_strncpy(xmlnode->tag, &str[1 + tag_end], n - 1 - tag_end);
	xmlnode->tag[n - 1 - tag_end] = NULC;
	if (tag_end) {
		xmlnode->tag_type = TAG_END;
		return TAG_END;
	}

	/* Here, 'n' is the position of the first space after tag name */
	while (n < len) {
		/* Skips spaces */
		while (sx_isspace(str[n])) n++;

		/* Check for XML end ('>' or '/>') */
		if (str[n] == C2SX('>')) { /* Tag with children */
			int type = (str[n-1] == '/' ? TAG_SELF : TAG_FATHER); // TODO: Find something better to cope with <tag attr=v/>
			xmlnode->tag_type = type;
			return type;
		}
		if (!sx_strcmp(str+n, C2SX("/>"))) { /* Tag without children */
			xmlnode->tag_type = TAG_SELF;
			return TAG_SELF;
		}

		/* New attribute found */
		p = sx_strchr(str+n, C2SX('='));
		if (p == NULL) goto parse_err;
		pt = (XMLAttribute*)__realloc(xmlnode->attributes, (xmlnode->n_attributes + 1) * sizeof(XMLAttribute));
		if (pt == NULL) goto parse_err;

		pt[xmlnode->n_attributes].name = NULL;
		pt[xmlnode->n_attributes].value = NULL;
		pt[xmlnode->n_attributes].active = false;
		xmlnode->n_attributes++;
		xmlnode->attributes = pt;
		while (*p != NULC && sx_isspace(*++p)) ; /* Skip spaces */
		if (isquote(*p)) { /* Attribute value starts with a quote, look for next one, ignoring protected ones with '\' */
			for (nn = p-str+1; str[nn] && str[nn] != *p; nn++) { // CHECK UNICODE "nn = p-str+1"
				/* if (str[nn] == C2SX('\\')) nn++; [bugs:#7]: '\' is valid in values */
			}
		} else { /* Attribute value stops at first space or end of XML string */
			for (nn = p-str+1; str[nn] != NULC && !sx_isspace(str[nn]) && str[nn] != C2SX('/') && str[nn] != C2SX('>'); nn++) ; /* Go to the end of the attribute value */ // CHECK UNICODE
		}

		/* Here 'str[nn]' is the character after value */
		/* the attribute definition ('attrName="attrVal"') is between 'str[n]' and 'str[nn]' */
		rc = XML_parse_attribute_to(&str[n], nn - n, &xmlnode->attributes[xmlnode->n_attributes - 1]);
		if (!rc) goto parse_err;
		if (rc == 2) { /* Probable presence of '>' inside attribute value, which is legal XML. Remove attribute to re-parse it later */
			XMLNode_remove_attribute(xmlnode, xmlnode->n_attributes - 1);
			return TAG_PARTIAL;
		}

		n = nn + 1;
	}

	sx_fprintf(stderr, C2SX("\nWE SHOULD NOT BE HERE!\n[%s]\n\n"), str);

parse_err:
	(void)XMLNode_free(xmlnode);

	return TAG_ERROR;
}

static int _parse_data_SAX(void* in, const DataSourceType in_type, const SAX_Callbacks* sax, SAX_Data* sd)
{
	SXML_CHAR *line = NULL, *txt_end, *p;
	XMLNode node;
	int ret, exit, sz, n0, ncr;
	TagType tag_type;
	int (*meos)(void* ds) = (in_type == DATA_SOURCE_BUFFER ? (int(*)(void*))_beob : (int(*)(void*))sx_feof);

	if (sax->start_doc != NULL && !sax->start_doc(sd))
		return true;
	if (sax->all_event != NULL && !sax->all_event(XML_EVENT_START_DOC, NULL, (SXML_CHAR*)sd->name, 0, sd))
		return true;

	ret = true;
	exit = false;
	sd->line_num = 1; /* Line counter, starts at 1 */
	sz = 0; /* 'line' buffer size */
	node.init_value = 0;
	(void)XMLNode_init(&node);
	while ((n0 = read_line_alloc(in, in_type, &line, &sz, 0, NULC, C2SX('>'), true, C2SX('\n'), &ncr)) != 0) {
		(void)XMLNode_free(&node);
		for (p = line; *p != NULC && sx_isspace(*p); p++) ; /* Checks if text is only spaces */
		if (*p == NULC)
			break;
		sd->line_num += ncr;

		/* Get text for 'father' (i.e. what is before '<') */
		while ((txt_end = sx_strchr(line, C2SX('<'))) == NULL) { /* '<' was not found, indicating a probable '>' inside text (should have been escaped with '&gt;' but we'll handle that ;) */
			int n1 = read_line_alloc(in, in_type, &line, &sz, n0, 0, C2SX('>'), true, C2SX('\n'), &ncr); /* Go on reading the file from current position until next '>' */
			sd->line_num += ncr;
			if (n1 <= n0) {
				ret = false;
				if (sax->on_error == NULL && sax->all_event == NULL)
					sx_fprintf(stderr, C2SX("%s:%d: MEMORY ERROR.\n"), sd->name, sd->line_num);
				else {
					if (sax->on_error != NULL && !sax->on_error(PARSE_ERR_MEMORY, sd->line_num, sd))
						break;
					if (sax->all_event != NULL && !sax->all_event(XML_EVENT_ERROR, NULL, (SXML_CHAR*)sd->name, PARSE_ERR_MEMORY, sd))
						break;
				}
				break; /* 'txt_end' is still NULL here so we'll display the syntax error below */
			}
			n0 = n1;
		}
		if (txt_end == NULL) { /* Missing tag start */
			ret = false;
			if (sax->on_error == NULL && sax->all_event == NULL)
				sx_fprintf(stderr, C2SX("%s:%d: ERROR: Unexpected end character '>', without matching '<'!\n"), sd->name, sd->line_num);
			else {
				if (sax->on_error != NULL && !sax->on_error(PARSE_ERR_UNEXPECTED_TAG_END, sd->line_num, sd))
					break;
				if (sax->all_event != NULL && !sax->all_event(XML_EVENT_ERROR, NULL, (SXML_CHAR*)sd->name, PARSE_ERR_UNEXPECTED_TAG_END, sd))
					break;
			}
			break;
		}
		/* First part of 'line' (before '<') is to be added to 'father->text' */
		*txt_end = NULC; /* Have 'line' be the text for 'father' */
		if (*line != NULC && (sax->new_text != NULL || sax->all_event != NULL)) {
			if (sax->new_text != NULL && (exit = !sax->new_text(line, sd))) /* no str_unescape(line) */
				break;
			if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_TEXT, NULL, line, sd->line_num, sd)))
				break;
		}
		*txt_end = '<'; /* Restores tag start */

		switch (tag_type = XML_parse_1string(txt_end, &node)) {
			case TAG_ERROR: /* Memory error */
				ret = false;
				if (sax->on_error == NULL && sax->all_event == NULL)
					sx_fprintf(stderr, C2SX("%s:%d: MEMORY ERROR.\n"), sd->name, sd->line_num);
				else {
					if (sax->on_error != NULL && (exit = !sax->on_error(PARSE_ERR_MEMORY, sd->line_num, sd)))
						break;
					if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_ERROR, NULL, (SXML_CHAR*)sd->name, PARSE_ERR_MEMORY, sd)))
						break;
				}
				break;

			case TAG_NONE: /* Syntax error */
				ret = false;
				p = sx_strchr(txt_end, C2SX('\n'));
				if (p != NULL)
					*p = NULC;
				if (sax->on_error == NULL && sax->all_event == NULL) {
					sx_fprintf(stderr, C2SX("%s:%d: SYNTAX ERROR (%s%s).\n"), sd->name, sd->line_num, txt_end, p == NULL ? C2SX("") : C2SX("..."));
					if (p != NULL)
						*p = C2SX('\n');
				} else {
					if (sax->on_error != NULL && (exit = !sax->on_error(PARSE_ERR_SYNTAX, sd->line_num, sd)))
						break;
					if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_ERROR, NULL, (SXML_CHAR*)sd->name, PARSE_ERR_SYNTAX, sd)))
						break;
				}
				break;

			case TAG_END:
				if (sax->end_node != NULL || sax->all_event != NULL) {
					if (sax->end_node != NULL && (exit = !sax->end_node(&node, sd)))
						break;
					if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_END_NODE, &node, NULL, sd->line_num, sd)))
						break;
				}
				break;

			default: /* Add 'node' to 'father' children */
				/* If the line looks like a comment (or CDATA) but is not properly finished, loop until we find the end. */
				while (tag_type == TAG_PARTIAL) {
					int n1 = read_line_alloc(in, in_type, &line, &sz, n0, NULC, C2SX('>'), true, C2SX('\n'), &ncr); /* Go on reading the file from current position until next '>' */
					sd->line_num += ncr;
					if (n1 <= n0) {
						ret = false;
						if (sax->on_error == NULL && sax->all_event == NULL)
							sx_fprintf(stderr, C2SX("%s:%d: SYNTAX ERROR.\n"), sd->name, sd->line_num);
						else {
							if (sax->on_error != NULL && (exit = !sax->on_error(meos(in) ? PARSE_ERR_EOF : PARSE_ERR_MEMORY, sd->line_num, sd)))
								break;
							if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_ERROR, NULL, (SXML_CHAR*)sd->name, meos(in) ? PARSE_ERR_EOF : PARSE_ERR_MEMORY, sd)))
								break;
						}
						break;
					}
					n0 = n1;
					txt_end = sx_strchr(line, C2SX('<')); /* In case 'line' has been moved by the '__realloc' in 'read_line_alloc' */
					tag_type = XML_parse_1string(txt_end, &node);
					if (tag_type == TAG_ERROR) {
						ret = false;
						if (sax->on_error == NULL && sax->all_event == NULL)
							sx_fprintf(stderr, C2SX("%s:%d: PARSE ERROR.\n"), sd->name, sd->line_num);
						else {
							if (sax->on_error != NULL && (exit = !sax->on_error(meos(in) ? PARSE_ERR_EOF : PARSE_ERR_SYNTAX, sd->line_num, sd)))
								break;
							if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_ERROR, NULL, (SXML_CHAR*)sd->name, meos(in) ? PARSE_ERR_EOF : PARSE_ERR_SYNTAX, sd)))
								break;
						}
						break;
					}
				}
				if (ret == false)
					break;
				if (sax->start_node != NULL && (exit = !sax->start_node(&node, sd)))
					break;
				if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_START_NODE, &node, NULL, sd->line_num, sd)))
					break;
				if (node.tag_type != TAG_FATHER && (sax->end_node != NULL || sax->all_event != NULL)) {
					if (sax->end_node != NULL && (exit = !sax->end_node(&node, sd)))
						break;
					if (sax->all_event != NULL && (exit = !sax->all_event(XML_EVENT_END_NODE, &node, NULL, sd->line_num, sd)))
						break;
				}
			break;
		}
		if (exit == true || ret == false || meos(in))
			break;
	}
	__free(line);
	(void)XMLNode_free(&node);

	if (sax->end_doc != NULL && !sax->end_doc(sd))
		return ret;
	if (sax->all_event != NULL)
		(void)sax->all_event(XML_EVENT_END_DOC, NULL, (SXML_CHAR*)sd->name, sd->line_num, sd);

	return ret;
}

int SAX_Callbacks_init(SAX_Callbacks* sax)
{
	if (sax == NULL)
		return false;

	sax->start_doc = NULL;
	sax->start_node = NULL;
	sax->end_node = NULL;
	sax->new_text = NULL;
	sax->on_error = NULL;
	sax->end_doc = NULL;
	sax->all_event = NULL;

	return true;
}

int DOMXMLDoc_doc_start(SAX_Data* sd)
{
	DOM_through_SAX* dom = (DOM_through_SAX*)sd->user;

	dom->current = NULL;
	dom->error = PARSE_ERR_NONE;
	dom->line_error = 0;

	return true;
}

int DOMXMLDoc_node_start(const XMLNode* node, SAX_Data* sd)
{
	DOM_through_SAX* dom = (DOM_through_SAX*)sd->user;
	XMLNode* new_node;
	int i;

	if ((new_node = XMLNode_dup(node, true)) == NULL) goto node_start_err; /* No real need to put 'true' for 'XMLNode_dup', but cleaner */

	if (dom->current == NULL) {
		if ((i = _add_node(&dom->doc->nodes, &dom->doc->n_nodes, new_node)) < 0) goto node_start_err;

		if (dom->doc->i_root < 0 && (node->tag_type == TAG_FATHER || node->tag_type == TAG_SELF))
			dom->doc->i_root = i;
	} else {
		if (_add_node(&dom->current->children, &dom->current->n_children, new_node) < 0) goto node_start_err;
	}

	new_node->father = dom->current;
	dom->current = new_node;

	return true;

node_start_err:
	dom->error = PARSE_ERR_MEMORY;
	dom->line_error = sd->line_num;
	(void)XMLNode_free(new_node);
	__free(new_node);

	return false;
}

int DOMXMLDoc_node_end(const XMLNode* node, SAX_Data* sd)
{
	DOM_through_SAX* dom = (DOM_through_SAX*)sd->user;

	if (dom->current == NULL || sx_strcmp(dom->current->tag, node->tag)) {
		sx_fprintf(stderr, C2SX("%s:%d: ERROR - End tag </%s> was unexpected"), sd->name, sd->line_num, node->tag);
		if (dom->current != NULL)
			sx_fprintf(stderr, C2SX(" (</%s> was expected)\n"), dom->current->tag);
		else
			sx_fprintf(stderr, C2SX(" (no node to end)\n"));

		dom->error = PARSE_ERR_UNEXPECTED_NODE_END;
		dom->line_error = sd->line_num;

		return false;
	}

	dom->current = dom->current->father;

	return true;
}

int DOMXMLDoc_node_text(SXML_CHAR* text, SAX_Data* sd)
{
	SXML_CHAR* p = text;
	DOM_through_SAX* dom = (DOM_through_SAX*)sd->user;

	/* Keep text, even if it is only spaces */
#if 0
	while(*p != NULC && sx_isspace(*p++)) ;
	if (*p == NULC) return true; /* Only spaces */
#endif

	/* If there is no current node to add text to, raise an error, except if text is only spaces, in which case it is probably just formatting */
	if (dom->current == NULL) {
		while(*p != NULC && sx_isspace(*p++)) ;
		if (*p == NULC) /* Only spaces => probably pretty-printing */
			return true;
		dom->error = PARSE_ERR_TEXT_OUTSIDE_NODE;
		dom->line_error = sd->line_num;
		return false; /* There is some "real" text => raise an error */
	}

	if (dom->text_as_nodes) {
		XMLNode* new_node = XMLNode_allocN(1);
		if (new_node == NULL || (new_node->text = sx_strdup(text)) == NULL
			|| _add_node(&dom->current->children, &dom->current->n_children, new_node) < 0) {
			dom->error = PARSE_ERR_MEMORY;
			dom->line_error = sd->line_num;
			(void)XMLNode_free(new_node);
			__free(new_node);
			return false;
		}
		new_node->tag_type = TAG_TEXT;
		new_node->father = dom->current;
		//dom->current->tag_type = TAG_FATHER; // OS: should parent field be forced to be TAG_FATHER? now it has at least one TAG_TEXT child. I decided not to enforce this to enforce backward-compatibility related to tag_types
		return true;
	} else { /* Old behaviour: concatenate text to the previous one */
		/* 'p' will point at the new text */
		if (dom->current->text == NULL) {
			p = sx_strdup(text);
		} else {
			p = (SXML_CHAR*)__realloc(dom->current->text, (sx_strlen(dom->current->text) + sx_strlen(text) + 1)*sizeof(SXML_CHAR));
			if (p != NULL)
				sx_strcat(p, text);
		}
		if (p == NULL) {
			dom->error = PARSE_ERR_MEMORY;
			dom->line_error = sd->line_num;
			return false;
		}

		dom->current->text = p;
	}

	return true;
}

int DOMXMLDoc_parse_error(ParseError error_num, int line_number, SAX_Data* sd)
{
	DOM_through_SAX* dom = (DOM_through_SAX*)sd->user;

	dom->error = error_num;
	dom->line_error = line_number;

	/* Complete error message will be displayed in 'DOMXMLDoc_doc_end' callback */

	return false; /* Stop on error */
}

int DOMXMLDoc_doc_end(SAX_Data* sd)
{
	DOM_through_SAX* dom = (DOM_through_SAX*)sd->user;

	if (dom->error != PARSE_ERR_NONE) {
		SXML_CHAR* msg;

		switch (dom->error) {
			case PARSE_ERR_MEMORY:				msg = C2SX("MEMORY"); break;
			case PARSE_ERR_UNEXPECTED_TAG_END:	msg = C2SX("UNEXPECTED_TAG_END"); break;
			case PARSE_ERR_SYNTAX:				msg = C2SX("SYNTAX"); break;
			case PARSE_ERR_EOF:					msg = C2SX("UNEXPECTED_END_OF_FILE"); break;
			case PARSE_ERR_TEXT_OUTSIDE_NODE:	msg = C2SX("TEXT_OUTSIDE_NODE"); break;
			case PARSE_ERR_UNEXPECTED_NODE_END:	msg = C2SX("UNEXPECTED_NODE_END"); break;
			default:							msg = C2SX("UNKNOWN"); break;
		}
		sx_fprintf(stderr, C2SX("%s:%d: An error was found (%s), loading aborted...\n"), sd->name, dom->line_error, msg);
		dom->current = NULL;
		(void)XMLDoc_free(dom->doc);
		dom->doc = NULL;
	}

	return true;
}

int SAX_Callbacks_init_DOM(SAX_Callbacks* sax)
{
	if (sax == NULL)
		return false;

	sax->start_doc = DOMXMLDoc_doc_start;
	sax->start_node = DOMXMLDoc_node_start;
	sax->end_node = DOMXMLDoc_node_end;
	sax->new_text = DOMXMLDoc_node_text;
	sax->on_error = DOMXMLDoc_parse_error;
	sax->end_doc = DOMXMLDoc_doc_end;
	sax->all_event = NULL;

	return true;
}

int XMLDoc_parse_file_SAX(const SXML_CHAR* filename, const SAX_Callbacks* sax, void* user)
{
	FILE* f;
	int ret;
	SAX_Data sd;
	SXML_CHAR* fmode =
#ifndef SXMLC_UNICODE
	C2SX("rt");
#else
	C2SX("rb"); /* In Unicode, open the file as binary so that further 'fgetwc' read all bytes */
	BOM_TYPE bom;
#endif


	if (sax == NULL || filename == NULL || filename[0] == NULC)
		return false;

	f = sx_fopen(filename, fmode);
	if (f == NULL)
		return false;
	/* Microsoft' 'ftell' returns invalid position for Unicode text files
	   (see http://connect.microsoft.com/VisualStudio/feedback/details/369265/ftell-ftell-nolock-incorrectly-handling-unicode-text-translation)
	   However, we're opening the file as binary in Unicode so we don't fall into that case...
	*/
	#if defined(SXMLC_UNICODE) && (defined(WIN32) || defined(WIN64))
	//setvbuf(f, NULL, _IONBF, 0);
	#endif

	sd.name = (SXML_CHAR*)filename;
	sd.user = user;
	sd.file = f;
#ifdef SXMLC_UNICODE
	bom = freadBOM(f, NULL, NULL); /* Skip BOM, if any */
	/* In Unicode, re-open the file in text-mode if there is no BOM (or UTF-8) as we assume that
	   the file is "plain" text (i.e. 1 byte = 1 character). If opened in binary mode, 'fgetwc'
	   would read 2 bytes for 1 character, which would not work on "plain" files. */
	if (bom == BOM_NONE || bom == BOM_UTF_8) {
		sx_fclose(f);
		f = sx_fopen(filename, C2SX("rt"));
		if (f == NULL)
			return false;
		if (bom == BOM_UTF_8)
			freadBOM(f, NULL, NULL); /* Skip the UTF-8 BOM that was found */
	}
#endif
	ret = _parse_data_SAX((void*)f, DATA_SOURCE_FILE, sax, &sd);
	(void)sx_fclose(f);

	return ret;
}

int XMLDoc_parse_buffer_SAX(const SXML_CHAR* buffer, const SXML_CHAR* name, const SAX_Callbacks* sax, void* user)
{
	DataSourceBuffer dsb = { buffer, 0 };
	SAX_Data sd;

	if (sax == NULL || buffer == NULL)
		return false;

	sd.name = name;
	sd.user = user;
	return _parse_data_SAX((void*)&dsb, DATA_SOURCE_BUFFER, sax, &sd);
}

int XMLDoc_parse_file_DOM_text_as_nodes(const SXML_CHAR* filename, XMLDoc* doc, int text_as_nodes)
{
	DOM_through_SAX dom;
	SAX_Callbacks sax;

	if (doc == NULL || filename == NULL || filename[0] == NULC || doc->init_value != XML_INIT_DONE)
		return false;

	sx_strncpy(doc->filename, filename, SXMLC_MAX_PATH - 1);
	doc->filename[SXMLC_MAX_PATH - 1] = NULC;

	/* Read potential BOM on file, only when unicode is defined */
#ifdef SXMLC_UNICODE
	{
		/* In Unicode, open the file as binary so that further 'fgetwc' read all bytes */
		FILE* f = sx_fopen(filename, C2SX("rb"));
		if (f != NULL) {
			#if defined(SXMLC_UNICODE) && (defined(WIN32) || defined(WIN64))
			//setvbuf(f, NULL, _IONBF, 0);
			#endif
			doc->bom_type = freadBOM(f, doc->bom, &doc->sz_bom);
			sx_fclose(f);
		}
	}
#endif

	dom.doc = doc;
	dom.current = NULL;
	dom.text_as_nodes = text_as_nodes;
	SAX_Callbacks_init_DOM(&sax);

	if (!XMLDoc_parse_file_SAX(filename, &sax, &dom)) {
		(void)XMLDoc_free(doc);
		dom.doc = NULL;
		return false;
	}

	return true;
}

int XMLDoc_parse_buffer_DOM_text_as_nodes(const SXML_CHAR* buffer, const SXML_CHAR* name, XMLDoc* doc, int text_as_nodes)
{
	DOM_through_SAX dom;
	SAX_Callbacks sax;

	if (doc == NULL || buffer == NULL || doc->init_value != XML_INIT_DONE)
		return false;

	dom.doc = doc;
	dom.current = NULL;
	dom.text_as_nodes = text_as_nodes;
	SAX_Callbacks_init_DOM(&sax);

	return XMLDoc_parse_buffer_SAX(buffer, name, &sax, &dom) ? true : XMLDoc_free(doc);
}



/* --- Utility functions (ex sxmlutils.c) --- */

#ifdef DBG_MEM
static int nb_alloc = 0, nb_free = 0;

void* __malloc(size_t sz)
{
	void* p = malloc(sz);
	if (p != NULL)
		nb_alloc++;
	printf("0x%x: MALLOC (%d) - NA %d - NF %d = %d\n", p, sz, nb_alloc, nb_free, nb_alloc - nb_free);
	return p;
}

void* __calloc(size_t count, size_t sz)
{
	void* p = calloc(count, sz);
	if (p != NULL)
		nb_alloc++;
	printf("0x%x: CALLOC (%d, %d) - NA %d - NF %d = %d\n", p, count, sz, nb_alloc, nb_free, nb_alloc - nb_free);
	return p;
}

void* __realloc(void* mem, size_t sz)
{
	void* p = realloc(mem, sz);
	if (mem == NULL && p != NULL)
		nb_alloc++;
	else if (mem != NULL && sz == 0)
		nb_free++;
	printf("0x%x: REALLOC 0x%x (%d)", p, mem, sz);
	if (mem == NULL)
		printf(" - NA %d - NF %d = %d", nb_alloc, nb_free, nb_alloc - nb_free);
	printf("\n");
	return p;
}

void __free(void* mem)
{
	nb_free++;
	printf("0x%x: FREE - NA %d - NF %d = %d\n", mem, nb_alloc, nb_free, nb_alloc - nb_free);
	free(mem);
}

char* __sx_strdup(const char* s)
{
/* Mimic the behavior of sx_strdup(), as we can't use it directly here: DBG_MEM is defined
   and sx_strdup is this function! (bug #5) */
#ifdef SXMLC_UNICODE
	char* p = wcsdup(s);
#else
	char* p = strdup(s);
#endif
	if (p != NULL)
		nb_alloc++;
	printf("0x%x: STRDUP (%d) - NA %d - NF %d = %d\n", p, sx_strlen(s), nb_alloc, nb_free, nb_alloc - nb_free);
	return p;
}
#endif

/* Dictionary of special characters and their HTML equivalent */
static struct _html_special_dict {
	SXML_CHAR chr;		/* Original character */
	SXML_CHAR* html;	/* Equivalent HTML string */
	int html_len;	/* 'sx_strlen(html)' */
} HTML_SPECIAL_DICT[] = {
	{ C2SX('<'), C2SX("&lt;"), 4 },
	{ C2SX('>'), C2SX("&gt;"), 4 },
	{ C2SX('"'), C2SX("&quot;"), 6 },
	{ C2SX('\''), C2SX("&apos;"), 6 },
	{ C2SX('&'), C2SX("&amp;"), 5 },
	{ NULC, NULL, 0 }, /* Terminator */
};

int _bgetc(DataSourceBuffer* ds)
{
	if (ds == NULL || ds->buf[ds->cur_pos] == NULC)
		return EOF;

	return (int)(ds->buf[ds->cur_pos++]);
}

int _beob(DataSourceBuffer* ds)
{

	if (ds == NULL || ds->buf[ds->cur_pos] == NULC)
		return true;

	return false;
}

int read_line_alloc(void* in, DataSourceType in_type, SXML_CHAR** line, int* sz_line, int i0, SXML_CHAR from, SXML_CHAR to, int keep_fromto, SXML_CHAR interest, int* interest_count)
{
	int init_sz = 0;
	SXML_CHAR ch, *pt;
	int c;
	int n, ret;
	int (*mgetc)(void* ds) = (in_type == DATA_SOURCE_BUFFER ? (int(*)(void*))_bgetc : (int(*)(void*))sx_fgetc);
	int (*meos)(void* ds) = (in_type == DATA_SOURCE_BUFFER ? (int(*)(void*))_beob : (int(*)(void*))sx_feof);

	if (in == NULL || line == NULL)
		return 0;

	if (to == NULC)
		to = C2SX('\n');
	/* Search for character 'from' */
	if (interest_count != NULL)
		*interest_count = 0;
	while (true) {
		/* Reaching EOF before 'to' char is not an error but should trigger 'line' alloc and init to '' */
		c = mgetc(in);
		ch = (SXML_CHAR)c;
		if (c == EOF)
			break;
		if (interest_count != NULL && ch == interest)
			(*interest_count)++;
		/* If 'from' is '\0', we stop here */
		if (ch == from || from == NULC)
			break;
	}

	if (sz_line == NULL)
		sz_line = &init_sz;

	if (*line == NULL || *sz_line == 0) {
		if (*sz_line == 0) *sz_line = MEM_INCR_RLA;
		*line = (SXML_CHAR*)__malloc(*sz_line*sizeof(SXML_CHAR));
		if (*line == NULL)
			return 0;
	}
	if (i0 < 0)
		i0 = 0;
	if (i0 > *sz_line)
		return 0;

	n = i0;
	if (c == CSXEOF) { /* EOF reached before 'to' char => return the empty string */
		(*line)[n] = NULC;
		return meos(in) ? n : 0; /* Error if not EOF */
	}
	if (ch != from || keep_fromto)
		(*line)[n++] = ch;
	(*line)[n] = NULC;
	ret = 0;
	while (true) {
		if ((c = mgetc(in)) == CSXEOF) { /* EOF or error */
			(*line)[n] = NULC;
			ret = meos(in) ? n : 0;
			break;
		}
		ch = (SXML_CHAR)c;
		if (interest_count != NULL && ch == interest)
			(*interest_count)++;
		(*line)[n] = ch;
		if (ch != to || (keep_fromto && to != NULC && ch == to)) /* If we reached the 'to' character and we keep it, we still need to add the extra '\0' */
			n++;
		if (n >= *sz_line) { /* Too many characters for our line => realloc some more */
			*sz_line += MEM_INCR_RLA;
			pt = (SXML_CHAR*)__realloc(*line, *sz_line*sizeof(SXML_CHAR));
			if (pt == NULL) {
				ret = 0;
				break;
			} else
				*line = pt;
		}
		(*line)[n] = NULC; /* If we reached the 'to' character and we want to strip it, 'n' hasn't changed and 'line[n]' (which is 'to') will be replaced by '\0' */
		if (ch == to) {
			ret = n;
			break;
		}
	}

#if 0 /* Automatic buffer resize is deactivated */
	/* Resize line to the exact size */
	pt = (SXML_CHAR*)__realloc(*line, (n+1)*sizeof(SXML_CHAR));
	if (pt != NULL)
		*line = pt;
#endif

	return ret;
}

/* --- */

SXML_CHAR* strcat_alloc(SXML_CHAR** src1, const SXML_CHAR* src2)
{
	SXML_CHAR* cat;
	int n;

	/* Do not concatenate '*src1' with itself */
	if (src1 == NULL || *src1 == src2)
		return NULL;

	/* Concatenate a NULL or empty string */
	if (src2 == NULL || *src2 == NULC)
		return *src1;

	n = (*src1 == NULL ? 0 : sx_strlen(*src1)) + sx_strlen(src2) + 1;
	cat = (SXML_CHAR*)__realloc(*src1, n*sizeof(SXML_CHAR));
	if (cat == NULL)
		return NULL;
	if (*src1 == NULL)
		*cat = NULC;
	*src1 = cat;
	sx_strcat(*src1, src2);

	return *src1;
}

SXML_CHAR* strip_spaces(SXML_CHAR* str, SXML_CHAR repl_sq)
{
	SXML_CHAR* p;
	int i, len;

	/* 'p' to the first non-space */
	for (p = str; *p != NULC && sx_isspace(*p); p++) ; /* No need to search for 'protect' as it is not a space */
	len = sx_strlen(str);
	for (i = len-1; sx_isspace(str[i]); i--) ;
	if (str[i] == C2SX('\\')) /* If last non-space is the protection, keep the last space */
		i++;
	str[i+1] = NULC; /* New end of string to last non-space */

	if (repl_sq == NULC) {
		if (p == str && i == len)
			return str; /* Nothing to do */
		for (i = 0; (str[i] = *p) != NULC; i++, p++) ; /* Copy 'p' to 'str' */
		return str;
	}

	/* Squeeze all spaces with 'repl_sq' */
	i = 0;
	while (*p != NULC) {
		if (sx_isspace(*p)) {
			str[i++] = repl_sq;
			while (sx_isspace(*++p)) ; /* Skips all next spaces */
		} else {
			if (*p == C2SX('\\'))
				p++;
			str[i++] = *p++;
		}
	}
	str[i] = NULC;

	return str;
}

SXML_CHAR* str_unescape(SXML_CHAR* str)
{
	int i, j;

	if (str == NULL)
		return NULL;

	for (i = j = 0; str[j]; j++) {
		if (str[j] == C2SX('\\'))
			j++;
		str[i++] = str[j];
	}

	return str;
}

int split_left_right(SXML_CHAR* str, SXML_CHAR sep, int* l0, int* l1, int* i_sep, int* r0, int* r1, int ignore_spaces, int ignore_quotes)
{
	int n0, n1, is;
	SXML_CHAR quote = '\0';

	if (str == NULL)
		return false;

	if (i_sep != NULL)
		*i_sep = -1;

	if (!ignore_spaces) /* No sense of ignore quotes if spaces are to be kept */
		ignore_quotes = false;

	/* Parse left part */

	if (ignore_spaces) {
		for (n0 = 0; str[n0] != NULC && sx_isspace(str[n0]); n0++) ; /* Skip head spaces, n0 points to first non-space */
		if (ignore_quotes && isquote(str[n0])) { /* If quote is found, look for next one */
			quote = str[n0++]; /* Quote can be '\'' or '"' */
			for (n1 = n0; str[n1] != NULC && str[n1] != quote; n1++) {
				if (str[n1] == C2SX('\\') && str[++n1] == NULC)
					break; /* Escape character (can be the last) */
			}
			for (is = n1 + 1; str[is] != NULC && sx_isspace(str[is]); is++) ; /* '--' not to take quote into account */
		} else {
			for (n1 = n0; str[n1] != NULC && str[n1] != sep && !sx_isspace(str[n1]); n1++) ; /* Search for separator or a space */
			for (is = n1; str[is] != NULC && sx_isspace(str[is]); is++) ;
		}
	} else {
		n0 = 0;
		for (n1 = 0; str[n1] != NULC && str[n1] != sep; n1++) ; /* Search for separator only */
		if (str[n1] != sep) /* Separator not found: malformed string */
			return false;
		is = n1;
	}

	/* Here 'n0' is the start of left member, 'n1' is the character after the end of left member */

	if (l0 != NULL)
		*l0 = n0;
	if (l1 != NULL)
		*l1 = n1 - 1;
	if (i_sep != NULL)
		*i_sep = is;
	if (str[is] == NULC || str[is+1] == NULC) { /* No separator => empty right member */
		if (r0 != NULL)
			*r0 = is;
		if (r1 != NULL)
			*r1 = is-1;
		if (i_sep != NULL)
			*i_sep = (str[is] == NULC ? -1 : is);
		return true;
	}

	/* Parse right part */

	n0 = is + 1;
	if (ignore_spaces) {
		for (; str[n0] != NULC && sx_isspace(str[n0]); n0++) ;
		if (ignore_quotes && isquote(str[n0]))
			quote = str[n0];
	}

	for (n1 = ++n0; str[n1]; n1++) {
		if (ignore_quotes && str[n1] == quote) /* Quote was reached */
			break;
		if (str[n1] == C2SX('\\') && str[++n1] == NULC) /* Escape character (can be the last) */
			break;
	}
	if (ignore_quotes && str[n1--] != quote) /* Quote is not the same than earlier, '--' is not to take it into account */
		return false;
	if (!ignore_spaces)
		while (str[++n1]) ; /* Jump down the end of the string */

	if (r0 != NULL)
		*r0 = n0;
	if (r1 != NULL)
		*r1 = n1;

	return true;
}

BOM_TYPE freadBOM(FILE* f, unsigned char* bom, int* sz_bom)
{
	unsigned char c1, c2;
	long pos;

	if (f == NULL)
		return BOM_NONE;

	/* Save position and try to read and skip BOM if found. If not, go back to save position. */
	pos = ftell(f);
	if (pos < 0)
		return BOM_NONE;
	if (fread(&c1, sizeof(char), 1, f) != 1 || fread(&c2, sizeof(char), 1, f) != 1) {
		fseek(f, pos, SEEK_SET);
		return BOM_NONE;
	}
	if (bom != NULL) {
		bom[0] = c1;
		bom[1] = c2;
		bom[2] = '\0';
		if (sz_bom != NULL)
			*sz_bom = 2;
	}
	switch ((unsigned short)(c1 << 8) | c2) {
		case (unsigned short)0xfeff:
			return BOM_UTF_16BE;

		case (unsigned short)0xfffe:
			pos = ftell(f); /* Save current position to get it back if BOM is not UTF-32LE */
			if (pos < 0)
				return BOM_UTF_16LE;
			if (fread(&c1, sizeof(char), 1, f) != 1 || fread(&c2, sizeof(char), 1, f) != 1) {
				fseek(f, pos, SEEK_SET);
				return BOM_UTF_16LE;
			}
			if (c1 == 0x00 && c2 == 0x00) {
				if (bom != NULL)
					bom[2] = bom[3] = bom[4] = '\0';
				if (sz_bom != NULL)
					*sz_bom = 4;
				return BOM_UTF_32LE;
			}
			fseek(f, pos, SEEK_SET); /* fseek(f, -2, SEEK_CUR) is not garanteed on Windows (and actually fail in Unicode...) */
			return BOM_UTF_16LE;

		case (unsigned short)0x0000:
			if (fread(&c1, sizeof(char), 1, f) == 1 && fread(&c2, sizeof(char), 1, f) == 1
					&& c1 == 0xfe && c2 == 0xff) {
				bom[2] = c1;
				bom[3] = c2;
				bom[4] = '\0';
				if (sz_bom != NULL)
					*sz_bom = 4;
				return BOM_UTF_32BE;
			}
			fseek(f, pos, SEEK_SET);
			return BOM_NONE;

		case (unsigned short)0xefbb: /* UTF-8? */
			if (fread(&c1, sizeof(char), 1, f) != 1 || c1 != 0xbf) { /* Not UTF-8 */
				fseek(f, pos, SEEK_SET);
				if (bom != NULL)
					bom[0] = '\0';
				if (sz_bom != NULL)
					*sz_bom = 0;
				return BOM_NONE;
			}
			if (bom != NULL) {
				bom[2] = c1;
				bom[3] = '\0';
			}
			if (sz_bom != NULL)
				*sz_bom = 3;
			return BOM_UTF_8;

		default: /* No BOM, go back */
			fseek(f, pos, SEEK_SET);
			if (bom != NULL)
				bom[0] = '\0';
			if (sz_bom != NULL)
				*sz_bom = 0;
			return BOM_NONE;
	}
}

/* --- */

SXML_CHAR* html2str(SXML_CHAR* html, SXML_CHAR* str)
{
	SXML_CHAR *ps, *pd;
	int i;

	if (html == NULL) return NULL;

	if (str == NULL) str = html;

	/* Look for '&' and matches it to any of the recognized HTML pattern. */
	/* If found, replaces the '&' by the corresponding char. */
	/* 'p2' is the char to analyze, 'p1' is where to insert it */
	for (pd = str, ps = html; *ps; ps++, pd++) {
		if (*ps != C2SX('&')) {
			if (pd != ps)
				*pd = *ps;
			continue;
		}

		for (i = 0; HTML_SPECIAL_DICT[i].chr; i++) {
			if (sx_strncmp(ps, HTML_SPECIAL_DICT[i].html, HTML_SPECIAL_DICT[i].html_len))
				continue;

			*pd = HTML_SPECIAL_DICT[i].chr;
			ps += HTML_SPECIAL_DICT[i].html_len-1;
			break;
		}
		/* If no string was found, simply copy the character */
		if (HTML_SPECIAL_DICT[i].chr == NULC && pd != ps)
			*pd = *ps;
	}
	*pd = NULC;

	return str;
}

/* TODO: Allocate 'html'? */
SXML_CHAR* str2html(SXML_CHAR* str, SXML_CHAR* html)
{
	SXML_CHAR *ps, *pd;
	int i;

	if (str == NULL)
		return NULL;

	if (html == str) /* Not handled (yet) */
		return NULL;

	if (html == NULL) { /* Allocate 'html' to the correct size */
		html = __malloc(strlen_html(str) * sizeof(SXML_CHAR));
		if (html == NULL)
			return NULL;
	}

	for (ps = str, pd = html; *ps; ps++, pd++) {
		for (i = 0; HTML_SPECIAL_DICT[i].chr; i++) {
			if (*ps == HTML_SPECIAL_DICT[i].chr) {
				sx_strcpy(pd, HTML_SPECIAL_DICT[i].html);
				pd += HTML_SPECIAL_DICT[i].html_len - 1;
				break;
			}
		}
		if (HTML_SPECIAL_DICT[i].chr == NULC && pd != ps)
			*pd = *ps;
	}
	*pd = NULC;

	return html;
}

int strlen_html(SXML_CHAR* str)
{
	int i, j, n;

	if (str == NULL)
		return 0;

	n = 0;
	for (i = 0; str[i] != NULC; i++) {
		for (j = 0; HTML_SPECIAL_DICT[j].chr; j++) {
			if (str[i] == HTML_SPECIAL_DICT[j].chr) {
				n += HTML_SPECIAL_DICT[j].html_len;
				break;
			}
		}
		if (HTML_SPECIAL_DICT[j].chr == NULC)
			n++;
	}

	return n;
}

int fprintHTML(FILE* f, SXML_CHAR* str)
{
	SXML_CHAR* p;
	int i, n;

	for (p = str, n = 0; *p != NULC; p++) {
		for (i = 0; HTML_SPECIAL_DICT[i].chr; i++) {
			if (*p != HTML_SPECIAL_DICT[i].chr)
				continue;
			sx_fprintf(f, HTML_SPECIAL_DICT[i].html);
			n += HTML_SPECIAL_DICT[i].html_len;
			break;
		}
		if (HTML_SPECIAL_DICT[i].chr == NULC) {
			(void)sx_fputc(*p, f);
			n++;
		}
	}

	return n;
}

int regstrcmp(SXML_CHAR* str, SXML_CHAR* pattern)
{
	SXML_CHAR *p, *s;

	if (str == NULL && pattern == NULL)
		return true;

	if (str == NULL || pattern == NULL)
		return false;

	p = pattern;
	s = str;
	while (true) {
		switch (*p) {
			/* Any character matches, go to next one */
			case C2SX('?'):
				p++;
				s++;
				break;

			/* Go to next character in pattern and wait until it is found in 'str' */
			case C2SX('*'):
				for (; *p != NULC; p++) { /* Squeeze '**?*??**' to '*' */
					if (*p != C2SX('*') && *p != C2SX('?'))
						break;
				}
				for (; *s != NULC; s++) {
					if (*s == *p)
						break;
				}
				break;

			/* NULL character on pattern has to be matched by 'str' */
			case 0:
				return *s ? false : true;

			default:
				if (*p == C2SX('\\')) /* Escape character */
					p++;
				if (*p++ != *s++) /* Characters do not match */
					return false;
				break;
		}
	}

	return false;
}
