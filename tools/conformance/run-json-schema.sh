#!/bin/sh
# Score this library's schema engine against JSON-Schema-Test-Suite.
#
#   tools/conformance/run-json-schema.sh
#
# The suite is cloned into build/json-schema-test-suite on first use and
# checked out at the commit named in tools/conformance/JSON_SCHEMA_COMMIT.
# The commit is pinned rather than tracking the default branch for the reason
# run-json.sh does not need to care about: this score is a percentage, and a
# percentage that moves because somebody else added test cases is not a
# measurement of anything here. `regex` pins the same commit, so the two
# libraries' numbers are about the same corpus.
#
# JSS_MIN sets a floor the required-suite rate must meet; JSS_REPORT names a
# file to write the wrong answers to; JSS_DRAFT picks the draft directory.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
commit=$(cat "$root/tools/conformance/JSON_SCHEMA_COMMIT")
suite=${JSS_SUITE:-$root/build/json-schema-test-suite}
draft=${JSS_DRAFT:-draft2020-12}

if [ ! -d "$suite/.git" ]; then
	echo "fetching JSON-Schema-Test-Suite into $suite"
	git clone https://github.com/json-schema-org/JSON-Schema-Test-Suite.git "$suite"
fi
if [ "$(git -C "$suite" rev-parse HEAD)" != "$commit" ]; then
	git -C "$suite" fetch --quiet origin "$commit" 2>/dev/null || git -C "$suite" fetch --quiet
	git -C "$suite" checkout --quiet "$commit"
fi

[ -n "$PREFIX" ] || { echo "PREFIX must be set, as for any build here" >&2; exit 1; }
pc="$PREFIX/share/pkgconfig:$PREFIX/lib/pkgconfig"
cflags=$(PKG_CONFIG_PATH="$pc" pkg-config --cflags ghoti.io-cutil-0 ghoti.io-chron-0)
libs=$(PKG_CONFIG_PATH="$pc" pkg-config --libs ghoti.io-cutil-0 ghoti.io-chron-0)

# `pattern` and `patternProperties` are regular expressions, and this library
# has no engine - the caller supplies one. ghoti.io-regex is the one written
# for it, so the score is measured with it when it is installed. Without it
# those groups are refused like any other unenforceable keyword, and the
# report says so rather than quietly scoring a smaller corpus.
if PKG_CONFIG_PATH="$pc" pkg-config --exists ghoti.io-regex-0; then
	cflags="$cflags -DGTEXT_CONFORMANCE_HAVE_REGEX $(PKG_CONFIG_PATH="$pc" pkg-config --cflags ghoti.io-regex-0)"
	libs="$libs $(PKG_CONFIG_PATH="$pc" pkg-config --libs ghoti.io-regex-0)"
	echo "regular expressions: ghoti.io-regex"
else
	echo "regular expressions: none installed - pattern groups will not run"
fi

archive=$(ls "$root"/build/*/release/apps/*.a 2>/dev/null | head -1)
[ -n "$archive" ] || { echo "build the library first (make)" >&2; exit 1; }
generated=$(dirname "$(dirname "$archive")")/generated
runner=$root/build/json-schema-runner
cc -O1 -o "$runner" "$root/tools/conformance/json_schema_suite.c" \
	-I"$root/include" -I"$generated" $cflags "$archive" $libs -lm \
	-Wl,-rpath,"$PREFIX/lib/ghoti.io"

JSS_COMMIT="$commit" exec python3 "$root/tools/conformance/json_schema_suite.py" \
	"$suite" "$runner" --draft "$draft"
