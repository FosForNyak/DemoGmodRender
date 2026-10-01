#!/usr/bin/env bash
# Configures, builds and tests one engine preset from a read-only checkout mounted at /src.
# Usage: run-preset <preset> [fuzz-seconds]   (fuzz-seconds: run each libFuzzer target that long)
set -euo pipefail
preset="${1:-linux-gcc}"
fuzz_seconds="${2:-0}"

mkdir -p /work
tar -C /src --exclude=./engine/build --exclude=./app --exclude=./.git -cf - . | tar -C /work -xf -
cd /work/engine
cmake --preset "$preset"
cmake --build --preset "$preset"
ctest --preset "$preset"

if [ "$fuzz_seconds" -gt 0 ]; then
  for f in build/"$preset"/fuzz/fuzz_*; do
    [ -x "$f" ] || continue
    name=$(basename "$f")
    echo "== $name for ${fuzz_seconds}s"
    "build/$preset/fuzz/seeds_${name#fuzz_}" --write-seeds="/tmp/corpus/$name"
    "$f" -max_total_time="$fuzz_seconds" -rss_limit_mb=2048 -timeout=10 "/tmp/corpus/$name"
  done
fi
