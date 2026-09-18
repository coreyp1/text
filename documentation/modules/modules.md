@page modules Modules

# Modules

This section contains detailed documentation for each module in the Ghoti.io
Text library. These pages document the **API**: the types, the functions, the
options and what they do.

@subpage core_module

@subpage json_module

@subpage csv_module

@subpage yaml_module

## Format conformance

The module pages above describe how to call the library. \ref format_references
"Format and specification references" describes what the library will do with
a given byte sequence: which specification each parser implements, which
clauses, where it deviates from other parsers, and what evidence backs each
claim.

- \ref format_json "JSON" — RFC 8259, plus Pointer, Patch and Merge Patch
- \ref format_csv "CSV" — RFC 4180 and the dialect options
- \ref format_yaml "YAML" — YAML 1.2.2, and the known deviations
- \ref format_adding "Adding a format" — the checklist for a new parser

## Quick Links

- [Function Index](@ref functions_index) - Browse all library functions
- [Examples](@ref examples) - Example programs demonstrating library usage
