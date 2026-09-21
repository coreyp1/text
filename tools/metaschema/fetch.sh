#!/bin/sh
#
# Fetch the published JSON Schema 2020-12 meta-schemas.
#
# These nine documents are what a `$ref` to
# https://json-schema.org/draft/2020-12/schema and the seven vocabulary
# meta-schemas beneath it resolve to. The library embeds them, so that
# validating a schema against its own dialect needs neither a resolver nor a
# socket; tools/metaschema/gen_metaschema.py turns what this script fetches
# into the committed C file, and `make check-metaschema` fails if the two
# disagree.
#
# This is the opposite of what tools/idna/fetch.sh does with the UCD, and the
# difference is the point. The UCD versions: 17.0.0 supersedes 16.0.0, and a
# committed copy would be a stale copy of somebody else's data. These URIs do
# not version. The whole reference model of 2020-12 rests on each of them
# naming one fixed document forever, so there is no drift to be behind - and
# the alternative to embedding them is a validator that opens a connection in
# the middle of a compile, to a URI it read out of the document it was handed.
#
# format-assertion is fetched although the root meta-schema does not reference
# it: it is the meta-schema of the one optional vocabulary in the dialect, and
# a schema that declares that vocabulary refers to it.
#
# Everything lands in third_party/json-schema/2020-12/, which .gitignore
# excludes.
#
# Usage:  tools/metaschema/fetch.sh
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
dest="$root/third_party/json-schema/2020-12"
base="https://json-schema.org/draft/2020-12"

mkdir -p "$dest/meta"

# json-schema.org negotiates on Accept, and a request that does not ask for
# JSON is answered with the human-readable page about the document rather than
# the document. Asking explicitly is not politeness, it is the difference
# between fetching a schema and fetching HTML that describes one.
fetch() {
	url="$1"
	out="$2"
	printf '  %s\n' "$url"
	if command -v curl >/dev/null 2>&1; then
		curl -fsSL -H 'Accept: application/schema+json' -o "$out" "$url"
	elif command -v wget >/dev/null 2>&1; then
		wget -q --header='Accept: application/schema+json' -O "$out" "$url"
	else
		echo "need curl or wget" >&2
		exit 1
	fi
	# A negotiated HTML page is still a 200, so the transfer succeeding says
	# nothing. Every one of these documents is a JSON object.
	case $(head -c 1 "$out") in
	'{') ;;
	*)
		echo "$url did not return a JSON object" >&2
		exit 1
		;;
	esac
}

echo "fetching the 2020-12 meta-schemas into $dest"
fetch "$base/schema" "$dest/schema.json"
for name in core applicator unevaluated validation meta-data \
	format-annotation format-assertion content; do
	fetch "$base/meta/$name" "$dest/meta/$name.json"
done
echo "done; regenerate with tools/metaschema/gen_metaschema.py"
