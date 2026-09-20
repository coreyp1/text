#!/bin/sh
# Score this library against csv-spectrum.
#
#   tools/conformance/run-csv.sh
#
# The suite is cloned into build/csv-spectrum on first use and reused after
# that.  CSS_MIN sets a floor the score must meet.  Mirrors run.sh and
# run-json.sh, which do the same for YAML and JSON.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
suite=${CSS_SUITE:-$root/build/csv-spectrum}

if [ ! -d "$suite/csvs" ]; then
	echo "fetching csv-spectrum into $suite"
	git clone --depth 1 https://github.com/maxogden/csv-spectrum.git "$suite"
fi

[ -n "$PREFIX" ] || { echo "PREFIX must be set, as for any build here" >&2; exit 1; }
cflags=$(PKG_CONFIG_PATH="$PREFIX/share/pkgconfig" pkg-config --cflags ghoti.io-cutil-0)
libs=$(PKG_CONFIG_PATH="$PREFIX/share/pkgconfig" pkg-config --libs ghoti.io-cutil-0)
archive=$(ls "$root"/build/*/release/apps/*.a 2>/dev/null | head -1)
[ -n "$archive" ] || { echo "build the library first (make)" >&2; exit 1; }
generated=$(dirname "$(dirname "$archive")")/generated
runner=$suite/../css-runner
cc -O1 -o "$runner" "$root/tools/conformance/csv_spectrum.c" \
	-I"$root/include" -I"$generated" $cflags "$archive" $libs -lm \
	-Wl,-rpath,"$PREFIX/lib/ghoti.io"

exec python3 "$root/tools/conformance/csv_spectrum.py" "$suite" "$runner"
