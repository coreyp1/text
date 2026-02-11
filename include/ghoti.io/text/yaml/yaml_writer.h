/**
 * @file yaml_writer.h
 * @brief Document emission and sink helpers for YAML.
 *
 * The writer API serializes a DOM into a caller-provided sink. The
 * sink abstraction mirrors other modules in the repository and allows
 * writing to growable buffers, fixed buffers, or custom callbacks.
 */

#ifndef GHOTI_IO_TEXT_YAML_WRITER_H
#define GHOTI_IO_TEXT_YAML_WRITER_H

#include <ghoti.io/text/yaml/yaml_core.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GTEXT_YAML_Sink GTEXT_YAML_Sink;

/**
 * @brief Serialize @p doc to @p sink using @p opts.
 *
 * If @p opts is NULL default write options are used. The function returns
 * GTEXT_YAML_OK on success or a writer/sink error (GTEXT_YAML_E_WRITE).
 */
GTEXT_API GTEXT_YAML_Status gtext_yaml_write_document(const GTEXT_YAML_Document * doc, GTEXT_YAML_Sink * sink, const GTEXT_YAML_Write_Options * opts);

#ifdef __cplusplus
}
#endif

#endif // GHOTI_IO_TEXT_YAML_WRITER_H
