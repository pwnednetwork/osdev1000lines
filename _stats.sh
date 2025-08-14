#!/usr/bin/env zsh
# rev: 1
# change summary: initial version — recursively totals size (KB) and LOC for .c/.h files in current dir.

set -euo pipefail

# Collect matching files (null-safe for weird filenames)
typeset -a files
while IFS= read -r -d '' f; do
  files+=("$f")
done < <(find . -type f \( -name '*.c' -o -name '*.h' \) -print0)

if (( ${#files} == 0 )); then
  echo "No .c or .h files found under $(pwd)"
  exit 0
fi

tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

# For each file, compute bytes + LOC, then emit: path<TAB>KB<TAB>LOC
for f in "${files[@]}"; do
  bytes=$(stat -c '%s' -- "$f")      # GNU coreutils (Linux). On macOS use: stat -f '%z'
  loc=$(wc -l < "$f")
  awk -v b="$bytes" -v loc="$loc" -v f="$f" 'BEGIN {
    printf "%s\t%.1f\t%d\n", f, b/1024.0, loc
  }' >> "$tmp"
done

# Pretty-print, sorted by KB (descending)
# Columns: File, KB (1 decimal), LOC
sort -k2,2nr "$tmp" | awk '
BEGIN {
  fmt = "%-60s %10s %10s\n"
  printf fmt, "File", "KB", "LOC"
  sep = sprintf("%-60s %10s %10s", "", "", "")
  gsub(/./, "-", sep)
  print sep
}
{
  total_kb += $2
  total_loc += $3
  printf "%-60s %10.1f %10d\n", $1, $2, $3
}
END {
  sep = sprintf("%-60s %10s %10s", "", "", "")
  gsub(/./, "-", sep)
  print sep
  printf "%-60s %10.1f %10d\n", "TOTAL", total_kb, total_loc
}'
