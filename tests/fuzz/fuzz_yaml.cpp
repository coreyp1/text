/**
 * @file
 *
 * libFuzzer harness for the YAML parser.
 *
 * YAML has considerably more state than JSON - anchors, aliases, tags, block
 * scalars, flow collections, multiple documents - so this is the parser where
 * malformed input has the most ways to go wrong.
 *
 * Alias expansion is bounded by max_alias_expansion so that a billion-laughs
 * input is reported as an error rather than consuming the machine; the
 * harness sets a small budget deliberately, since exhausting memory is not
 * the bug class being looked for here.
 *
 * Build with: make fuzz-yaml     Run: make fuzz-run-yaml
 *
 * Copyright 2026 by Corey Pennycuff
 */

#include <cstddef>
#include <cstdint>

extern "C" {
#include <ghoti.io/text/yaml.h>
}

/** Walk a node so the accessors are exercised, not just the parser. */
static void walk(const GTEXT_YAML_Node * n, int depth) {
  if (!n || depth > 300) {
    return;
  }
  switch (gtext_yaml_node_type(n)) {
    case GTEXT_YAML_SEQUENCE: {
      size_t len = gtext_yaml_sequence_length(n);
      for (size_t i = 0; i < len; i++) {
        walk(gtext_yaml_sequence_get(n, i), depth + 1);
      }
      break;
    }
    case GTEXT_YAML_MAPPING: {
      size_t len = gtext_yaml_mapping_size(n);
      for (size_t i = 0; i < len; i++) {
        const GTEXT_YAML_Node * key = nullptr;
        const GTEXT_YAML_Node * value = nullptr;
        if (gtext_yaml_mapping_get_at(n, i, &key, &value)) {
          walk(key, depth + 1);
          walk(value, depth + 1);
        }
      }
      break;
    }
    case GTEXT_YAML_ALIAS:
      // Follow the link but do not recurse through it; an alias may point at
      // an ancestor, and the cycle is the resolver's problem, not ours.
      (void)gtext_yaml_alias_target(n);
      break;
    default:
      (void)gtext_yaml_node_as_string(n);
      break;
  }
  (void)gtext_yaml_node_tag(n);
  (void)gtext_yaml_node_anchor(n);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size) {
  if (size < 2) {
    return 0;
  }

  const uint8_t flags = data[0];
  const char * text = reinterpret_cast<const char *>(data + 1);
  const size_t len = size - 1;

  GTEXT_YAML_Parse_Options opts = gtext_yaml_parse_options_default();
  opts.allow_aliases = (flags & 0x01) != 0;
  opts.resolve_tags = (flags & 0x02) != 0;
  // Bounded so that a deliberately explosive alias graph reports an error
  // instead of exhausting memory.
  opts.max_alias_expansion = 1000;

  GTEXT_YAML_Error err{};
  GTEXT_YAML_Document * doc = gtext_yaml_parse(text, len, &opts, &err);
  if (doc) {
    walk(gtext_yaml_document_root(doc), 0);
    gtext_yaml_free(doc);
  }
  // The error carries a heap-allocated context snippet, owned by us.
  gtext_yaml_error_free(&err);
  return 0;
}
