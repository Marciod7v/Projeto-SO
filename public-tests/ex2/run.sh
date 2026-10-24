#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
source "$repo_root/public-tests/common.sh"
core_count=$(get_test_core_count)
output_file=$(mktemp)
actual_file=$(mktemp)
marker_file=$(mktemp)
input_error_file=$(mktemp)
config_dir=$(mktemp -d)
rm -f "$marker_file"
rm -f "$input_error_file"
trap 'rm -f "$output_file" "$actual_file" "$marker_file" "$input_error_file"; rm -f "$config_dir"/*; rmdir "$config_dir"' EXIT
export CLOUDIST_SIMPLE_CWD_MARKER="$marker_file"
export CLOUDIST_SIMPLE_INPUT_ERROR="$input_error_file"

if ! make -C "$repo_root"; then
  echo "Exercise 2: FAILED: could not build cloudIST."
  exit 1
fi
cd "$repo_root"

cp "$script_dir/01_relative_path.conf" "$config_dir/"
run_succeeded=1
if ! ./cloudIST 1 4 8 4 "$core_count" "$config_dir" 1 2>&1 | tee "$output_file"; then
  run_succeeded=0
fi

{ grep -E 'VM successfully defined!|Reservation made successfully!|Finished reservation execution!|VM ID : .*State: RUNNING|Hosted VMs \(0\):|\[ RESERVATIONS \] \(0 total\)|Invalid config file|Invalid Command|Failed to|Invalid .*command|does not exist' "$output_file" || true; } |
  sed -E 's/^.*(VM successfully defined!|Reservation made successfully!|Finished reservation execution!|State: RUNNING|Hosted VMs \(0\):|\[ RESERVATIONS \] \(0 total\)|Invalid config file|Invalid Command|Failed to|Invalid .*command|does not exist).*$/\1/' >"$actual_file"

if [[ "$run_succeeded" -ne 1 ]]; then
  echo "Exercise 2: FAILED: cloudIST exited with an error while running the VM." >&2
  exit 1
fi

failed=0
worker_unconfirmed=0
if [[ ! -f "$marker_file" ]]; then
  worker_unconfirmed=1
  if [[ -s "$input_error_file" ]]; then
    worker_cwd=$(<"$input_error_file")
    echo "Exercise 2: the worker started, but could not find the nested input file relative to its working directory ($worker_cwd). Check the copied input and relative path." >&2
  else
    echo "Exercise 2: the worker did not confirm the copied input file. It may not have started, or the input copy/path may be wrong." >&2
  fi
  failed=1
fi

running_count=$(awk '
  /DATA CENTER STATUS/ {
    listing_count++
    if (listing_count > 1) exit
  }
  listing_count == 1 && /VM ID : .*State: RUNNING/ { count++ }
  END { print count + 0 }
' "$output_file")
if [[ "$running_count" -ne 1 && "$worker_unconfirmed" -eq 0 ]]; then
  echo "Exercise 2: expected one VM in RUNNING state at the first listing, but found $running_count. No VM was active when that listing ran." >&2
  failed=1
fi

last_hosted_vms=$(grep -F 'Hosted VMs (0):' "$output_file" | tail -n 1 || true)
last_reservations=$(grep -F '[ RESERVATIONS ] (0 total)' "$output_file" | tail -n 1 || true)
if [[ -z "$last_hosted_vms" || -z "$last_reservations" ]]; then
  echo "Exercise 2: the final listing should show zero hosted VMs and zero reservations, but one or both are still present." >&2
  failed=1
fi

if ! diff -u "$script_dir/expected.txt" "$actual_file" >/dev/null && (( failed == 0 )); then
  echo "Exercise 2: the expected success messages or listing sequence did not appear. Review the simulator output above." >&2
  failed=1
fi

if (( failed != 0 )); then
  echo "Exercise 2: FAILED" >&2
  exit 1
fi

echo "Exercise 2: PASSED"
