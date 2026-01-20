/**
 * @file json_schema.c
 * @brief JSON Schema validation example
 *
 * This example demonstrates:
 * - Compiling a JSON Schema
 * - Validating JSON values against a schema
 * - Handling validation errors
 */

#include <ghoti.io/text/json.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    // Schema definition
    const char* schema_json = "{"
        "\"type\":\"object\","
        "\"properties\":{"
            "\"name\":{\"type\":\"string\",\"minLength\":1},"
            "\"age\":{\"type\":\"number\",\"minimum\":0,\"maximum\":150},"
            "\"email\":{\"type\":\"string\"}"
        "},"
        "\"required\":[\"name\",\"age\"]"
    "}";

    text_json_parse_options opt = text_json_parse_options_default();
    text_json_error err = {0};

    // Parse and compile schema
    text_json_value* schema_doc = text_json_parse(schema_json, strlen(schema_json), &opt, &err);
    if (!schema_doc) {
        fprintf(stderr, "Schema parse error: %s\n", err.message);
        text_json_error_free(&err);
        return 1;
    }

    text_json_schema* schema = text_json_schema_compile(schema_doc, &err);
    text_json_free(schema_doc);  // Schema document no longer needed after compilation
    if (!schema) {
        fprintf(stderr, "Schema compile error: %s\n", err.message);
        text_json_error_free(&err);
        return 1;
    }

    // Test cases
    struct {
        const char* name;
        const char* json;
    } test_cases[] = {
        {
            "Valid document",
            "{\"name\":\"Frank\",\"age\":42,\"email\":\"frank@example.com\"}"
        },
        {
            "Missing required field",
            "{\"name\":\"Frank\"}"
        },
        {
            "Invalid type",
            "{\"name\":\"Frank\",\"age\":\"not a number\"}"
        },
        {
            "Value out of range",
            "{\"name\":\"Frank\",\"age\":200}"
        }
    };

    printf("Schema validation results:\n\n");
    for (size_t i = 0; i < sizeof(test_cases) / sizeof(test_cases[0]); i++) {
        printf("Test: %s\n", test_cases[i].name);
        printf("JSON: %s\n", test_cases[i].json);

        text_json_value* instance = text_json_parse(
            test_cases[i].json,
            strlen(test_cases[i].json),
            &opt,
            &err
        );
        if (!instance) {
            printf("Parse error: %s\n\n", err.message);
            text_json_error_free(&err);
            continue;
        }

        if (text_json_schema_validate(schema, instance, &err) == TEXT_JSON_OK) {
            printf("Result: VALID\n\n");
        } else {
            printf("Result: INVALID - %s\n", err.message);
            if (err.context_snippet) {
                printf("Context: %s\n", err.context_snippet);
            }
            printf("\n");
            text_json_error_free(&err);
        }

        text_json_free(instance);
    }

    // Cleanup
    text_json_schema_free(schema);

    return 0;
}
