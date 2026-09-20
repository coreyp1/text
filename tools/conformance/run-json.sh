#!/bin/sh
# Score this library against JSONTestSuite.
#
#   tools/conformance/run-json.sh
#
# The suite is cloned into build/json-test-suite on first use and reused after
# that.  JTS_MIN sets a floor the score must meet; JTS_REPORT names a file to
# write the failing cases to.  Mirrors run.sh, which does the same for YAML.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
suite=${JTS_SUITE:-$root/build/json-test-suite}

if [ ! -d "$suite/test_parsing" ]; then
	echo "fetching JSONTestSuite into $suite"
	git clone --depth 1 https://github.com/nst/JSONTestSuite.git "$suite"
fi

[ -n "$PREFIX" ] || { echo "PREFIX must be set, as for any build here" >&2; exit 1; }
cflags=$(PKG_CONFIG_PATH="$PREFIX/share/pkgconfig" pkg-config --cflags ghoti.io-cutil-0)
libs=$(PKG_CONFIG_PATH="$PREFIX/share/pkgconfig" pkg-config --libs ghoti.io-cutil-0)
archive=$(ls "$root"/build/*/release/apps/*.a 2>/dev/null | head -1)
[ -n "$archive" ] || { echo "build the library first (make)" >&2; exit 1; }
generated=$(dirname "$(dirname "$archive")")/generated
runner=$suite/../jts-runner
# The same rpath the Makefile links with, so the runner finds cutil without
# the caller having to set LD_LIBRARY_PATH.
cc -O1 -o "$runner" "$root/tools/conformance/json_test_suite.c" \
	-I"$root/include" -I"$generated" $cflags "$archive" $libs -lm \
	-Wl,-rpath,"$PREFIX/lib/ghoti.io"

exec python3 "$root/tools/conformance/json_test_suite.py" "$suite" "$runner"
