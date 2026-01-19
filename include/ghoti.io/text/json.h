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

#ifndef GHOTI_IO_TEXT_JSON_H
#define GHOTI_IO_TEXT_JSON_H

// Include core types and definitions
#include <ghoti.io/text/json/json_core.h>

// Include all JSON module headers
#include <ghoti.io/text/json/json_dom.h>
#include <ghoti.io/text/json/json_writer.h>
#include <ghoti.io/text/json/json_stream.h>
#include <ghoti.io/text/json/json_pointer.h>
#include <ghoti.io/text/json/json_patch.h>
#include <ghoti.io/text/json/json_schema.h>

#endif /* GHOTI_IO_TEXT_JSON_H */
