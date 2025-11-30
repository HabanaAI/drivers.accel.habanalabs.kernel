#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0
# Compare in-tree habanalabs sources against a local drm-tip checkout.
# Fails on known upstream API regressions (patterns present in drm-tip shared
# files but missing here). Gaudi3-only paths are out of scope for drm-tip.
set -euo pipefail

OUR="${1:-$(cd "$(dirname "$0")/.." && pwd)}"
UP="${DRM_TIP_HABANALABS:-}"

if [[ -z "$UP" ]]; then
	for candidate in \
		"$HOME/upstream/wt-trunk/drm-tip/drivers/accel/habanalabs" \
		"$HOME/upstream/drm-tip.git/drivers/accel/habanalabs"; do
		if [[ -d "$candidate/common" ]]; then
			UP="$candidate"
			break
		fi
	done
fi

if [[ -z "$UP" || ! -d "$UP/common" ]]; then
	echo "error: set DRM_TIP_HABANALABS to drm-tip drivers/accel/habanalabs" >&2
	exit 1
fi

echo "Our tree:  $OUR"
echo "drm-tip:   $UP"
echo

mapfile -t shared < <(
	find "$OUR" -type f \( -name '*.c' -o -name '*.h' \) -printf '%P\n' | sort |
	while read -r f; do
		[[ -f "$UP/$f" ]] && echo "$f"
	done
)

echo "Shared source files: ${#shared[@]}"
echo

fail=0

check_marker() {
	local pat="$1" desc="$2"
	local regressed=()
	for f in "${shared[@]}"; do
		grep -q "$pat" "$UP/$f" 2>/dev/null || continue
		grep -q "$pat" "$OUR/$f" 2>/dev/null || regressed+=("$f")
	done
	if ((${#regressed[@]})); then
		echo "FAIL: $desc ($pat)"
		printf '  - %s\n' "${regressed[@]}"
		fail=1
	else
		echo "OK:   $desc"
	fi
}

# Inverse of check_marker: flag out-of-tree-only constructs that drm-tip dropped.
check_absent() {
	local pat="$1" desc="$2"
	local leftover=()
	for f in "${shared[@]}"; do
		if ! grep -q "$pat" "$UP/$f" 2>/dev/null &&
		   grep -q "$pat" "$OUR/$f" 2>/dev/null; then
			leftover+=("$f")
		fi
	done
	if ((${#leftover[@]})); then
		echo "FAIL: $desc ($pat)"
		printf '  - %s\n' "${leftover[@]}"
		fail=1
	else
		echo "OK:   $desc"
	fi
}

check_marker '\bsecs_to_jiffies\b' 'jiffies timeouts use secs_to_jiffies'
check_marker '\bpin_user_pages_fast\b' 'user memory pin uses pin_user_pages_fast'
check_marker '\bunpin_user_pages_dirty_lock\b' 'user memory unpin uses unpin_user_pages_dirty_lock'
check_marker 'depends on X86 && X86_64' 'Kconfig uses X86 && X86_64'
check_marker 'trace/events/habanalabs\.h' 'tracepoints header included where drm-tip uses it'
check_marker 'get_signal_cb_size' 'command submission uses get_signal_cb_size'
check_marker 'CLASS(get_unused_fd' 'dma-buf export uses CLASS(get_unused_fd) UAF fix'
check_marker 'const struct bin_attribute' 'sysfs uses const bin_attribute'

check_absent '\.fop_flags\s*=' 'no out-of-tree fop_flags initialiser'
check_absent '\bframe_vector\b' 'no legacy frame_vector fallback'
check_absent '_HAS_[A-Z0-9_]\+' 'no out-of-tree compat feature macros'

echo
if ((fail)); then
	echo "Parity check failed."
	exit 1
fi
echo "Parity markers passed (shared files vs drm-tip)."
