#!/usr/bin/env bash
# Build, then flash the Nucleo over the ST-LINK (SWD), verify, and reset.
# Usage: ./flash.sh [Debug|Release]   (default: Debug)
set -euo pipefail
cd "$(dirname "$0")/.."

preset="${1:-Debug}"
elf="nucleo/build/$preset/nucleo.elf"

if ! command -v STM32_Programmer_CLI >/dev/null; then
    echo "STM32_Programmer_CLI not on PATH (find it with: mdfind -name STM32_Programmer_CLI)" >&2
    exit 1
fi

./scripts/build_nucleo.sh "$preset"
STM32_Programmer_CLI -c port=SWD -w "$elf" -v -rst
