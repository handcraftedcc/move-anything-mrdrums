#!/usr/bin/env bash
set -euo pipefail

file="src/ui.js"

rg -q "function resolveFileBrowserStartDir" "$file"
rg -q "ui_last_sample_dir" "$file"
rg -q "host_open_file_browser\(key, '\\.wav', resolveFileBrowserStartDir\(key\)\)" "$file"
rg -F -q "if (currentPath) return currentPath;" "$file"
rg -F -q "const lastPath = getParamRaw('ui_last_sample_dir');" "$file"
rg -F -q "if (lastPath) return lastPath;" "$file"
if rg -q "seedEmptyFilepathFromLastPath" "$file"; then
  echo "FAIL: UI still seeds empty pad sample_path from previous selection" >&2
  exit 1
fi
