#!/usr/bin/env bash

set -uo pipefail

expected_output=$1
shift

if command_output=$("$@" 2>&1); then
  printf 'command unexpectedly succeeded\n%s\n' "$command_output" >&2
  exit 1
fi

printf '%s\n' "$command_output"
if [[ "$command_output" != *"$expected_output"* ]]; then
  printf 'expected output not found: %s\n' "$expected_output" >&2
  exit 1
fi
