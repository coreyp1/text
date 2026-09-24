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
 * @file json.h
 * @brief JSON parsing and serialization
 *
 * This header serves as the umbrella header for JSON functionality.
 * It includes all JSON module headers for convenience.
 *
 * For internal implementations that only need core types, use
 * <ghoti.io/text/json/json_core.h> instead to reduce compile-time dependencies.
 */

#ifndef GHOTI_IO_GTEXT_JSON_H
#define GHOTI_IO_GTEXT_JSON_H

#include <ghoti.io/text/macros.h>

// Include core types and definitions
#include <ghoti.io/text/json/json_core.h>

// Include all JSON module headers
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_patch.h>
#include <ghoti.io/text/json/json_path.h>
#include <ghoti.io/text/json/json_pointer.h>
#include <ghoti.io/text/json/json_schema.h>
#include <ghoti.io/text/json/json_stream.h>
#include <ghoti.io/text/json/json_writer.h>

#endif // GHOTI_IO_GTEXT_JSON_H
