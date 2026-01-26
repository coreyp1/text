/**
 * @file json_patch.c
 * @brief JSON Patch (RFC 6902) and Merge Patch (RFC 7386) example
 *
 * This example demonstrates:
 * - Applying JSON Patch operations
 * - Applying JSON Merge Patch
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
  // Original document
  const char * doc_json = "{\"name\":\"Eve\",\"age\":35,\"city\":\"Boston\"}";
  GTEXT_JSON_Parse_Options opt = gtext_json_parse_options_default();
  GTEXT_JSON_Error err = {0};

  GTEXT_JSON_Value * doc =
      gtext_json_parse(doc_json, strlen(doc_json), &opt, &err);
  if (!doc) {
    fprintf(stderr, "Parse error: %s\n", err.message);
    gtext_json_error_free(&err);
    return 1;
  }

  // JSON Patch: array of operations
  const char * patch_json =
      "["
      "{\"op\":\"replace\",\"path\":\"/age\",\"value\":36},"
      "{\"op\":\"add\",\"path\":\"/country\",\"value\":\"USA\"},"
      "{\"op\":\"remove\",\"path\":\"/city\"}"
      "]";

  GTEXT_JSON_Value * patch =
      gtext_json_parse(patch_json, strlen(patch_json), &opt, &err);
  if (!patch) {
    fprintf(stderr, "Patch parse error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_free(doc);
    return 1;
  }

  // Apply patch
  if (gtext_json_patch_apply(doc, patch, &err) != GTEXT_JSON_OK) {
    fprintf(stderr, "Patch apply error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_free(patch);
    gtext_json_free(doc);
    return 1;
  }

  printf("After JSON Patch:\n");
  GTEXT_JSON_Sink sink;
  gtext_json_sink_buffer(&sink);
  GTEXT_JSON_Write_Options write_opt = gtext_json_write_options_default();
  write_opt.pretty = true;
  gtext_json_write_value(&sink, &write_opt, doc, NULL);
  printf("%s\n\n", gtext_json_sink_buffer_data(&sink));
  gtext_json_sink_buffer_free(&sink);

  // JSON Merge Patch
  const char * merge_json = "{\"age\":37,\"city\":\"New York\"}";
  GTEXT_JSON_Value * merge_patch =
      gtext_json_parse(merge_json, strlen(merge_json), &opt, &err);
  if (!merge_patch) {
    fprintf(stderr, "Merge patch parse error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_free(patch);
    gtext_json_free(doc);
    return 1;
  }

  // Apply merge patch
  if (gtext_json_merge_patch(doc, merge_patch, &err) != GTEXT_JSON_OK) {
    fprintf(stderr, "Merge patch error: %s\n", err.message);
    gtext_json_error_free(&err);
    gtext_json_free(merge_patch);
    gtext_json_free(patch);
    gtext_json_free(doc);
    return 1;
  }

  printf("After JSON Merge Patch:\n");
  gtext_json_sink_buffer(&sink);
  gtext_json_write_value(&sink, &write_opt, doc, NULL);
  printf("%s\n", gtext_json_sink_buffer_data(&sink));

  // Cleanup
  gtext_json_sink_buffer_free(&sink);
  gtext_json_free(merge_patch);
  gtext_json_free(patch);
  gtext_json_free(doc);

  return 0;
}
