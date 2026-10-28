#!/bin/sh
# Run from any directory; builds only temporary host binaries.
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src_dir="$test_dir/../src"
test_build=$(mktemp -d)
# Preserve binaries for debugging; no repository build artifacts.
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$src_dir" "$src_dir/scheduler_core.c" "$test_dir/test_scheduler_core.c" \
  -o "$test_build/core"
"$test_build/core"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$test_dir/host" -I "$src_dir" "$src_dir/ejection_settings.c" \
  "$test_dir/test_settings.c" -o "$test_build/settings"
"$test_build/settings"
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I "$test_dir/host" -I "$src_dir" "$src_dir/scheduler_core.c" \
  "$src_dir/ejection_scheduler.c" "$test_dir/test_scheduler_api.c" -o "$test_build/api"
"$test_build/api"
printf 'Host test binaries: %s\n' "$test_build"
