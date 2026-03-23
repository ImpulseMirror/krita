#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JSON_FILE="$SCRIPT_DIR/Technical_Specification_Completed.json"

if [[ ! -f "$JSON_FILE" ]]; then
    echo "Error: $JSON_FILE not found." >&2
    exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
    echo "Error: jq is required but not installed or not in PATH." >&2
    exit 1
fi

tmp_file="$(mktemp)"
trap 'rm -f "$tmp_file"' EXIT

jq '
  # Format a 0-100 number to percentage string rounded to hundredths (2 decimal places)
  def pct_hundredths:
    ((. * 100 | round) / 100) as $rounded
    | ($rounded * 100 | round) as $cents
    | ($cents / 100 | floor) as $int
    | ($cents % 100) as $dec
    | "\($int).\(if $dec < 10 then "0" else "" end)\($dec)%";

  # Entries that correspond to numbered spec sections (exclude metadata keys)
  def section_entries:
    to_entries
    | map(select(.key != "possible_values"
                 and .key != "completion_percentage"
                 and .key != "in_progress_percentage"));

  section_entries as $sections
  | ($sections | length) as $total
  | ($sections | map(select(.value == "complete")) | length) as $complete
  | ($sections | map(select(.value == "in_progress")) | length) as $in_progress
  | ($sections | map(select(.value == "not_applicable")) | length) as $na
  | ($complete + $in_progress) as $progress_count
  # Percentages over sections that can be ported (exclude not_applicable) so 100% = all applicable complete
  | ($total - $na) as $applicable
  | .completion_percentage =
      (if $applicable <= 0 then
         "0.00%"
       else
         (100 * $complete / $applicable | pct_hundredths)
       end)
  | .in_progress_percentage =
      (if $applicable <= 0 then
         "0.00%"
       else
         (100 * $progress_count / $applicable | pct_hundredths)
       end)
' "$JSON_FILE" > "$tmp_file"

mv "$tmp_file" "$JSON_FILE"

echo "Updated completion_percentage and in_progress_percentage in $JSON_FILE."

