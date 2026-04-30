#!/bin/sh
set -eu

stamp=$1
shift

for patch in "$@"; do
    if git apply --check --reverse "$patch" >/dev/null 2>&1; then
        continue
    fi
    git apply "$patch"
done

touch "$stamp"
