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
#include <stdio.h>
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

static const GTEXT_YAML_Node *deref_alias(const GTEXT_YAML_Node *node);

static const char *implicit_tag_suffix(GTEXT_YAML_Node_Type type) {
	switch (type) {
		case GTEXT_YAML_STRING:
			return "str";
		case GTEXT_YAML_BOOL:
			return "bool";
		case GTEXT_YAML_INT:
			return "int";
		case GTEXT_YAML_FLOAT:
			return "float";
		case GTEXT_YAML_NULL:
			return "null";
		default:
			return NULL;
	}
}

static const char *scalar_tag_id(const GTEXT_YAML_Node *node) {
	if (!node) return NULL;
	const char *tag = node->as.scalar.tag;
	if (tag) {
		const char *suffix = tag_suffix(tag);
		return suffix ? suffix : tag;
	}
	return implicit_tag_suffix(node->type);
}

static bool node_is_null(const GTEXT_YAML_Node *node) {
	const GTEXT_YAML_Node *resolved = deref_alias(node);
	if (!resolved) return false;
	if (resolved->type == GTEXT_YAML_NULL) return true;
	return false;
}

static bool parse_fixed_digits(const char *s, size_t len, size_t count, int *out) {
	if (!s || len < count || !out) return false;
	int value = 0;
	for (size_t i = 0; i < count; i++) {
		if (s[i] < '0' || s[i] > '9') return false;
		value = value * 10 + (s[i] - '0');
	}
	*out = value;
	return true;
}

static int days_in_month(int year, int month) {
	static const int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	int is_leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
	if (month == 2) return days[1] + (is_leap ? 1 : 0);
	return days[month - 1];
}

static bool parse_timestamp(
	const char *value,
	size_t len,
	yaml_node_scalar *out
) {
	if (!value || len < 10 || !out) return false;
	int year = 0;
	int month = 0;
	int day = 0;
	if (!parse_fixed_digits(value, len, 4, &year)) return false;
	if (value[4] != '-') return false;
	if (!parse_fixed_digits(value + 5, len - 5, 2, &month)) return false;
	if (value[7] != '-') return false;
	if (!parse_fixed_digits(value + 8, len - 8, 2, &day)) return false;
	if (month < 1 || month > 12) return false;
	if (day < 1 || day > days_in_month(year, month)) return false;

	bool has_time = false;
	bool tz_specified = false;
	bool tz_utc = false;
	int tz_offset = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	int nsec = 0;

	if (len > 10) {
		size_t idx = 10;
		char sep = value[idx];
		if (sep != 'T' && sep != 't' && sep != ' ') return false;
		idx++;
		has_time = true;

		if (!parse_fixed_digits(value + idx, len - idx, 2, &hour)) return false;
		idx += 2;
		if (idx >= len || value[idx] != ':') return false;
		idx++;
		if (!parse_fixed_digits(value + idx, len - idx, 2, &minute)) return false;
		idx += 2;
		if (idx < len && value[idx] == ':') {
			idx++;
			if (!parse_fixed_digits(value + idx, len - idx, 2, &second)) return false;
			idx += 2;
		}
		if (hour > 23 || minute > 59 || second > 60) return false;

		if (idx < len && value[idx] == '.') {
			idx++;
			if (idx >= len) return false;
			int digits = 0;
			int frac = 0;
			while (idx < len && value[idx] >= '0' && value[idx] <= '9') {
				if (digits >= 9) return false;
				frac = frac * 10 + (value[idx] - '0');
				digits++;
				idx++;
			}
			while (digits < 9) {
				frac *= 10;
				digits++;
			}
			nsec = frac;
		}

		if (idx < len) {
			if (value[idx] == 'Z' || value[idx] == 'z') {
				tz_specified = true;
				tz_utc = true;
				tz_offset = 0;
				idx++;
			} else if (value[idx] == '+' || value[idx] == '-') {
				int sign = value[idx] == '-' ? -1 : 1;
				idx++;
				int tz_hour = 0;
				int tz_minute = 0;
				if (!parse_fixed_digits(value + idx, len - idx, 2, &tz_hour)) return false;
				idx += 2;
				if (idx < len && value[idx] == ':') {
					idx++;
				}
				if (idx + 2 <= len && idx < len && value[idx] >= '0' && value[idx] <= '9') {
					if (!parse_fixed_digits(value + idx, len - idx, 2, &tz_minute)) return false;
					idx += 2;
				}
				if (tz_hour > 23 || tz_minute > 59) return false;
				tz_specified = true;
				tz_offset = sign * (tz_hour * 60 + tz_minute);
			}
			if (idx != len) return false;
		}
	}

	out->has_timestamp = true;
	out->timestamp_has_time = has_time;
	out->timestamp_tz_specified = tz_specified;
	out->timestamp_tz_utc = tz_utc;
	out->timestamp_year = year;
	out->timestamp_month = month;
	out->timestamp_day = day;
	out->timestamp_hour = hour;
	out->timestamp_minute = minute;
	out->timestamp_second = second;
	out->timestamp_nsec = nsec;
	out->timestamp_tz_offset = tz_offset;
	return true;
}

