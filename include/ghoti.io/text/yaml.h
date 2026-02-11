/**
 * @file yaml.h
 * @brief Public umbrella header for the YAML module.
 *
 * This header includes the YAML public sub-headers so callers can simply
 * include @c <ghoti.io/text/yaml.h> to access the YAML API. The sub-headers
 * provide the core types, DOM inspection API, streaming parser, and writer.
 *
 * Usage:
 * - From a C source: #include <ghoti.io/text/yaml.h>
 * - From C++ sources the headers are C-linkage guarded.
 *
 * Copyright 2026 by Corey Pennycuff
 */

#ifndef GHOTI_IO_TEXT_YAML_H
#define GHOTI_IO_TEXT_YAML_H

/* Core types and definitions */
#include <ghoti.io/text/yaml/yaml_core.h>

/* Public module headers */
#include <ghoti.io/text/yaml/yaml_dom.h>
#include <ghoti.io/text/yaml/yaml_stream.h>
#include <ghoti.io/text/yaml/yaml_writer.h>

#endif // GHOTI_IO_TEXT_YAML_H
