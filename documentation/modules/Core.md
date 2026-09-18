@page core_module Core Module Documentation

# Core Module Documentation

The Core module provides the foundational utilities and infrastructure for the Ghoti.io Text library. This includes cross-compiler macros, version information API, and platform compatibility features.

---

## 1. Overview

The Core module consists of:

- **Cross-compiler macros** (`macros.h`) - Platform-independent utilities for common C programming patterns
- **Version API** (`text.h`) - Runtime access to library version information
- **Platform compatibility** - Support for GCC, Clang, MSVC, and other compilers

This module is automatically included when you use any part of the text library, as it provides the base macros and utilities used throughout the library.

---

## 2. Cross-Compiler Macros

The `macros.h` header provides macros that work consistently across different compilers and platforms.

### 2.1 Unused Parameter Suppression

**`GTEXT_MAYBE_UNUSED(X)`**

Marks a function parameter as potentially unused, suppressing compiler warnings. Useful for callback functions or functions with parameters reserved for future use.

**Compiler Support:**
- GCC/Clang: Uses `__attribute__((unused))`
- MSVC: Uses `(void)(X)` cast
- Other compilers: No-op (parameter remains as-is)

**Example:**
```c
#include <ghoti.io/text/macros.h>

void callback(int GTEXT_MAYBE_UNUSED(user_data)) {
    // user_data may not be used in all callback implementations
}
```

### 2.2 Deprecated Function Marking

**`GTEXT_DEPRECATED`**

Marks a function as deprecated, generating compiler warnings when the function is used. Helps with API migration and deprecation workflows.

**Compiler Support:**
- GCC/Clang: Uses `__attribute__((deprecated))`
- MSVC: Uses `__declspec(deprecated)`
- Other compilers: No-op

**Example:**
```c
GTEXT_DEPRECATED
void old_function(void);

// Usage will generate a deprecation warning:
// old_function();  // Warning: 'old_function' is deprecated
```

### 2.3 API Export/Import

**`GTEXT_API`**

Marks functions for export from shared libraries. Automatically handles:
- Windows DLL export/import (`__declspec(dllexport)` / `__declspec(dllimport)`)
- Unix symbol visibility (`__attribute__((visibility("default")))`)
- C++ linkage (`extern "C"`)

**Usage:**
- When building the library (`GTEXT_BUILD` defined): Functions are exported
- When using the library: Functions are imported (Windows) or visible (Unix)

**Example:**
```c
GTEXT_API void public_function(void);
GTEXT_API int process_data(const char *data);
```

### 2.4 Internal API for Testing

**`GTEXT_INTERNAL_API`**

Marks internal functions that should only be exported when building for testing. These functions are not part of the public API but may be needed for unit testing.

**Behavior:**
- When `GTEXT_TEST_BUILD` is defined: Same as `GTEXT_API`
- Otherwise: No export marking (function remains internal)

**Example:**
```c
GTEXT_INTERNAL_API void internal_helper_function(void);
```

### 2.5 Array Size Helper

**`GTEXT_ARRAY_SIZE(a)`**

Calculates the number of elements in a statically-allocated array at compile time. This is a compile-time constant and safe to use in constant expressions.

**Important:** Only works with actual arrays, not pointers. Passing a pointer will give incorrect results.

**Example:**
```c
int numbers[10];
size_t count = GTEXT_ARRAY_SIZE(numbers);  // count = 10

// Safe in constant expressions:
#define MAX_ITEMS 100
int items[MAX_ITEMS];
static_assert(GTEXT_ARRAY_SIZE(items) == MAX_ITEMS);
```

### 2.6 Bit Manipulation

**`GTEXT_BIT(x)`**

Creates a bitmask with a single bit set at the specified position (0-based).

**Example:**
```c
unsigned int flags = 0;
flags |= GTEXT_BIT(0);  // Set bit 0: flags = 0x01
flags |= GTEXT_BIT(3);  // Set bit 3: flags = 0x09
flags |= GTEXT_BIT(7);  // Set bit 7: flags = 0x89

// Check if bit is set:
if (flags & GTEXT_BIT(3)) {
    // Bit 3 is set
}
```

---

## 3. Version Information API

The `text.h` header provides runtime access to library version information.

### 3.1 Version Constants

The library defines compile-time version constants:

```c
#define GTEXT_VERSION_MAJOR 0
#define GTEXT_VERSION_MINOR 0
#define GTEXT_VERSION_PATCH 0
```

These can be used for compile-time version checks.

### 3.2 Version Functions

**`gtext_version_major()`**

Returns the major version number as a `uint32_t`.

**`gtext_version_minor()`**

Returns the minor version number as a `uint32_t`.

**`gtext_version_patch()`**

Returns the patch version number as a `uint32_t`.

**`gtext_version_string()`**

Returns a string representation of the version (e.g., `"0.0.0"`). The string is statically allocated and remains valid for the lifetime of the program.

