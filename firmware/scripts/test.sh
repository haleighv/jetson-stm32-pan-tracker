#!/usr/bin/env bash
# Build, then run all host unit tests.
set -euo pipefail
cd "$(dirname "$0")/.."

./scripts/build.sh
ctest --test-dir build --output-on-failure
