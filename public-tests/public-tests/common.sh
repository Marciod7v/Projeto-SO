#!/usr/bin/env bash

get_test_core_count() {
  local available_cores percent selected_cores

  if command -v nproc >/dev/null 2>&1; then
    available_cores=$(nproc)
  else
    available_cores=$(getconf _NPROCESSORS_ONLN)
  fi

  if [[ ! "$available_cores" =~ ^[1-9][0-9]*$ ]]; then
    echo "Could not determine the number of available CPU cores." >&2
    return 1
  fi

  percent="${CLOUDIST_TEST_CORE_PERCENT:-50}"
  if [[ ! "$percent" =~ ^[0-9]+$ ]] || (( percent < 1 || percent > 100 )); then
    echo "CLOUDIST_TEST_CORE_PERCENT must be an integer from 1 to 100." >&2
    return 1
  fi

  selected_cores=$((available_cores * percent / 100))
  if (( selected_cores < 1 )); then
    selected_cores=1
  fi

  printf '%s\n' "$selected_cores"
}
