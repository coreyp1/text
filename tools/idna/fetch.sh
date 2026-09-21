#!/bin/sh
#
# Fetch the Unicode Character Database the IDNA tables are derived from.
#
# The UCD files are not committed: they are large, they are reproducible from
# a version number and a URL, and committing them would make this repository
# the second-best copy of somebody else's data. The *generated* table is
# committed instead, so that a build needs neither the network nor Python.
#
# Only the files RFC 5892's derivation and RFC 5893's bidi rule actually read
# are fetched. That is a short list on purpose: this library validates host
# names, it is not a Unicode library, and a fetch script that pulled the whole
# UCD would invite it to become one.
#
# The version is read from tools/idna/UCD_VERSION, which is the one place it
# is written down. Everything lands in third_party/ucd/<version>/, which
# .gitignore excludes.
#
# Usage:  tools/idna/fetch.sh [version]
#
# Copyright 2026 by Corey Pennycuff

set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
version=${1:-$(cat "$root/tools/idna/UCD_VERSION")}
dest="$root/third_party/ucd/$version"
base="https://www.unicode.org/Public/$version/ucd"

# The directory structure is flattened on disk: the generator asks for
# "DerivedGeneralCategory.txt", not for the "extracted/" it happens to live
# under upstream.
files="
DerivedCoreProperties.txt
DerivedNormalizationProps.txt
PropList.txt
Blocks.txt
HangulSyllableType.txt
Scripts.txt
extracted/DerivedGeneralCategory.txt
extracted/DerivedJoiningType.txt
extracted/DerivedBidiClass.txt
extracted/DerivedCombiningClass.txt
"

mkdir -p "$dest"
for path in $files; do
	name=$(basename "$path")
	if [ -s "$dest/$name" ]; then
		printf 'have    %s\n' "$name"
		continue
	fi
	printf 'fetch   %s\n' "$name"
	# --fail so that an HTML error page never lands on disk looking like data.
	curl --fail --silent --show-error --location \
		--output "$dest/$name.partial" "$base/$path"
	mv "$dest/$name.partial" "$dest/$name"
done

printf '\nUCD %s is in %s\n' "$version" "$dest"
