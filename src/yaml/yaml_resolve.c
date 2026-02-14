/**
 * @file yaml_resolve.c
 * @brief Tag and implicit type resolver for YAML documents.
 *
 * Resolves explicit tags and applies schema-based implicit typing to
 * scalar nodes after parsing.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include "yaml_internal.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool str_eq_len(const char *a, size_t len, const char *b) {
	size_t blen = strlen(b);
	if (len != blen) return false;
	return memcmp(a, b, len) == 0;
}

static bool str_eq_ci_len(const char *a, size_t len, const char *b) {
	size_t blen = strlen(b);
	if (len != blen) return false;
	for (size_t i = 0; i < len; i++) {
		if ((char)tolower((unsigned char)a[i]) != (char)tolower((unsigned char)b[i])) {
			return false;
		}
	}
	return true;
}

static const char *tag_suffix(const char *tag) {
	static const char yaml_prefix[] = "tag:yaml.org,2002:";
	if (!tag) return NULL;
	if (strncmp(tag, "!!", 2) == 0) return tag + 2;
	if (strncmp(tag, yaml_prefix, sizeof(yaml_prefix) - 1) == 0) {
		return tag + (sizeof(yaml_prefix) - 1);
	}
	return NULL;
}

static char *strip_underscores(const char *s, size_t len, bool allow) {
	if (!allow) {
		char *copy = (char *)malloc(len + 1);
		if (!copy) return NULL;
		memcpy(copy, s, len);
		copy[len] = '\0';
		return copy;
	}

	char *buf = (char *)malloc(len + 1);
	if (!buf) return NULL;
	size_t out = 0;
	for (size_t i = 0; i < len; i++) {
		if (s[i] != '_') {
			buf[out++] = s[i];
		}
	}
	buf[out] = '\0';
	return buf;
}

static bool parse_bool_value(const char *s, size_t len, bool json_only, bool *out) {
	if (json_only) {
		if (str_eq_len(s, len, "true")) { *out = true; return true; }
		if (str_eq_len(s, len, "false")) { *out = false; return true; }
		return false;
	}
	if (str_eq_ci_len(s, len, "true")) { *out = true; return true; }
	if (str_eq_ci_len(s, len, "false")) { *out = false; return true; }
	return false;
}

static bool parse_null_value(const char *s, size_t len, bool json_only) {
	if (json_only) {
		return str_eq_len(s, len, "null");
	}
	if (len == 1 && s[0] == '~') return true;
	return str_eq_ci_len(s, len, "null");
}

static bool parse_int_value(
	const char *s,
	size_t len,
	bool allow_underscore,
	bool allow_base_prefix,
	int64_t *out
) {
	if (!s || len == 0 || !out) return false;

	char *clean = strip_underscores(s, len, allow_underscore);
	if (!clean) return false;

	const char *p = clean;
	bool neg = false;
	if (*p == '+' || *p == '-') {
		neg = (*p == '-');
		p++;
	}
	if (*p == '\0') { free(clean); return false; }

	int base = 10;
	if (allow_base_prefix && p[0] == '0' && p[1] != '\0') {
		switch (p[1]) {
			case 'b':
			case 'B':
				base = 2;
				p += 2;
				break;
			case 'o':
			case 'O':
				base = 8;
				p += 2;
				break;
			case 'x':
			case 'X':
				base = 16;
				p += 2;
				break;
			default:
				break;
		}
	}

	if (*p == '\0') { free(clean); return false; }

	errno = 0;
	char *end = NULL;
	long long parsed = strtoll(p, &end, base);
	if (errno == ERANGE || end == p || (end && *end != '\0')) {
		free(clean);
		return false;
	}

	if (neg) parsed = -parsed;
	*out = (int64_t)parsed;
	free(clean);
	return true;
}

static bool parse_float_value(
	const char *s,
	size_t len,
	bool allow_underscore,
	double *out
) {
	if (!s || len == 0 || !out) return false;

	char *clean = strip_underscores(s, len, allow_underscore);
	if (!clean) return false;

	bool has_dot = strchr(clean, '.') != NULL;
	bool has_exp = strchr(clean, 'e') != NULL || strchr(clean, 'E') != NULL;

	if (str_eq_ci_len(clean, strlen(clean), ".inf") ||
		str_eq_ci_len(clean, strlen(clean), "+.inf") ||
		str_eq_ci_len(clean, strlen(clean), "-.inf")) {
		*out = clean[0] == '-' ? -INFINITY : INFINITY;
		free(clean);
		return true;
	}

	if (str_eq_ci_len(clean, strlen(clean), ".nan")) {
		*out = NAN;
		free(clean);
		return true;
	}

	if (!has_dot && !has_exp) {
		free(clean);
		return false;
	}

	errno = 0;
	char *end = NULL;
	double parsed = strtod(clean, &end);
	if (errno == ERANGE || end == clean || (end && *end != '\0')) {
		free(clean);
		return false;
	}

	*out = parsed;
	free(clean);
	return true;
}

static const GTEXT_YAML_Node *deref_alias(const GTEXT_YAML_Node *node) {
	if (!node) return NULL;
	if (node->type == GTEXT_YAML_ALIAS && node->as.alias.target) {
		return node->as.alias.target;
	}
	return node;
}

static bool scalar_equal(const GTEXT_YAML_Node *a, const GTEXT_YAML_Node *b) {
	if (a->type != b->type) return false;
	if (a->type == GTEXT_YAML_NULL) return true;
	if (a->type == GTEXT_YAML_BOOL) return a->as.scalar.bool_value == b->as.scalar.bool_value;
	if (a->type == GTEXT_YAML_INT) return a->as.scalar.int_value == b->as.scalar.int_value;
	if (a->type == GTEXT_YAML_FLOAT) {
		if (isnan(a->as.scalar.float_value) && isnan(b->as.scalar.float_value)) return true;
		return a->as.scalar.float_value == b->as.scalar.float_value;
	}
	if (a->type == GTEXT_YAML_STRING) {
		if (a->as.scalar.length != b->as.scalar.length) return false;
		return memcmp(a->as.scalar.value, b->as.scalar.value, a->as.scalar.length) == 0;
	}
	return false;
}

static bool nodes_equal(
	const GTEXT_YAML_Node *a,
	const GTEXT_YAML_Node *b,
	size_t depth,
	size_t max_depth
) {
	a = deref_alias(a);
	b = deref_alias(b);
	if (a == b) return true;
	if (!a || !b) return false;
	if (max_depth > 0 && depth >= max_depth) return false;

	if (a->type != b->type) return false;

	switch (a->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return scalar_equal(a, b);
		case GTEXT_YAML_SEQUENCE:
			if (a->as.sequence.count != b->as.sequence.count) return false;
			for (size_t i = 0; i < a->as.sequence.count; i++) {
				if (!nodes_equal(a->as.sequence.children[i], b->as.sequence.children[i], depth + 1, max_depth)) {
					return false;
				}
			}
			return true;
		case GTEXT_YAML_MAPPING:
			if (a->as.mapping.count != b->as.mapping.count) return false;
			for (size_t i = 0; i < a->as.mapping.count; i++) {
				const GTEXT_YAML_Node *key = a->as.mapping.pairs[i].key;
				const GTEXT_YAML_Node *value = a->as.mapping.pairs[i].value;
				bool found = false;
				for (size_t j = 0; j < b->as.mapping.count; j++) {
					if (nodes_equal(key, b->as.mapping.pairs[j].key, depth + 1, max_depth) &&
						nodes_equal(value, b->as.mapping.pairs[j].value, depth + 1, max_depth)) {
						found = true;
						break;
					}
				}
				if (!found) return false;
			}
			return true;
		default:
			return false;
	}
}

typedef struct {
	GTEXT_YAML_Node *key;
	GTEXT_YAML_Node *value;
	const char *key_tag;
	const char *value_tag;
	bool from_merge;
} yaml_merge_pair;

typedef struct {
	GTEXT_YAML_Node *old_node;
	GTEXT_YAML_Node *new_node;
} yaml_merge_replacement;

static bool is_merge_key(const GTEXT_YAML_Node *key) {
	key = deref_alias(key);
	if (!key) return false;
	if (key->type != GTEXT_YAML_STRING) return false;
	if (key->as.scalar.value && strcmp(key->as.scalar.value, "<<") == 0) return true;

	const char *suffix = tag_suffix(key->as.scalar.tag);
	if (suffix && strcmp(suffix, "merge") == 0) return true;

	return false;
}

static bool merge_pairs_grow(
	yaml_merge_pair **pairs,
	size_t *count,
	size_t *capacity
) {
	if (*count < *capacity) return true;
	size_t new_cap = *capacity == 0 ? 8 : *capacity * 2;
	yaml_merge_pair *new_pairs = (yaml_merge_pair *)realloc(
		*pairs, new_cap * sizeof(yaml_merge_pair)
	);
	if (!new_pairs) return false;
	*pairs = new_pairs;
	*capacity = new_cap;
	return true;
}

static long merge_pairs_find(
	yaml_merge_pair *pairs,
	size_t count,
	const GTEXT_YAML_Node *key,
	size_t max_depth
) {
	for (size_t i = 0; i < count; i++) {
		if (nodes_equal(key, pairs[i].key, 0, max_depth)) return (long)i;
	}
	return -1;
}

static bool merge_pairs_add_or_replace(
	yaml_merge_pair **pairs,
	size_t *count,
	size_t *capacity,
	const GTEXT_YAML_Node *key,
	GTEXT_YAML_Node *value,
	const char *key_tag,
	const char *value_tag,
	size_t max_depth,
	bool from_merge
) {
	long idx = merge_pairs_find(*pairs, *count, key, max_depth);
	if (idx >= 0) {
		yaml_merge_pair *existing = &(*pairs)[(size_t)idx];
		if (from_merge) {
			existing->value = value;
			existing->key_tag = key_tag;
			existing->value_tag = value_tag;
			existing->from_merge = true;
			return true;
		}
		if (existing->from_merge) {
			existing->value = value;
			existing->key_tag = key_tag;
			existing->value_tag = value_tag;
			existing->from_merge = false;
			return true;
		}
	}
	if (!merge_pairs_grow(pairs, count, capacity)) return false;
	(*pairs)[*count].key = (GTEXT_YAML_Node *)key;
	(*pairs)[*count].value = value;
	(*pairs)[*count].key_tag = key_tag;
	(*pairs)[*count].value_tag = value_tag;
	(*pairs)[*count].from_merge = from_merge;
	(*count)++;
	return true;
}

static GTEXT_YAML_Status merge_from_mapping(
	yaml_merge_pair **pairs,
	size_t *count,
	size_t *capacity,
	const GTEXT_YAML_Node *source,
	size_t max_depth,
	GTEXT_YAML_Error *error
) {
	if (!source || source->type != GTEXT_YAML_MAPPING) {
		if (error) {
			error->code = GTEXT_YAML_E_INVALID;
			error->message = "Merge source is not a mapping";
		}
		return GTEXT_YAML_E_INVALID;
	}

	for (size_t i = 0; i < source->as.mapping.count; i++) {
		const yaml_mapping_pair *pair = &source->as.mapping.pairs[i];
		if (!pair->key || is_merge_key(pair->key)) continue;
		if (!merge_pairs_add_or_replace(
			pairs,
			count,
			capacity,
			pair->key,
			pair->value,
			pair->key_tag,
			pair->value_tag,
			max_depth,
			true
		)) {
			if (error) {
				error->code = GTEXT_YAML_E_OOM;
				error->message = "Out of memory merging mapping";
			}
			return GTEXT_YAML_E_OOM;
		}
	}

	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status apply_merge_keys(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *node,
	const GTEXT_YAML_Parse_Options *opts,
	GTEXT_YAML_Node **out_node,
	bool *out_replaced,
	GTEXT_YAML_Error *error
) {
	*out_node = node;
	*out_replaced = false;
	if (!node || node->type != GTEXT_YAML_MAPPING) return GTEXT_YAML_OK;

	bool has_merge = false;
	for (size_t i = 0; i < node->as.mapping.count; i++) {
		yaml_mapping_pair *pair = &node->as.mapping.pairs[i];
		if (pair->key && is_merge_key(pair->key)) {
			has_merge = true;
			break;
		}
	}

	if (!has_merge) return GTEXT_YAML_OK;

	yaml_merge_pair *merged_pairs = NULL;
	size_t merged_count = 0;
	size_t merged_capacity = 0;
	GTEXT_YAML_Status st = GTEXT_YAML_OK;

	for (size_t i = 0; i < node->as.mapping.count; i++) {
		yaml_mapping_pair *pair = &node->as.mapping.pairs[i];
		if (!pair->key || !is_merge_key(pair->key)) continue;
		const GTEXT_YAML_Node *value = deref_alias(pair->value);
		if (!value) continue;

		if (value->type == GTEXT_YAML_MAPPING) {
			st = merge_from_mapping(
				&merged_pairs,
				&merged_count,
				&merged_capacity,
				value,
				opts ? opts->max_depth : 0,
				error
			);
			if (st != GTEXT_YAML_OK) break;
		} else if (value->type == GTEXT_YAML_SEQUENCE) {
			for (size_t j = 0; j < value->as.sequence.count; j++) {
				const GTEXT_YAML_Node *item = deref_alias(value->as.sequence.children[j]);
				st = merge_from_mapping(
					&merged_pairs,
					&merged_count,
					&merged_capacity,
					item,
					opts ? opts->max_depth : 0,
					error
				);
				if (st != GTEXT_YAML_OK) break;
			}
			if (st != GTEXT_YAML_OK) break;
		} else {
			if (error) {
				error->code = GTEXT_YAML_E_INVALID;
				error->message = "Merge value must be mapping or sequence of mappings";
			}
			st = GTEXT_YAML_E_INVALID;
			break;
		}
	}

	if (st != GTEXT_YAML_OK) {
		free(merged_pairs);
		return st;
	}

	for (size_t i = 0; i < node->as.mapping.count; i++) {
		yaml_mapping_pair *pair = &node->as.mapping.pairs[i];
		if (!pair->key || is_merge_key(pair->key)) continue;
		if (!merge_pairs_add_or_replace(
			&merged_pairs,
			&merged_count,
			&merged_capacity,
			pair->key,
			pair->value,
			pair->key_tag,
			pair->value_tag,
			opts ? opts->max_depth : 0,
			false
		)) {
			free(merged_pairs);
			if (error) {
				error->code = GTEXT_YAML_E_OOM;
				error->message = "Out of memory merging mapping";
			}
			return GTEXT_YAML_E_OOM;
		}
	}

	if (merged_count <= node->as.mapping.count) {
		for (size_t i = 0; i < merged_count; i++) {
			node->as.mapping.pairs[i].key = merged_pairs[i].key;
			node->as.mapping.pairs[i].value = merged_pairs[i].value;
			node->as.mapping.pairs[i].key_tag = merged_pairs[i].key_tag;
			node->as.mapping.pairs[i].value_tag = merged_pairs[i].value_tag;
		}
		node->as.mapping.count = merged_count;
		free(merged_pairs);
		*out_node = node;
		*out_replaced = false;
		return GTEXT_YAML_OK;
	}

	GTEXT_YAML_Node *merged = yaml_node_new_mapping(
		doc->ctx,
		merged_count,
		node->as.mapping.tag,
		node->as.mapping.anchor
	);
	if (!merged) {
		free(merged_pairs);
		if (error) {
			error->code = GTEXT_YAML_E_OOM;
			error->message = "Out of memory creating merged mapping";
		}
		return GTEXT_YAML_E_OOM;
	}

	for (size_t i = 0; i < merged_count; i++) {
		merged->as.mapping.pairs[i].key = merged_pairs[i].key;
		merged->as.mapping.pairs[i].value = merged_pairs[i].value;
		merged->as.mapping.pairs[i].key_tag = merged_pairs[i].key_tag;
		merged->as.mapping.pairs[i].value_tag = merged_pairs[i].value_tag;
	}
	merged->as.mapping.count = merged_count;

	free(merged_pairs);
	*out_node = merged;
	*out_replaced = true;
	return GTEXT_YAML_OK;
}

static void update_alias_targets(
	GTEXT_YAML_Node *node,
	yaml_merge_replacement *replacements,
	size_t replacement_count
) {
	if (!node) return;

	if (node->type == GTEXT_YAML_ALIAS && node->as.alias.target) {
		for (size_t i = 0; i < replacement_count; i++) {
			if (node->as.alias.target == replacements[i].old_node) {
				node->as.alias.target = replacements[i].new_node;
				break;
			}
		}
		return;
	}

	if (node->type == GTEXT_YAML_SEQUENCE) {
		for (size_t i = 0; i < node->as.sequence.count; i++) {
			update_alias_targets(node->as.sequence.children[i], replacements, replacement_count);
		}
		return;
	}

	if (node->type == GTEXT_YAML_MAPPING) {
		for (size_t i = 0; i < node->as.mapping.count; i++) {
			update_alias_targets(node->as.mapping.pairs[i].key, replacements, replacement_count);
			update_alias_targets(node->as.mapping.pairs[i].value, replacements, replacement_count);
		}
	}
}

static void mapping_remove_pair(GTEXT_YAML_Node *node, size_t index) {
	if (!node || node->type != GTEXT_YAML_MAPPING) return;
	if (index >= node->as.mapping.count) return;
	for (size_t i = index; i + 1 < node->as.mapping.count; i++) {
		node->as.mapping.pairs[i] = node->as.mapping.pairs[i + 1];
	}
	if (node->as.mapping.count > 0) {
		node->as.mapping.count--;
	}
}

static GTEXT_YAML_Status apply_dupkey_policy(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *node,
	const GTEXT_YAML_Parse_Options *opts,
	GTEXT_YAML_Error *error
) {
	if (!doc || !node || !opts) return GTEXT_YAML_OK;
	if (node->type != GTEXT_YAML_MAPPING) return GTEXT_YAML_OK;
	if (node->as.mapping.count < 2) return GTEXT_YAML_OK;

	for (size_t i = 0; i < node->as.mapping.count; i++) {
		for (size_t j = i + 1; j < node->as.mapping.count; j++) {
			if (!nodes_equal(node->as.mapping.pairs[i].key, node->as.mapping.pairs[j].key, 0, opts->max_depth)) {
				continue;
			}
			switch (opts->dupkeys) {
				case GTEXT_YAML_DUPKEY_ERROR:
					if (error) {
						error->code = GTEXT_YAML_E_DUPKEY;
						error->message = "Duplicate mapping key";
					}
					return GTEXT_YAML_E_DUPKEY;
				case GTEXT_YAML_DUPKEY_FIRST_WINS:
					mapping_remove_pair(node, j);
					j--;
					break;
				case GTEXT_YAML_DUPKEY_LAST_WINS:
					mapping_remove_pair(node, i);
					if (i > 0) i--;
					j = i;
					break;
				default:
					break;
			}
		}
	}

	return GTEXT_YAML_OK;
}

static const char *resolve_tag_handle(
	GTEXT_YAML_Document *doc,
	const char *tag
) {
	if (!doc || !tag) return tag;
	if (tag[0] != '!' || tag[1] == '!') return tag;
	if (!doc->tag_handles || doc->tag_handle_count == 0) return tag;

	const char *best_prefix = NULL;
	size_t best_len = 0;

	for (size_t i = 0; i < doc->tag_handle_count; i++) {
		const char *handle = doc->tag_handles[i].handle;
		const char *prefix = doc->tag_handles[i].prefix;
		if (!handle || !prefix) continue;
		size_t hlen = strlen(handle);
		if (hlen == 0 || hlen > strlen(tag)) continue;
		if (strncmp(tag, handle, hlen) != 0) continue;
		if (hlen > best_len) {
			best_len = hlen;
			best_prefix = prefix;
		}
	}

	if (!best_prefix || best_len == 0) return tag;

	const char *suffix = tag + best_len;
	size_t prefix_len = strlen(best_prefix);
	size_t suffix_len = strlen(suffix);
	char *resolved = (char *)yaml_context_alloc(doc->ctx, prefix_len + suffix_len + 1, 1);
	if (!resolved) return tag;
	memcpy(resolved, best_prefix, prefix_len);
	memcpy(resolved + prefix_len, suffix, suffix_len);
	resolved[prefix_len + suffix_len] = '\0';
	return resolved;
}

static GTEXT_YAML_Status resolve_scalar(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *node,
	const GTEXT_YAML_Parse_Options *opts,
	GTEXT_YAML_Error *error
) {
	if (!node || node->type == GTEXT_YAML_ALIAS) return GTEXT_YAML_OK;
	if (!opts || !opts->resolve_tags) return GTEXT_YAML_OK;

	const char *value = node->as.scalar.value;
	size_t len = node->as.scalar.length;
	const char *tag = node->as.scalar.tag;

	const char *resolved_tag = resolve_tag_handle(doc, tag);
	if (resolved_tag != tag) {
		node->as.scalar.tag = resolved_tag;
		tag = resolved_tag;
	}

	const char *suffix = tag_suffix(tag);
	if (suffix) {
		if (strcmp(suffix, "str") == 0) {
			node->type = GTEXT_YAML_STRING;
			node->as.scalar.type = GTEXT_YAML_STRING;
			return GTEXT_YAML_OK;
		}
		if (strcmp(suffix, "bool") == 0) {
			bool out = false;
			if (!parse_bool_value(value, len, false, &out)) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid boolean scalar for explicit tag";
				}
				return GTEXT_YAML_E_INVALID;
			}
			node->type = GTEXT_YAML_BOOL;
			node->as.scalar.type = GTEXT_YAML_BOOL;
			node->as.scalar.bool_value = out;
			return GTEXT_YAML_OK;
		}
		if (strcmp(suffix, "int") == 0) {
			int64_t out = 0;
			if (!parse_int_value(value, len, true, true, &out)) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid integer scalar for explicit tag";
				}
				return GTEXT_YAML_E_INVALID;
			}
			node->type = GTEXT_YAML_INT;
			node->as.scalar.type = GTEXT_YAML_INT;
			node->as.scalar.int_value = out;
			return GTEXT_YAML_OK;
		}
		if (strcmp(suffix, "float") == 0) {
			double out = 0.0;
			if (!parse_float_value(value, len, true, &out)) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid float scalar for explicit tag";
				}
				return GTEXT_YAML_E_INVALID;
			}
			node->type = GTEXT_YAML_FLOAT;
			node->as.scalar.type = GTEXT_YAML_FLOAT;
			node->as.scalar.float_value = out;
			return GTEXT_YAML_OK;
		}
		if (strcmp(suffix, "null") == 0) {
			node->type = GTEXT_YAML_NULL;
			node->as.scalar.type = GTEXT_YAML_NULL;
			return GTEXT_YAML_OK;
		}

		return GTEXT_YAML_OK;
	}

	if (tag && tag[0] != '\0') {
		return GTEXT_YAML_OK;
	}

	if (opts->schema == GTEXT_YAML_SCHEMA_FAILSAFE) {
		return GTEXT_YAML_OK;
	}

	bool json_only = opts->schema == GTEXT_YAML_SCHEMA_JSON;
	bool allow_underscore = opts->schema == GTEXT_YAML_SCHEMA_CORE;
	bool allow_base_prefix = opts->schema == GTEXT_YAML_SCHEMA_CORE;

	if (parse_null_value(value, len, json_only)) {
		node->type = GTEXT_YAML_NULL;
		node->as.scalar.type = GTEXT_YAML_NULL;
		return GTEXT_YAML_OK;
	}

	bool bool_out = false;
	if (parse_bool_value(value, len, json_only, &bool_out)) {
		node->type = GTEXT_YAML_BOOL;
		node->as.scalar.type = GTEXT_YAML_BOOL;
		node->as.scalar.bool_value = bool_out;
		return GTEXT_YAML_OK;
	}

	int64_t int_out = 0;
	if (parse_int_value(value, len, allow_underscore, allow_base_prefix, &int_out)) {
		node->type = GTEXT_YAML_INT;
		node->as.scalar.type = GTEXT_YAML_INT;
		node->as.scalar.int_value = int_out;
		return GTEXT_YAML_OK;
	}

	double float_out = 0.0;
	if (parse_float_value(value, len, allow_underscore, &float_out)) {
		node->type = GTEXT_YAML_FLOAT;
		node->as.scalar.type = GTEXT_YAML_FLOAT;
		node->as.scalar.float_value = float_out;
		return GTEXT_YAML_OK;
	}

	return GTEXT_YAML_OK;
}

static GTEXT_YAML_Status resolve_node(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node **node_ptr,
	const GTEXT_YAML_Parse_Options *opts,
	yaml_merge_replacement **replacements,
	size_t *replacement_count,
	size_t *replacement_capacity,
	GTEXT_YAML_Error *error
) {
	if (!node_ptr || !*node_ptr) return GTEXT_YAML_OK;
	GTEXT_YAML_Node *node = *node_ptr;

	switch (node->type) {
		case GTEXT_YAML_STRING:
		case GTEXT_YAML_BOOL:
		case GTEXT_YAML_INT:
		case GTEXT_YAML_FLOAT:
		case GTEXT_YAML_NULL:
			return resolve_scalar(doc, node, opts, error);
		case GTEXT_YAML_SEQUENCE:
			if (node->as.sequence.tag) {
				node->as.sequence.tag = resolve_tag_handle(doc, node->as.sequence.tag);
			}
			for (size_t i = 0; i < node->as.sequence.count; i++) {
				GTEXT_YAML_Node *child = node->as.sequence.children[i];
				GTEXT_YAML_Status st = resolve_node(
					doc,
					&child,
					opts,
					replacements,
					replacement_count,
					replacement_capacity,
					error
				);
				if (st != GTEXT_YAML_OK) return st;
				node->as.sequence.children[i] = child;
			}
			return GTEXT_YAML_OK;
		case GTEXT_YAML_MAPPING:
			if (node->as.mapping.tag) {
				node->as.mapping.tag = resolve_tag_handle(doc, node->as.mapping.tag);
			}
			for (size_t i = 0; i < node->as.mapping.count; i++) {
				GTEXT_YAML_Node *key = node->as.mapping.pairs[i].key;
				GTEXT_YAML_Status st = resolve_node(
					doc,
					&key,
					opts,
					replacements,
					replacement_count,
					replacement_capacity,
					error
				);
				if (st != GTEXT_YAML_OK) return st;
				node->as.mapping.pairs[i].key = key;

				GTEXT_YAML_Node *value = node->as.mapping.pairs[i].value;
				st = resolve_node(
					doc,
					&value,
					opts,
					replacements,
					replacement_count,
					replacement_capacity,
					error
				);
				if (st != GTEXT_YAML_OK) return st;
				node->as.mapping.pairs[i].value = value;
			}
			{
				GTEXT_YAML_Node *merged_node = node;
				bool replaced = false;
				GTEXT_YAML_Status st = apply_merge_keys(
					doc,
					node,
					opts,
					&merged_node,
					&replaced,
					error
				);
				if (st != GTEXT_YAML_OK) return st;
				if (replaced) {
					if (*replacement_count >= *replacement_capacity) {
						size_t new_cap = *replacement_capacity == 0 ? 4 : *replacement_capacity * 2;
						yaml_merge_replacement *new_items = (yaml_merge_replacement *)realloc(
							*replacements,
							new_cap * sizeof(yaml_merge_replacement)
						);
						if (!new_items) {
							if (error) {
								error->code = GTEXT_YAML_E_OOM;
								error->message = "Out of memory tracking merge replacements";
							}
							return GTEXT_YAML_E_OOM;
						}
						*replacements = new_items;
						*replacement_capacity = new_cap;
					}
					(*replacements)[*replacement_count].old_node = node;
					(*replacements)[*replacement_count].new_node = merged_node;
					(*replacement_count)++;
					*node_ptr = merged_node;
					node = merged_node;
				}
			}
			return apply_dupkey_policy(doc, node, opts, error);
		case GTEXT_YAML_ALIAS:
		default:
			return GTEXT_YAML_OK;
	}
}

GTEXT_INTERNAL_API GTEXT_YAML_Status yaml_resolve_document(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Error *error
) {
	if (!doc) return GTEXT_YAML_E_INVALID;
	const GTEXT_YAML_Parse_Options *opts = &doc->options;

	yaml_merge_replacement *replacements = NULL;
	size_t replacement_count = 0;
	size_t replacement_capacity = 0;

	GTEXT_YAML_Node *root = doc->root;
	GTEXT_YAML_Status st = resolve_node(
		doc,
		&root,
		opts,
		&replacements,
		&replacement_count,
		&replacement_capacity,
		error
	);
	if (st != GTEXT_YAML_OK) {
		free(replacements);
		return st;
	}

	doc->root = root;
	if (replacement_count > 0) {
		update_alias_targets(doc->root, replacements, replacement_count);
	}
	free(replacements);
	return GTEXT_YAML_OK;
}
