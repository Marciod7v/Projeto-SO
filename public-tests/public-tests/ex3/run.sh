#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
source "$repo_root/public-tests/common.sh"
core_count=$(get_test_core_count)
output_file=$(mktemp)
actual_file=$(mktemp)
config_dir=$(mktemp -d)
trap 'rm -f "$output_file" "$actual_file"; rm -f "$config_dir"/*.conf; rmdir "$config_dir"' EXIT

if ! make -C "$repo_root"; then
  echo "Exercise 3: FAILED: could not build cloudIST."
  exit 1
fi
cd "$repo_root"

cp "$script_dir"/*.conf "$config_dir/"
run_succeeded=1
if ! ./cloudIST 4 4 8 4 "$core_count" "$config_dir" 4 2>&1 | tee "$output_file"; then
  run_succeeded=0
fi

{ grep -E 'VM successfully defined!|Reservation made successfully!|DATA CENTER STATUS|Invalid config file|Invalid Command|Failed to|Invalid .*command|does not exist' "$output_file" || true; } |
  sed -E 's/^.*DATA CENTER STATUS.*$/DATA CENTER STATUS/' |
  LC_ALL=C sort >"$actual_file"
if [[ "$run_succeeded" -eq 1 ]] &&
   diff -u "$script_dir/expected.txt" "$actual_file" >/dev/null; then
  echo "Exercise 3: PASSED"
else
  if [[ "$run_succeeded" -ne 1 ]]; then
    echo "Exercise 3: FAILED: cloudIST exited with an error while processing the configs." >&2
  else
    echo "Exercise 3: FAILED: expected four reservations and four data-center listings; one or more configs may not have completed successfully." >&2
  fi
  exit 1
fi
