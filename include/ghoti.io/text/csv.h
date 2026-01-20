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

#ifndef GHOTI_IO_TEXT_CSV_H
#define GHOTI_IO_TEXT_CSV_H

// Include core types and definitions
#include <ghoti.io/text/csv/csv_core.h>

// CSV module headers
#include <ghoti.io/text/csv/csv_table.h>
#include <ghoti.io/text/csv/csv_stream.h>
#include <ghoti.io/text/csv/csv_writer.h>

#endif /* GHOTI_IO_TEXT_CSV_H */
