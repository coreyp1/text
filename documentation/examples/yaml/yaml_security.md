@page example_yaml_security yaml_security.c - Security Limits and Validation

# yaml_security.c - Security Limits and Validation

This example demonstrates security best practices when parsing untrusted YAML input: enforcing depth limits, byte limits, alias expansion limits, and proper error handling.

## What This Example Demonstrates

- **Parse options configuration** - Set security limits before parsing
- **Depth limits** - Prevent stack overflow from deeply nested structures
- **Byte limits** - Prevent memory exhaustion from large documents
- **Alias expansion limits** - Prevent decompression bomb attacks
- **Error handling** - Detecting and reporting limit violations
- **Secure defaults** - Using MODE_CONFIG for restrictive parsing

## Use Case

**Always use security limits when parsing untrusted YAML!**

Common scenarios:
- User-uploaded configuration files
- Data from network requests
- CI/CD pipeline configurations
- Container orchestration manifests

Without limits, attackers can:
- Cause stack overflow (billion laughs attack)
- Exhaust memory (large documents)
- Cause exponential expansion (alias bombs)

## Conceptual Source Code

```c
#include <ghoti.io/text/yaml.h>
#include <stdio.h>
#include <string.h>

GTEXT_YAML_Status event_callback(
    GTEXT_YAML_Stream *s,
    const void *event_payload,
    void *user
) {
    // For this example, we're just validating - not processing
    return GTEXT_YAML_OK;
}

int parse_untrusted_yaml(const char *input, size_t len) {
    GTEXT_YAML_Parse_Options opts = {0};
    
    // Configure security limits
    opts.max_depth = 32;              // Max nesting depth
    opts.max_total_bytes = 1024 * 1024; // 1 MB max document size
    opts.max_alias_expansion = 10000;   // Max nodes after alias expansion
    
    // Create parser with security limits
    GTEXT_YAML_Stream *parser = gtext_yaml_stream_new(
        &opts,
        event_callback,
        NULL
    );
    
    if (!parser) {
        fprintf(stderr, "Failed to create parser\n");
        return 1;
    }
    
    // Feed input
    GTEXT_YAML_Status status = gtext_yaml_stream_feed(parser, input, len);
    
    if (status != GTEXT_YAML_OK) {
        fprintf(stderr, "Parse failed: ");
        switch (status) {
            case GTEXT_YAML_E_DEPTH:
                fprintf(stderr, "depth limit exceeded (max: %zu)\n",
                        opts.max_depth);
                break;
            case GTEXT_YAML_E_LIMIT:
                fprintf(stderr, "limit exceeded (byte or alias expansion)\n");
                break;
            case GTEXT_YAML_E_INVALID:
                fprintf(stderr, "invalid YAML syntax\n");
                break;
            default:
                fprintf(stderr, "status code %d\n", status);
                break;
        }
        gtext_yaml_stream_free(parser);
        return 1;
    }
    
    // Finish parsing
    status = gtext_yaml_stream_finish(parser);
    if (status != GTEXT_YAML_OK) {
        fprintf(stderr, "Finish failed: status %d\n", status);
        gtext_yaml_stream_free(parser);
        return 1;
    }
    
    gtext_yaml_stream_free(parser);
    printf("Validation successful - input is safe\n");
    return 0;
}

int main(void) {
    // Example 1: Valid input within limits
    printf("=== Example 1: Valid input ===\n");
    const char *safe_yaml =
        "name: MyApp\n"
        "config:\n"
        "  timeout: 30\n"
        "  retries: 3\n";
    parse_untrusted_yaml(safe_yaml, strlen(safe_yaml));
    
    // Example 2: Depth limit exceeded
    printf("\n=== Example 2: Depth limit exceeded ===\n");
    const char *deep_yaml =
        "a:\n"
        "  b:\n"
        "    c:\n"
        "      d:\n"
        "        e:\n"
        "          f:\n"
        "            g:\n"
        "              h:\n"
        "                i:\n"
        "                  j:\n"
        // ... continues deeply ...
        ;
    parse_untrusted_yaml(deep_yaml, strlen(deep_yaml));
    
    // Example 3: Alias expansion bomb (billion laughs)
    printf("\n=== Example 3: Alias expansion attack ===\n");
    const char *bomb_yaml =
        "a: &a [\"lol\"]\n"
        "b: &b [*a, *a]\n"
        "c: &c [*b, *b]\n"
        "d: &d [*c, *c]\n"
        "e: &e [*d, *d]\n"
        "f: &f [*e, *e]\n"
        "result: *f\n";  // Exponential expansion!
    parse_untrusted_yaml(bomb_yaml, strlen(bomb_yaml));
    
    return 0;
}
```

