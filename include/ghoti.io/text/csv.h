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
 * @file csv.h
 * @brief CSV parsing and serialization
 *
 * This header serves as the umbrella header for CSV functionality.
 * It includes all CSV module headers for convenience.
 *
 * For internal implementations that only need core types, use
 * <ghoti.io/text/csv/csv_core.h> instead to reduce compile-time dependencies.
 */

#ifndef GHOTI_IO_GTEXT_CSV_H
#define GHOTI_IO_GTEXT_CSV_H

#include <ghoti.io/text/macros.h>

// Include core types and definitions
#include <ghoti.io/text/csv/csv_core.h>

// CSV module headers
#include <ghoti.io/text/csv/csv_stream.h>
#include <ghoti.io/text/csv/csv_table.h>
#include <ghoti.io/text/csv/csv_writer.h>

#endif // GHOTI_IO_GTEXT_CSV_H
