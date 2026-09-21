#!/bin/sh
# Score this library against yaml-test-suite, and optionally against a
# reference implementation for comparison.
#
#   tools/conformance/run.sh              # this library
#   tools/conformance/run.sh js           # js-yaml, if it can be found
#   tools/conformance/run.sh py           # PyYAML, if it is installed
#
# The suite is cloned into build/yaml-test-suite on first use and reused after
# that.  YTS_MIN sets a floor the score must meet; YTS_REPORT names a file to
# write the failing cases to.
set -e

root=$(cd "$(dirname "$0")/../.." && pwd)
suite=${YTS_SUITE:-$root/build/yaml-test-suite}
which=${1:-ours}

if [ ! -d "$suite/src" ]; then
	echo "fetching yaml-test-suite into $suite"
	git clone --depth 1 https://github.com/yaml/yaml-test-suite.git "$suite"
fi

case "$which" in
ours)
	[ -n "$PREFIX" ] || { echo "PREFIX must be set, as for any build here" >&2; exit 1; }
	# chron as well as cutil: yaml_dom.h has included <ghoti.io/chron/chron.h>
	# since !!timestamp stopped being parsed by hand, and this script asked
	# only for cutil - so `make conformance` stopped compiling at that commit
	# and nobody noticed, because it is not one of the targets `make test`
	# runs. Both lookup directories, the way run-json-schema.sh does it.
	pc="$PREFIX/share/pkgconfig:$PREFIX/lib/pkgconfig"
	cflags=$(PKG_CONFIG_PATH="$pc" pkg-config --cflags ghoti.io-cutil-0 ghoti.io-chron-0)
	libs=$(PKG_CONFIG_PATH="$pc" pkg-config --libs ghoti.io-cutil-0 ghoti.io-chron-0)
	archive=$(ls "$root"/build/*/release/apps/*.a 2>/dev/null | head -1)
	generated=$(dirname "$(dirname "$archive")")/generated
	[ -n "$archive" ] || { echo "build the library first (make)" >&2; exit 1; }
	runner=$suite/../yts-runner
	# The same rpath the Makefile links with, so the runner finds cutil and
	# chron without the caller having to set LD_LIBRARY_PATH.
	cc -O1 -o "$runner" "$root/tools/conformance/yaml_test_suite.c" \
		-I"$root/include" -I"$generated" $cflags "$archive" $libs -lm \
		-Wl,-rpath,"$PREFIX/lib/ghoti.io"
	set -- "$runner"
	;;
js)
	js=$(find / -maxdepth 8 -type d -name js-yaml 2>/dev/null | head -1)
	[ -n "$js" ] || { echo "js-yaml not found" >&2; exit 1; }
	cat > "$suite/../ref-js.js" <<JS
const yaml = require('$js');
let src=''; process.stdin.on('data',d=>src+=d);
process.stdin.on('end',()=>{ try{ const o=[];
  yaml.loadAll(src, d=>o.push(d===undefined?null:d));
  for(const d of o) console.log(JSON.stringify(d===undefined?null:d));
} catch(e){ console.log('FAIL parse: '+e.name); } });
JS
	set -- node "$suite/../ref-js.js"
	;;
py)
	cat > "$suite/../ref-py.py" <<PY
import sys, yaml, json
src = sys.stdin.buffer.read().decode('utf-8','replace')
try:
    for d in yaml.safe_load_all(src):
        print(json.dumps(d, ensure_ascii=False, default=str))
except Exception as e:
    print('FAIL parse: %s' % type(e).__name__)
PY
	set -- python3 "$suite/../ref-py.py"
	;;
*)
	echo "usage: $0 [ours|js|py]" >&2; exit 1 ;;
esac

exec python3 "$root/tools/conformance/yaml_test_suite.py" "$suite" "$@"
