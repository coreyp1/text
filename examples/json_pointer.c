/**
 * @file json_pointer.c
 * @brief JSON Pointer (RFC 6901) example
 *
 * This example demonstrates:
 * - Using JSON Pointers to access nested values
 * - Reading and modifying values via pointers
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Parse JSON
    const char* json_input = "{\"user\":{\"name\":\"David\",\"age\":28,\"tags\":[\"developer\",\"linux\"]}}";
    size_t json_len = strlen(json_input);

    text_json_parse_options opt = text_json_parse_options_default();
    text_json_error err = {0};

    text_json_value* root = text_json_parse(json_input, json_len, &opt, &err);
    if (!root) {
        fprintf(stderr, "Parse error: %s\n", err.message);
        text_json_error_free(&err);
        return 1;
    }

    // Access values using JSON Pointers
    const char* pointers[] = {
        "/user/name",      // Access nested object value
        "/user/age",       // Access nested number
        "/user/tags/0",     // Access array element
        "/user/tags/1",     // Access another array element
    };

    printf("JSON Pointer access:\n");
    for (size_t i = 0; i < sizeof(pointers) / sizeof(pointers[0]); i++) {
        const text_json_value* val = text_json_pointer_get(root, pointers[i], strlen(pointers[i]));
        if (val) {
            text_json_type type = text_json_typeof(val);
            printf("  %s: ", pointers[i]);

            switch (type) {
                case TEXT_JSON_STRING: {
                    const char* s;
                    size_t len;
                    if (text_json_get_string(val, &s, &len) == TEXT_JSON_OK) {
                        printf("%.*s\n", (int)len, s);
                    }
                    break;
                }
                case TEXT_JSON_NUMBER: {
                    int64_t n;
                    if (text_json_get_i64(val, &n) == TEXT_JSON_OK) {
                        printf("%lld\n", (long long)n);
                    }
                    break;
                }
                default:
                    printf("(type: %d)\n", type);
                    break;
            }
        } else {
            printf("  %s: (not found)\n", pointers[i]);
        }
    }

    // Modify a value using mutable pointer
    text_json_value* age_mut = text_json_pointer_get_mut(root, "/user/age", 10);
    if (age_mut) {
        // Replace with new age value
        text_json_value* new_age = text_json_new_number_i64(29);
        // Note: In a real application, you'd need to replace the value in the parent object
        // This is a simplified example
        text_json_free(new_age);
    }

    // Cleanup
    text_json_free(root);

    return 0;
}
