#!/usr/bin/env bash
# Configure and build the Nucleo firmware.
# Usage: ./build_nucleo.sh [Debug|Release]   (default: Debug)
set -euo pipefail
cd "$(dirname "$0")/../nucleo"

preset="${1:-Debug}"

cmake --preset "$preset"
cmake --build --preset "$preset"
