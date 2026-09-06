#!/bin/sh
# Verify a sealed libpebble3d Native Image artifact directory.
set -eu

program=${0##*/}

fail()
{
    printf '%s: %s\n' "$program" "$*" >&2
    exit 1
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    fail "usage: $program DIRECTORY [any|committed]"
fi

artifact_dir=$1
required_mode=${2:-any}
case $required_mode in
    any|committed)
        ;;
    *)
        fail "mode must be any or committed"
        ;;
esac

manifest=$artifact_dir/.build-provenance
[ -d "$artifact_dir" ] || fail "artifact directory does not exist: $artifact_dir"
[ -r "$manifest" ] || fail "missing build provenance: $manifest"

require_line()
{
    grep -F -x -- "$1" "$manifest" >/dev/null || \
        fail "missing or incorrect provenance field: $1"
}

require_line 'format=3'
require_line 'target_arch=aarch64'
require_line 'platform_abi=1.8'
require_line 'launcher_abi=1'
require_line 'sailfish_wire=1.9'

for field in format source_mode target_arch rockpool_commit mobileapp_commit \
    builder_image_id platform_abi launcher_abi sailfish_wire; do
    field_count=$(grep -c "^$field=" "$manifest" || true)
    [ "$field_count" -eq 1 ] || fail "provenance must contain exactly one $field field"
done

source_mode=$(sed -n 's/^source_mode=//p' "$manifest")
case $source_mode in
    committed|development)
        ;;
    *)
        fail "invalid source mode in provenance"
        ;;
esac
if [ "$required_mode" = committed ] && [ "$source_mode" != committed ]; then
    fail "release packaging requires committed Native Image input"
fi

if grep -E -v '^(format=3|source_mode=(committed|development)|target_arch=aarch64|rockpool_commit=[0-9a-f]{40}|mobileapp_commit=[0-9a-f]{40}|builder_image_id=sha256:[0-9a-f]{64}|platform_abi=1\.8|launcher_abi=1|sailfish_wire=1\.9|artifact_sha256=[0-9a-f]{64} [0-9A-Za-z._+-]+)$' \
        "$manifest" | grep -q .; then
    fail "provenance contains an invalid field"
fi

expected_names=$(sed -n 's/^artifact_sha256=[0-9a-f]* //p' "$manifest")
actual_names=$(find "$artifact_dir" -mindepth 1 -maxdepth 1 -type f \
    ! -name .build-provenance -printf '%f\n' | LC_ALL=C sort)
[ -n "$expected_names" ] || fail "provenance contains no artifacts"
[ "$actual_names" = "$expected_names" ] || \
    fail "artifact inventory does not match provenance"
if find "$artifact_dir" -mindepth 1 -maxdepth 1 ! -type f -print | grep -q .; then
    fail "artifact directory contains a non-regular entry"
fi

printf '%s\n' "$expected_names" | grep -F -x libpebble3d >/dev/null || \
    fail "artifact inventory omits libpebble3d"
printf '%s\n' "$expected_names" | grep -F -x libpebble3d-platform-loader.so >/dev/null || \
    fail "artifact inventory omits libpebble3d-platform-loader.so"
printf '%s\n' "$expected_names" | while IFS= read -r artifact; do
    case $artifact in
        libpebble3d|*.so)
            ;;
        *)
            fail "artifact cannot be installed by the unified RPM spec: $artifact"
            ;;
    esac
done

sed -n 's/^artifact_sha256=//p' "$manifest" |
    while IFS=' ' read -r expected_sha artifact; do
        [ -n "$expected_sha" ] && [ -n "$artifact" ] || \
            fail "malformed artifact digest"
        actual_sha=$(sha256sum "$artifact_dir/$artifact" | awk '{ print $1 }')
        [ "$actual_sha" = "$expected_sha" ] || \
            fail "digest mismatch for $artifact"
    done

command -v file >/dev/null 2>&1 || fail "the file command is required"
file "$artifact_dir/libpebble3d" | grep -q 'ELF 64-bit.*ARM aarch64' || \
    fail "libpebble3d is not an AArch64 ELF executable"
file "$artifact_dir/libpebble3d-platform-loader.so" | \
    grep -q 'ELF 64-bit.*ARM aarch64' || \
    fail "platform loader is not an AArch64 ELF object"

printf '%s\n' "$source_mode"