static const char *format_timestamp(
	GTEXT_YAML_Document *doc,
	const yaml_node_scalar *scalar
) {
	if (!doc || !scalar || !scalar->has_timestamp) return NULL;
	char buf[64];
	int offset_abs = scalar->timestamp_tz_offset < 0
		? -scalar->timestamp_tz_offset
		: scalar->timestamp_tz_offset;
	int offset_hour = offset_abs / 60;
	int offset_min = offset_abs % 60;

	int len = 0;
	if (!scalar->timestamp_has_time) {
		len = snprintf(
			buf,
			sizeof(buf),
			"%04d-%02d-%02d",
			scalar->timestamp_year,
			scalar->timestamp_month,
			scalar->timestamp_day
		);
	} else {
		len = snprintf(
			buf,
			sizeof(buf),
			"%04d-%02d-%02dT%02d:%02d:%02d",
			scalar->timestamp_year,
			scalar->timestamp_month,
			scalar->timestamp_day,
			scalar->timestamp_hour,
			scalar->timestamp_minute,
			scalar->timestamp_second
		);
		if (scalar->timestamp_nsec > 0) {
			int frac = scalar->timestamp_nsec;
			char frac_buf[10];
			int digits = 9;
			for (int i = 8; i >= 0; i--) {
				frac_buf[i] = (char)('0' + (frac % 10));
				frac /= 10;
			}
			while (digits > 0 && frac_buf[digits - 1] == '0') {
				digits--;
			}
			if (digits > 0 && len + 1 + digits < (int)sizeof(buf)) {
				buf[len++] = '.';
				memcpy(buf + len, frac_buf, (size_t)digits);
				len += digits;
				buf[len] = '\0';
			}
		}
		if (scalar->timestamp_tz_specified) {
			if (scalar->timestamp_tz_utc) {
				if (len + 1 < (int)sizeof(buf)) {
					buf[len++] = 'Z';
					buf[len] = '\0';
				}
			} else {
				char sign = scalar->timestamp_tz_offset < 0 ? '-' : '+';
				if (len + 6 < (int)sizeof(buf)) {
					len += snprintf(
						buf + len,
						sizeof(buf) - (size_t)len,
						"%c%02d:%02d",
						sign,
						offset_hour,
						offset_min
					);
				}
			}
		}
	}
	if (len <= 0) return NULL;
	char *out = (char *)yaml_context_alloc(doc->ctx, (size_t)len + 1, 1);
	if (!out) return NULL;
	memcpy(out, buf, (size_t)len);
	out[len] = '\0';
	return out;
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

static bool parse_bool_value(
	const char *s,
	size_t len,
	bool json_only,
	bool yaml_1_1,
	bool *out
) {
	if (json_only) {
		if (str_eq_len(s, len, "true")) { *out = true; return true; }
		if (str_eq_len(s, len, "false")) { *out = false; return true; }
		return false;
	}
	if (str_eq_ci_len(s, len, "true")) { *out = true; return true; }
	if (str_eq_ci_len(s, len, "false")) { *out = false; return true; }
	if (yaml_1_1) {
		if (str_eq_ci_len(s, len, "yes")) { *out = true; return true; }
		if (str_eq_ci_len(s, len, "no")) { *out = false; return true; }
		if (str_eq_ci_len(s, len, "on")) { *out = true; return true; }
		if (str_eq_ci_len(s, len, "off")) { *out = false; return true; }
		if (str_eq_ci_len(s, len, "y")) { *out = true; return true; }
		if (str_eq_ci_len(s, len, "n")) { *out = false; return true; }
	}
	return false;
}

static bool parse_null_value(const char *s, size_t len, bool json_only) {
	if (json_only) {
		return str_eq_len(s, len, "null");
	}
	if (len == 1 && s[0] == '~') return true;
	return str_eq_ci_len(s, len, "null");
}

static bool is_base64_space(unsigned char c) {
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static int base64_value(unsigned char c) {
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

static bool base64_decode(
	GTEXT_YAML_Document *doc,
	const char *value,
	size_t len,
	const unsigned char **out_data,
	size_t *out_len
) {
	if (!doc || !value || !out_data || !out_len) return false;

	char *filtered = (char *)malloc(len + 1);
	if (!filtered) return false;
	size_t count = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)value[i];
		if (is_base64_space(c)) continue;
		if (c == '=' || base64_value(c) >= 0) {
			filtered[count++] = (char)c;
			continue;
		}
		free(filtered);
		return false;
	}
	filtered[count] = '\0';

	if (count == 0 || (count % 4) != 0) {
		free(filtered);
		return false;
	}

	size_t padding = 0;
	if (count >= 1 && filtered[count - 1] == '=') padding++;
	if (count >= 2 && filtered[count - 2] == '=') padding++;
	if (padding > 2) {
		free(filtered);
		return false;
	}

	size_t decoded_len = (count / 4) * 3;
	if (padding > 0) decoded_len -= padding;
	unsigned char *decoded = (unsigned char *)yaml_context_alloc(doc->ctx, decoded_len, 1);
	if (!decoded) {
		free(filtered);
		return false;
	}

	size_t out = 0;
	for (size_t i = 0; i < count; i += 4) {
		char c0 = filtered[i];
		char c1 = filtered[i + 1];
		char c2 = filtered[i + 2];
		char c3 = filtered[i + 3];

		if (c0 == '=' || c1 == '=') {
			free(filtered);
			return false;
		}

		int v0 = base64_value((unsigned char)c0);
		int v1 = base64_value((unsigned char)c1);
		if (v0 < 0 || v1 < 0) {
			free(filtered);
			return false;
		}

		if (c2 == '=') {
			if (c3 != '=' || i + 4 != count) {
				free(filtered);
				return false;
			}
			decoded[out++] = (unsigned char)((v0 << 2) | (v1 >> 4));
			break;
		}

		int v2 = base64_value((unsigned char)c2);
		if (v2 < 0) {
			free(filtered);
			return false;
		}

		if (c3 == '=') {
			if (i + 4 != count) {
				free(filtered);
				return false;
			}
			decoded[out++] = (unsigned char)((v0 << 2) | (v1 >> 4));
			decoded[out++] = (unsigned char)(((v1 & 0x0F) << 4) | (v2 >> 2));
			break;
		}

		int v3 = base64_value((unsigned char)c3);
		if (v3 < 0) {
			free(filtered);
			return false;
		}

		decoded[out++] = (unsigned char)((v0 << 2) | (v1 >> 4));
		decoded[out++] = (unsigned char)(((v1 & 0x0F) << 4) | (v2 >> 2));
		decoded[out++] = (unsigned char)(((v2 & 0x03) << 6) | v3);
	}

	free(filtered);
	*out_data = decoded;
	*out_len = decoded_len;
	return true;
}

static const char *base64_encode(
	GTEXT_YAML_Document *doc,
	const unsigned char *data,
	size_t len,
	size_t *out_len
) {
	static const char alphabet[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	if (!doc || (!data && len != 0)) return NULL;
	size_t enc_len = ((len + 2) / 3) * 4;
	char *out = (char *)yaml_context_alloc(doc->ctx, enc_len + 1, 1);
	if (!out) return NULL;

	size_t idx = 0;
	for (size_t i = 0; i < len; i += 3) {
		unsigned int b0 = data[i];
		unsigned int b1 = (i + 1 < len) ? data[i + 1] : 0;
		unsigned int b2 = (i + 2 < len) ? data[i + 2] : 0;

		out[idx++] = alphabet[(b0 >> 2) & 0x3F];
		out[idx++] = alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)];
		if (i + 1 < len) {
			out[idx++] = alphabet[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)];
		} else {
			out[idx++] = '=';
		}
		if (i + 2 < len) {
			out[idx++] = alphabet[b2 & 0x3F];
		} else {
			out[idx++] = '=';
		}
	}

	out[idx] = '\0';
	if (out_len) *out_len = enc_len;
	return out;
}

