#!/usr/bin/env bash
# Regenerates libmii/tests/vectors from synthetic Miis, using MiiJS as the
# reference encoder.
#
#   scripts/gen-test-vectors.sh <build-dir-with-test_mii> <MiiJS checkout>
#
# MiiJS: https://github.com/Stewared/MiiJS (run `npm install` in it first;
# only its format code is used).
set -euo pipefail

build=${1:?build dir}
miijs=${2:?MiiJS checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/tests/vectors"

mkdir -p "$out"
rm -f "$out"/random_*
"$build/test_mii" --gen-vectors "$out"
node "$here/scripts/miijs-convert.mjs" "$miijs" "$out"
