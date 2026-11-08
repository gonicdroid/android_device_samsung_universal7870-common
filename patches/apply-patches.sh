#!/bin/bash
# Script to apply required system patches for Samsung universal7870-common

PATCHES_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${PATCHES_DIR}/../../../../" && pwd)"

apply_patch() {
    local target_dir="$1"
    local patch_file="$2"
    local patch_name="$(basename "${patch_file}")"

    if [ ! -d "${ROOT_DIR}/${target_dir}" ]; then
        echo "universal7870: Target directory ${target_dir} not found, skipping ${patch_name}"
        return
    fi

    # Check if patch is already applied
    if git -C "${ROOT_DIR}/${target_dir}" apply --reverse --check "${patch_file}" >/dev/null 2>&1; then
        echo "universal7870: Patch ${patch_name} already applied to ${target_dir}."
    elif git -C "${ROOT_DIR}/${target_dir}" apply --check "${patch_file}" >/dev/null 2>&1; then
        echo "universal7870: Applying patch ${patch_name} to ${target_dir}..."
        git -C "${ROOT_DIR}/${target_dir}" apply "${patch_file}"
        if [ $? -eq 0 ]; then
            echo "universal7870: Successfully applied ${patch_name} to ${target_dir}."
        else
            echo "universal7870: Failed to apply ${patch_name} to ${target_dir}!"
        fi
    else
        echo "universal7870: Warning: patch ${patch_name} does not apply cleanly to ${target_dir} (might be already applied with modifications)."
    fi
}

echo "=== Checking universal7870 device tree patches ==="
apply_patch "system/bt" "${PATCHES_DIR}/0002-system-bt-dynamic-sco-wbs-nbs.patch"
apply_patch "system/keymaster" "${PATCHES_DIR}/0003-system-keymaster-fix-auth-token-size-check.patch"
echo "=================================================="