static bool yaml_use_1_1(const GTEXT_YAML_Document *doc, const GTEXT_YAML_Parse_Options *opts) {
	if (opts && opts->yaml_1_1) return true;
	if (doc && doc->yaml_version_major == 1 && doc->yaml_version_minor == 1) return true;
	return false;
}

static bool has_disallowed_leading_zero(const char *s, size_t len, bool allow_underscore) {
	if (!s || len == 0) return false;
	char *clean = strip_underscores(s, len, allow_underscore);
	if (!clean) return false;
	const char *p = clean;
	if (*p == '+' || *p == '-') p++;
	bool result = false;
	if (p[0] == '0' && p[1] != '\0') {
		if (p[1] == 'x' || p[1] == 'X' || p[1] == 'o' || p[1] == 'O' || p[1] == 'b' || p[1] == 'B') {
			result = false;
		} else if (p[1] >= '0' && p[1] <= '9') {
			result = true;
		}
	}
	free(clean);
	return result;
}

static bool parse_sexagesimal_value(
	const char *s,
	size_t len,
	bool allow_underscore,
	double *out,
	bool *out_is_int
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
	if (*p == '\0' || strchr(p, ':') == NULL) {
		free(clean);
		return false;
	}

	double total = 0.0;
	bool has_fraction = false;
	while (*p != '\0') {
		const char *colon = strchr(p, ':');
		bool last = colon == NULL;
		size_t seg_len = last ? strlen(p) : (size_t)(colon - p);
		if (seg_len == 0) {
			free(clean);
			return false;
		}

		double segment = 0.0;
		if (!last) {
			for (size_t i = 0; i < seg_len; i++) {
				if (p[i] < '0' || p[i] > '9') {
					free(clean);
					return false;
				}
				segment = segment * 10.0 + (double)(p[i] - '0');
			}
		} else {
			bool seen_dot = false;
			double frac_scale = 1.0;
			for (size_t i = 0; i < seg_len; i++) {
				char c = p[i];
				if (c == '.') {
					if (seen_dot) {
						free(clean);
						return false;
					}
					seen_dot = true;
					continue;
				}
				if (c < '0' || c > '9') {
					free(clean);
					return false;
				}
				if (!seen_dot) {
					segment = segment * 10.0 + (double)(c - '0');
				} else {
					frac_scale *= 10.0;
					segment += (double)(c - '0') / frac_scale;
					has_fraction = true;
				}
			}
		}

		total = total * 60.0 + segment;
		if (last) break;
		p = colon + 1;
	}

	if (neg) total = -total;
	*out = total;
	if (out_is_int) *out_is_int = !has_fraction;
	free(clean);
	return true;
}

