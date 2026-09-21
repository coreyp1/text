/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Text.
 *
 * Ghoti.io Text is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * Ghoti.io Text is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Internal utility functions for JSON module.
 */

#include <string.h>

#include <ghoti.io/text/macros.h>
#include "json_internal.h"
// Check if a length-delimited string exactly equals a null-terminated keyword
int json_matches(const char * input, size_t len, const char * keyword) {
  size_t keyword_len = strlen(keyword);
  return len == keyword_len && memcmp(input, keyword, keyword_len) == 0;
}
