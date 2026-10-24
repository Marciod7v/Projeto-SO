#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
source "$repo_root/public-tests/common.sh"
core_count=$(get_test_core_count)
output_file=$(mktemp)
actual_file=$(mktemp)
config_dir=$(mktemp -d)
trap 'rm -f "$output_file" "$actual_file"; rm -f "$config_dir"/*; rmdir "$config_dir"' EXIT

if ! make -C "$repo_root"; then
  echo "Exercise 1: FAILED: could not build cloudIST."
  exit 1
fi
cd "$repo_root"

cp "$script_dir"/*.conf "$config_dir/"
cp "$script_dir/ignored.txt" "$config_dir/"

run_succeeded=1
if ! ./cloudIST 1 8 8 8 "$core_count" "$config_dir" 1 2>&1 | tee "$output_file"; then
  run_succeeded=0
fi

{ grep -E 'VM successfully defined!|Reservation made successfully!|Type ID:|Invalid config file|Invalid Command|Failed to reserve VMs|VM of type .* does not exist' "$output_file" || true; } |
  sed -E 's/.*Type ID: *([^ ]+).*/Type ID: \1/' >"$actual_file"
if [[ "$run_succeeded" -eq 1 ]] && diff -u "$script_dir/expected.txt" "$actual_file" >/dev/null; then
  echo "Exercise 1: PASSED"
else
  if [[ "$run_succeeded" -ne 1 ]]; then
    echo "Exercise 1: FAILED: cloudIST exited with an error while processing the config files." >&2
  else
    echo "Exercise 1: FAILED: the VM definitions or reservations did not match the expected result." >&2
  fi
  exit 1
fi