static bool parse_int_value(
	const char *s,
	size_t len,
	bool allow_underscore,
	bool allow_base_prefix,
	bool allow_yaml_1_1_octal,
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

	if (allow_yaml_1_1_octal && p[0] == '0' && p[1] != '\0') {
		bool octal = true;
		for (size_t i = 1; p[i] != '\0'; i++) {
			if (p[i] < '0' || p[i] > '7') {
				octal = false;
				break;
			}
		}
		if (octal) {
			errno = 0;
			char *end = NULL;
			long long parsed = strtoll(p, &end, 8);
			if (errno == ERANGE || end == p || (end && *end != '\0')) {
				free(clean);
				return false;
			}
			if (neg) parsed = -parsed;
			*out = (int64_t)parsed;
			free(clean);
			return true;
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
	const char *tag_a = scalar_tag_id(a);
	const char *tag_b = scalar_tag_id(b);
	if (tag_a || tag_b) {
		if (!tag_a || !tag_b) return false;
		if (strcmp(tag_a, tag_b) != 0) return false;
	}
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

static const GTEXT_YAML_Custom_Tag *find_custom_tag(
	const GTEXT_YAML_Parse_Options *opts,
	const char *tag
) {
	if (!opts || !tag || !opts->enable_custom_tags) return NULL;
	if (!opts->custom_tags || opts->custom_tag_count == 0) return NULL;
	for (size_t i = 0; i < opts->custom_tag_count; i++) {
		if (!opts->custom_tags[i].tag) continue;
		if (strcmp(opts->custom_tags[i].tag, tag) == 0) {
			return &opts->custom_tags[i];
		}
	}
	return NULL;
}

static GTEXT_YAML_Status apply_custom_tag_constructor(
	GTEXT_YAML_Document *doc,
	GTEXT_YAML_Node *node,
	const GTEXT_YAML_Parse_Options *opts,
	const char *tag,
	GTEXT_YAML_Error *error
) {
	const GTEXT_YAML_Custom_Tag *handler = find_custom_tag(opts, tag);
	if (!handler || !handler->construct) return GTEXT_YAML_OK;
	return handler->construct(doc, node, tag, handler->user, error);
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
			bool yaml_1_1 = yaml_use_1_1(doc, opts);
			if (!parse_bool_value(value, len, false, yaml_1_1, &out)) {
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
			bool yaml_1_1 = yaml_use_1_1(doc, opts);
			if (yaml_1_1) {
				double sexa = 0.0;
				bool is_int = false;
				if (parse_sexagesimal_value(value, len, true, &sexa, &is_int)) {
					if (!is_int) {
						if (error) {
							error->code = GTEXT_YAML_E_INVALID;
							error->message = "Invalid integer scalar for explicit tag";
						}
						return GTEXT_YAML_E_INVALID;
					}
					out = (int64_t)sexa;
					node->type = GTEXT_YAML_INT;
					node->as.scalar.type = GTEXT_YAML_INT;
					node->as.scalar.int_value = out;
					return GTEXT_YAML_OK;
				}
			}
			if (!parse_int_value(value, len, true, true, yaml_1_1, &out)) {
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
			bool yaml_1_1 = yaml_use_1_1(doc, opts);
			if (yaml_1_1) {
				double sexa = 0.0;
				bool is_int = false;
				if (parse_sexagesimal_value(value, len, true, &sexa, &is_int)) {
					out = sexa;
					node->type = GTEXT_YAML_FLOAT;
					node->as.scalar.type = GTEXT_YAML_FLOAT;
					node->as.scalar.float_value = out;
					return GTEXT_YAML_OK;
				}
			}
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
		if (strcmp(suffix, "timestamp") == 0) {
			yaml_node_scalar snapshot = node->as.scalar;
			if (!parse_timestamp(value, len, &snapshot)) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid timestamp scalar";
				}
				return GTEXT_YAML_E_INVALID;
			}
			const char *formatted = format_timestamp(doc, &snapshot);
			if (!formatted) {
				if (error) {
					error->code = GTEXT_YAML_E_OOM;
					error->message = "Out of memory normalizing timestamp";
				}
				return GTEXT_YAML_E_OOM;
			}
			node->as.scalar = snapshot;
			node->as.scalar.value = formatted;
			node->as.scalar.length = strlen(formatted);
			return GTEXT_YAML_OK;
		}
		if (strcmp(suffix, "binary") == 0) {
			const unsigned char *data = NULL;
			size_t data_len = 0;
			if (!base64_decode(doc, value, len, &data, &data_len)) {
				if (error) {
					error->code = GTEXT_YAML_E_INVALID;
					error->message = "Invalid base64 binary scalar";
				}
				return GTEXT_YAML_E_INVALID;
			}
			size_t encoded_len = 0;
			const char *encoded = base64_encode(doc, data, data_len, &encoded_len);
			if (!encoded) {
				if (error) {
					error->code = GTEXT_YAML_E_OOM;
					error->message = "Out of memory encoding binary scalar";
				}
				return GTEXT_YAML_E_OOM;
			}
			node->as.scalar.has_binary = true;
			node->as.scalar.binary_data = data;
			node->as.scalar.binary_len = data_len;
			node->as.scalar.value = encoded;
			node->as.scalar.length = encoded_len;
			node->type = GTEXT_YAML_STRING;
			node->as.scalar.type = GTEXT_YAML_STRING;
			return GTEXT_YAML_OK;
		}

		if (opts && opts->enable_custom_tags) {
			GTEXT_YAML_Status custom = apply_custom_tag_constructor(
				doc,
				node,
				opts,
				tag,
				error
			);
			if (custom != GTEXT_YAML_OK) return custom;
		}
		return GTEXT_YAML_OK;
	}

	if (tag && tag[0] != '\0') {
		GTEXT_YAML_Status custom = apply_custom_tag_constructor(doc, node, opts, tag, error);
		if (custom != GTEXT_YAML_OK) return custom;
		return GTEXT_YAML_OK;
	}

	if (opts->schema == GTEXT_YAML_SCHEMA_FAILSAFE) {
		return GTEXT_YAML_OK;
	}

	bool json_only = opts->schema == GTEXT_YAML_SCHEMA_JSON;
	bool allow_underscore = opts->schema == GTEXT_YAML_SCHEMA_CORE;
	bool allow_base_prefix = opts->schema == GTEXT_YAML_SCHEMA_CORE;
	bool yaml_1_1 = yaml_use_1_1(doc, opts) && opts->schema == GTEXT_YAML_SCHEMA_CORE;

	if (parse_null_value(value, len, json_only)) {
		node->type = GTEXT_YAML_NULL;
		node->as.scalar.type = GTEXT_YAML_NULL;
		return GTEXT_YAML_OK;
	}

	bool bool_out = false;
	if (parse_bool_value(value, len, json_only, yaml_1_1, &bool_out)) {
		node->type = GTEXT_YAML_BOOL;
		node->as.scalar.type = GTEXT_YAML_BOOL;
		node->as.scalar.bool_value = bool_out;
		return GTEXT_YAML_OK;
	}

	if (yaml_1_1) {
		double sexa = 0.0;
		bool is_int = false;
		if (parse_sexagesimal_value(value, len, allow_underscore, &sexa, &is_int)) {
			if (is_int) {
				node->type = GTEXT_YAML_INT;
				node->as.scalar.type = GTEXT_YAML_INT;
				node->as.scalar.int_value = (int64_t)sexa;
			} else {
				node->type = GTEXT_YAML_FLOAT;
				node->as.scalar.type = GTEXT_YAML_FLOAT;
				node->as.scalar.float_value = sexa;
			}
			return GTEXT_YAML_OK;
		}
	}

	int64_t int_out = 0;
	if (!yaml_1_1 && has_disallowed_leading_zero(value, len, allow_underscore)) {
		return GTEXT_YAML_OK;
	}
	if (parse_int_value(value, len, allow_underscore, allow_base_prefix, yaml_1_1, &int_out)) {
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
			{
				const char *seq_suffix = tag_suffix(node->as.sequence.tag);
				if (seq_suffix && strcmp(seq_suffix, "omap") == 0) {
					for (size_t i = 0; i < node->as.sequence.count; i++) {
						const GTEXT_YAML_Node *item = deref_alias(node->as.sequence.children[i]);
						if (!item || item->type != GTEXT_YAML_MAPPING || item->as.mapping.count != 1) {
							if (error) {
								error->code = GTEXT_YAML_E_INVALID;
								error->message = "omap entries must be single-pair mappings";
							}
							return GTEXT_YAML_E_INVALID;
						}
						const GTEXT_YAML_Node *key = item->as.mapping.pairs[0].key;
						for (size_t j = 0; j < i; j++) {
							const GTEXT_YAML_Node *prev = deref_alias(node->as.sequence.children[j]);
							if (!prev || prev->type != GTEXT_YAML_MAPPING) continue;
							if (nodes_equal(key, prev->as.mapping.pairs[0].key, 0, opts ? opts->max_depth : 0)) {
								if (error) {
									error->code = GTEXT_YAML_E_DUPKEY;
									error->message = "omap keys must be unique";
								}
								return GTEXT_YAML_E_DUPKEY;
							}
						}
					}
				} else if (seq_suffix && strcmp(seq_suffix, "pairs") == 0) {
					for (size_t i = 0; i < node->as.sequence.count; i++) {
						const GTEXT_YAML_Node *item = deref_alias(node->as.sequence.children[i]);
						if (!item || item->type != GTEXT_YAML_MAPPING || item->as.mapping.count != 1) {
							if (error) {
								error->code = GTEXT_YAML_E_INVALID;
								error->message = "pairs entries must be single-pair mappings";
							}
							return GTEXT_YAML_E_INVALID;
						}
					}
				}
			}
			{
				const char *tag = node->as.sequence.tag;
				GTEXT_YAML_Status custom = apply_custom_tag_constructor(
					doc,
					node,
					opts,
					tag,
					error
				);
				if (custom != GTEXT_YAML_OK) return custom;
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
			{
				const char *map_suffix = tag_suffix(node->as.mapping.tag);
				if (map_suffix && strcmp(map_suffix, "set") == 0) {
					for (size_t i = 0; i < node->as.mapping.count; i++) {
						if (!node_is_null(node->as.mapping.pairs[i].value)) {
							if (error) {
								error->code = GTEXT_YAML_E_INVALID;
								error->message = "set values must be null";
							}
							return GTEXT_YAML_E_INVALID;
						}
					}
				}
			}
			{
				const char *tag = node->as.mapping.tag;
				GTEXT_YAML_Status custom = apply_custom_tag_constructor(
					doc,
					node,
					opts,
					tag,
					error
				);
				if (custom != GTEXT_YAML_OK) return custom;
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