**Example:**
```c
#include <ghoti.io/text/text.h>
#include <stdio.h>

int main(void) {
    printf("Ghoti.io Text Library Version: %s\n", gtext_version_string());
    printf("Version: %u.%u.%u\n",
           gtext_version_major(),
           gtext_version_minor(),
           gtext_version_patch());
    
    // Version checking:
    if (gtext_version_major() > 0 || gtext_version_minor() >= 1) {
        // Use newer API features
    }
    
    return 0;
}
```

---

## 4. Cross-Platform Considerations

The Core module is designed to work across multiple platforms and compilers:

### 4.1 Supported Compilers

- **GCC** (GNU Compiler Collection)
- **Clang** (LLVM Compiler)
- **MSVC** (Microsoft Visual C++)
- **Other C99-compliant compilers** (with limited macro support)

### 4.2 Platform Support

- **Unix-like systems** (Linux, macOS, BSD, etc.)
- **Windows** (with MinGW, MSVC, or Cygwin)
- **Embedded systems** (with appropriate C standard library)

### 4.3 C++ Compatibility

All headers are C++ compatible and use `extern "C"` linkage when included from C++ code. This allows the library to be used from C++ projects without issues.

---

## 5. Usage Examples

### 5.1 Basic Usage

```c
#include <ghoti.io/text/text.h>
#include <ghoti.io/text/macros.h>
#include <stdio.h>

GTEXT_API void example_function(int GTEXT_MAYBE_UNUSED(param)) {
    printf("Library version: %s\n", gtext_version_string());
}

int main(void) {
    example_function(42);
    return 0;
}
```

### 5.2 Version Checking

```c
#include <ghoti.io/text/text.h>

bool supports_feature(void) {
    // Check if library version supports a specific feature
    return gtext_version_major() > 0 ||
           (gtext_version_major() == 0 && gtext_version_minor() >= 1);
}
```

### 5.3 Flag Management

```c
#include <ghoti.io/text/macros.h>

#define FLAG_OPTION1 GTEXT_BIT(0)
#define FLAG_OPTION2 GTEXT_BIT(1)
#define FLAG_OPTION3 GTEXT_BIT(2)

void process_with_flags(unsigned int flags) {
    if (flags & FLAG_OPTION1) {
        // Handle option 1
    }
    if (flags & FLAG_OPTION2) {
        // Handle option 2
    }
    if (flags & FLAG_OPTION3) {
        // Handle option 3
    }
}
```

---

## 6. Best Practices

1. **Always use `GTEXT_API`** for public functions that should be exported from shared libraries.

2. **Use `GTEXT_MAYBE_UNUSED`** instead of `(void)param` casts for better cross-compiler compatibility.

3. **Prefer `GTEXT_ARRAY_SIZE`** over manual `sizeof` calculations for array size, as it's more readable and less error-prone.

4. **Check version at runtime** if your code depends on specific library features or bug fixes.

5. **Use `GTEXT_DEPRECATED`** when deprecating functions to help users migrate to new APIs.

---

@anchor core-thread-safety
## 7. Thread Safety

The rule is the same for JSON, CSV and YAML, and it is the usual one for a C
parser: **no object in this library is thread-safe, and no two threads may
touch the same object at once, but distinct objects share nothing and may be
used concurrently.**

Concretely:

| Used by two threads at once | Safe |
|---|---|
| The same DOM, table, parser, stream, reader or writer | **no** |
| Two DOMs, tables, streams or writers created separately | yes |
| The same DOM read-only from both, with no writer anywhere | yes |
| `gtext_*_parse()` on separate inputs into separate results | yes |
| The version accessors and the `*_options_default()` functions | yes |

This holds because the library keeps no mutable global state. Every parse
writes only into the context, arena or table it was given, and the only
file-scope variables in the sources are `const` tables. It was checked by
searching for non-const file-scope variables rather than assumed - two things
that turned up are worth naming:

- **`gtext_version_string()` used to race with itself.** It formatted the
  version into a function-local static the first time it was called, guarded
  by a second static flag, so two threads calling it at once both saw the flag
  clear and both wrote the buffer. It now returns a compile-time constant, so
  there is no buffer and no race.
- **Number formatting changes the locale.** `LC_NUMERIC` decides whether a
  double is written with `.` or `,`, so the writer forces the C locale around
  each conversion. Where `uselocale()` is available - which includes Linux,
  macOS and the BSDs, and is what this build uses - the change is thread-local
  and invisible to other threads. On a platform without it the writer falls
  back to `setlocale()`, which is **process-global**: another thread
  formatting a number at that moment can see the wrong decimal separator.
  That fallback is a portability compromise, not the intended path.

**A caller-supplied `GTEXT_Allocator` must be thread-safe if the objects using
it are touched from more than one thread.** The library adds no locking of its
own. The default stdlib allocator is thread-safe.

None of this is enforced. There is no internal locking to disable and no
thread-safe build variant; a caller that needs shared access provides its own
mutual exclusion.

## 8. Related Documentation

- [JSON Module](@ref json_module) - JSON parsing and serialization
- [CSV Module](@ref csv_module) - CSV reading and writing
- [Function Index](@ref functions_index) - Complete API reference
- [Main Documentation](@ref index) - Library overview
