#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Compile a host test program unless the one that is there was built by this very command from these very files:
#   sh tests/hostcc.sh cc -O2 -Ibuild/gen -o build/host/x_test tests/x_test.c firmware/src/y.c -lm
# The command is the compiler's, whole; its output is the argument of -o. Beside the output a stamp (OUTPUT.stamp):
#   line 1   sha256 of: the compiler's --version, the working directory, every argument
#   the rest sha256 + path of the output itself and of every file the compile read: the sources and objects on the
#            command line and every header the preprocessor opened (cc -M: the system's too, and build/gen's)
# The output is kept only when line 1 is this command's and every listed file still has its hash: contents, not
# dates (a file put back with an old date, a header generated again with the same text). Anything else compiles:
# another flag, another compiler, a changed, moved or missing file, an output another command wrote, a stamp that
# cannot be read, a path with a space in it. HOSTCC_FORCE=1: always compile. HOSTCC_VERBOSE=1: say which it was.
# The exit status and the diagnostics are the compiler's. Only compiled programs are kept, never a test's result.
set -u
[ $# -ge 2 ] || { echo "usage: hostcc.sh compiler args... -o output ..." >&2; exit 2; }
out=
prev=
for a in "$@"; do
    [ "$prev" = -o ] && out=$a
    prev=$a
done
[ -n "$out" ] || { echo "hostcc.sh: no -o in: $*" >&2; exit 2; }
stamp=$out.stamp
if command -v sha256sum >/dev/null 2>&1; then SUM=sha256sum; else SUM="shasum -a 256"; fi
NL='
'
key=$( { "$1" --version 2>&1; pwd; for a in "$@"; do printf '%s\n' "$a"; done; } | $SUM | cut -c1-64)
say() { [ -z "${HOSTCC_VERBOSE:-}" ] || echo "hostcc: $out: $1" >&2; }

if [ -z "${HOSTCC_FORCE:-}" ] && [ -f "$out" ] && [ -f "$stamp" ] && [ "$(sed -n 1p "$stamp")" = "$key" ] &&
   [ "$(sed -n 2p "$stamp" | cut -c67-)" = "$out" ] && sed 1d "$stamp" | $SUM -c - >/dev/null 2>&1; then
    say "kept"
    exit 0
fi

# the dependencies first (the preprocessor alone, the same flags): a compile that fails here fails below too, with
# its diagnostics
rm -f "$stamp"
tmp=$out.tmp.$$
trap 'rm -f "$tmp" "$tmp.d" "$tmp.s"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
deps_ok=1
has_src=0
for a in "$@"; do
    case $a in -*) ;; *.c|*.cc|*.cpp|*.cxx|*.m|*.S|*.s) has_src=1 ;; esac
done
: > "$tmp.d"
[ $has_src -eq 0 ] || (                           # (a link of objects alone: the command line's files are all)
    cmd=$1; shift
    set -- "$cmd" -M "$@"
    n=$#; skip=0; i=0
    while [ $i -lt $n ]; do                       # the same arguments without "-o output" and without objects
        a=$1; shift; i=$((i + 1))
        if [ $skip -eq 1 ]; then skip=0; continue; fi
        case $a in
            -o) skip=1; continue ;;
            *.o|*.a) continue ;;
        esac
        set -- "$@" "$a"
    done
    exec "$@" -w                                  # (-w: "-lm unused" must not fail this pass under -Werror)
) > "$tmp.d" 2>/dev/null || deps_ok=0

"$@" || exit $?
[ -f "$out" ] || exit 0                           # (a command that makes no file: nothing to keep)
if [ $deps_ok -eq 1 ]; then
    {
        # cc -M: "target: dep dep \" lines; the files on the command line themselves (the objects are not in -M's list)
        tr -s ' \\\n\t' '\n\n\n\n' < "$tmp.d" | grep -v ':$' | grep -v '^$'
        for a in "$@"; do
            case $a in -*) ;; *) [ -f "$a" ] && [ "$a" != "$out" ] && printf '%s\n' "$a" ;; esac
        done
    } | sort -u > "$tmp.s"
    {
        echo "$key"
        { $SUM "$out" && tr '\n' '\0' < "$tmp.s" | xargs -0 $SUM; } ||
            echo "incomplete"                     # (a line sha256sum -c cannot check: compiled again next time)
    } > "$tmp" 2>/dev/null
    mv "$tmp" "$stamp"
fi
say "compiled"
exit 0
