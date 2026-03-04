#!/usr/bin/env bash
set -euo pipefail

file="src/ui.js"

rg -q "const PAD_NOTE_FALLBACK_MIN = 68" "$file"
rg -q "const PAD_NOTE_FALLBACK_MAX = 83" "$file"
rg -q "function noteToPad" "$file"
rg -q "noteToPad\(note\)" "$file"
rg -q "const paramPad = parseInt\(getParamRaw\('ui_current_pad'\)" "$file"
