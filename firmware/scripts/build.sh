#!/usr/bin/env bash
# Configure and build the host firmware targets (library + tests).
set -euo pipefail
cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja
cmake --build build