## Example Output

```
=== Example 1: Valid input ===
Validation successful - input is safe

=== Example 2: Depth limit exceeded ===
Parse failed: depth limit exceeded (max: 32)

=== Example 3: Alias expansion attack ===
Parse failed: limit exceeded (byte or alias expansion)
```

## Key Security Features

### 1. Depth Limit (`max_depth`)

Prevents deeply nested structures that could cause:
- Stack overflow
- Excessive recursion
- Slow parsing performance

**Recommended values:**
- Strict: 16-32 levels
- Moderate: 64 levels
- Permissive: 128 levels

### 2. Byte Limit (`max_total_bytes`)

Prevents memory exhaustion from:
- Very large documents
- Unbounded input streams
- Memory bombs

**Recommended values:**
- Small configs: 64 KB - 256 KB
- Medium docs: 1 MB - 4 MB
- Large allowed: 16 MB - 64 MB

### 3. Alias Expansion Limit (`max_alias_expansion`)

Prevents exponential expansion attacks (billion laughs):
- Aliases can reference other aliases
- Recursive expansion can grow exponentially
- Can cause memory exhaustion quickly

**Recommended values:**
- Strict: 1,000 - 10,000 nodes
- Moderate: 100,000 nodes
- Permissive: 1,000,000 nodes

### 4. Zero Means Default

Setting any limit to `0` uses the library default:
```c
GTEXT_YAML_Parse_Options opts = {0};
opts.max_depth = 0;  // Uses default (64)
```

## Security Best Practices

### Always Validate Untrusted Input

```c
// BAD: No limits
GTEXT_YAML_Stream *parser = gtext_yaml_stream_new(NULL, cb, NULL);

// GOOD: With limits
GTEXT_YAML_Parse_Options opts = {0};
opts.max_depth = 32;
opts.max_total_bytes = 1024 * 1024;
opts.max_alias_expansion = 10000;
GTEXT_YAML_Stream *parser = gtext_yaml_stream_new(&opts, cb, NULL);
```

### Set Limits Based on Use Case

```c
// Configuration files (strict)
opts.max_depth = 16;
opts.max_total_bytes = 256 * 1024;  // 256 KB
opts.max_alias_expansion = 1000;

// Data interchange (moderate)
opts.max_depth = 32;
opts.max_total_bytes = 4 * 1024 * 1024;  // 4 MB
opts.max_alias_expansion = 50000;

// Large document processing (permissive, trusted source)
opts.max_depth = 64;
opts.max_total_bytes = 64 * 1024 * 1024;  // 64 MB
opts.max_alias_expansion = 1000000;
```

### Handle Errors Gracefully

```c
GTEXT_YAML_Status status = gtext_yaml_stream_feed(parser, input, len);
if (status != GTEXT_YAML_OK) {
    // Log the error with context
    fprintf(stderr, "YAML parse failed: status=%d, source=%s\n",
            status, source_name);
    
    // Clean up
    gtext_yaml_stream_free(parser);
    
    // Return error to caller
    return -1;
}
```

### Consider Additional Restrictions

Future options may include:
- Disabling anchors/aliases entirely
- Restricting mapping key types (scalar-only)
- Disabling custom tags
- Limiting string lengths

## Related Examples

- [yaml_streaming_basic.c](@ref example_yaml_streaming_basic) - Basic streaming parser
- [yaml_config_parser.c](@ref example_yaml_config_parser) - Config file parsing
- [Examples Overview](@ref examples) - Return to examples index

## See Also

- [YAML Module - Limits and Security](@ref yaml_module) - Detailed security documentation
- [Parse Options Reference](@ref GTEXT_YAML_Parse_Options) - All available options
