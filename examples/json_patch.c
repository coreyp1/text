/**
 * @file json_patch.c
 * @brief JSON Patch (RFC 6902) and Merge Patch (RFC 7386) example
 *
 * This example demonstrates:
 * - Applying JSON Patch operations
 * - Applying JSON Merge Patch
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Original document
    const char* doc_json = "{\"name\":\"Eve\",\"age\":35,\"city\":\"Boston\"}";
    text_json_parse_options opt = text_json_parse_options_default();
    text_json_error err = {0};

    text_json_value* doc = text_json_parse(doc_json, strlen(doc_json), &opt, &err);
    if (!doc) {
        fprintf(stderr, "Parse error: %s\n", err.message);
        text_json_error_free(&err);
        return 1;
    }

    // JSON Patch: array of operations
    const char* patch_json = "["
        "{\"op\":\"replace\",\"path\":\"/age\",\"value\":36},"
        "{\"op\":\"add\",\"path\":\"/country\",\"value\":\"USA\"},"
        "{\"op\":\"remove\",\"path\":\"/city\"}"
    "]";

    text_json_value* patch = text_json_parse(patch_json, strlen(patch_json), &opt, &err);
    if (!patch) {
        fprintf(stderr, "Patch parse error: %s\n", err.message);
        text_json_error_free(&err);
        text_json_free(doc);
        return 1;
    }

    // Apply patch
    if (text_json_patch_apply(doc, patch, &err) != TEXT_JSON_OK) {
        fprintf(stderr, "Patch apply error: %s\n", err.message);
        text_json_error_free(&err);
        text_json_free(patch);
        text_json_free(doc);
        return 1;
    }

    printf("After JSON Patch:\n");
    text_json_sink sink;
    text_json_sink_buffer(&sink);
    text_json_write_options write_opt = text_json_write_options_default();
    write_opt.pretty = 1;
    text_json_write_value(&sink, &write_opt, doc, NULL);
    printf("%s\n\n", text_json_sink_buffer_data(&sink));
    text_json_sink_buffer_free(&sink);

    // JSON Merge Patch
    const char* merge_json = "{\"age\":37,\"city\":\"New York\"}";
    text_json_value* merge_patch = text_json_parse(merge_json, strlen(merge_json), &opt, &err);
    if (!merge_patch) {
        fprintf(stderr, "Merge patch parse error: %s\n", err.message);
        text_json_error_free(&err);
        text_json_free(patch);
        text_json_free(doc);
        return 1;
    }

    // Apply merge patch
    if (text_json_merge_patch(doc, merge_patch, &err) != TEXT_JSON_OK) {
        fprintf(stderr, "Merge patch error: %s\n", err.message);
        text_json_error_free(&err);
        text_json_free(merge_patch);
        text_json_free(patch);
        text_json_free(doc);
        return 1;
    }

    printf("After JSON Merge Patch:\n");
    text_json_sink_buffer(&sink);
    text_json_write_value(&sink, &write_opt, doc, NULL);
    printf("%s\n", text_json_sink_buffer_data(&sink));

    // Cleanup
    text_json_sink_buffer_free(&sink);
    text_json_free(merge_patch);
    text_json_free(patch);
    text_json_free(doc);

    return 0;
}
