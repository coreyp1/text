#!/bin/sh
#
# Fetch the Unicode Character Database the IDNA tables are derived from.
#
# The UCD files are not committed: they are large, they are reproducible from
# a version number and a URL, and committing them would make this repository
# the second-best copy of somebody else's data. The *generated* table is
# committed instead, so that a build needs neither the network nor Python.
#
# Only the files RFC 5892's derivation, RFC 5893's bidi rule, UTS #46's mapping
# step and NFC actually read are fetched. That is a short list on purpose: this
# library validates host names, it is not a Unicode library, and a fetch script
# that pulled the whole UCD would invite it to become one.
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
UnicodeData.txt
CompositionExclusions.txt
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

#
# UTS #46's mapping table, which versions on its own schedule.
#
# It is not part of the UCD and it lags it: there is no 17.0.0 of it at the
# time of writing, and pinning it to the UCD version would have made this
# script fail on a 404 rather than fetch the current one. Two data sets, two
# version files.
#
# The skew is harmless here because the two are used for different questions.
# The mapping table is read only for the characters it *changes* - mapped and
# ignored - and RFC 5892, derived from the UCD above, decides what is valid. A
# character the UCD has and the mapping table does not is simply one the
# mapping step leaves alone, which is what an unlisted character means anyway.
#
mapping_version=${2:-$(cat "$root/tools/idna/IDNA_MAPPING_VERSION")}
mapping_dest="$root/third_party/idna/$mapping_version"
mkdir -p "$mapping_dest"
if [ -s "$mapping_dest/IdnaMappingTable.txt" ]; then
	printf 'have    %s\n' "IdnaMappingTable.txt"
else
	printf 'fetch   %s\n' "IdnaMappingTable.txt"
	curl --fail --silent --show-error --location \
		--output "$mapping_dest/IdnaMappingTable.txt.partial" \
		"https://www.unicode.org/Public/idna/$mapping_version/IdnaMappingTable.txt"
	mv "$mapping_dest/IdnaMappingTable.txt.partial" \
		"$mapping_dest/IdnaMappingTable.txt"
fi

printf 'IDNA mapping %s is in %s\n' "$mapping_version" "$mapping_dest"
