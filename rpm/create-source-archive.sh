#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Create the Rockpool RPM source archive from committed Git trees only.

set -eu

program=${0##*/}

fail()
{
    printf '%s: %s\n' "$program" "$*" >&2
    exit 1
}

if [ "$#" -ne 1 ]; then
    fail "usage: $program VERSION"
fi
case $1 in
    ''|[!0-9A-Za-z]*|*[!0-9A-Za-z.+~_]*)
        fail "usage: $program VERSION"
        ;;
esac

version=$1
script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P) || \
    fail "cannot determine script directory"
project_dir=$(CDPATH= cd "$script_dir/.." && pwd -P) || \
    fail "cannot determine project directory"
checker=$project_dir/libpebble3d/tests/check-contract-artifacts.sh
mobileapp_dir=$project_dir/libpebble3d/mobileapp
spec_file=$project_dir/rpm/rockpool.spec
native_dir=$project_dir/rpm/native
native_verifier=$project_dir/rpm/verify-native-artifacts.sh
output_dir=$(pwd -P) || fail "cannot determine output directory"
archive_name=rockpool-$version.tar.xz
archive_path=$output_dir/$archive_name
archive_prefix=rockpool-$version

if [ ! -r "$checker" ]; then
    fail "missing readable release preflight: $checker"
fi
if [ ! -x "$native_verifier" ]; then
    fail "missing executable Native Image verifier: $native_verifier"
fi

spec_version=$(awk '/^Version:[[:space:]]*/ { print $2; exit }' "$spec_file") || \
    fail "cannot read package version from $spec_file"
if [ -z "$spec_version" ] || [ "$version" != "$spec_version" ]; then
    fail "archive version $version does not match package version ${spec_version:-unknown}"
fi

root_commit=$(git -C "$project_dir" rev-parse HEAD) || \
    fail "cannot determine Rockpool HEAD"
mobileapp_commit=$(git -C "$mobileapp_dir" rev-parse HEAD) || \
    fail "cannot determine mobileapp HEAD"
sh "$checker" --require-committed
sh "$native_verifier" "$native_dir" committed >/dev/null
grep -F -x -- "rockpool_commit=$root_commit" \
    "$native_dir/.build-provenance" >/dev/null || \
    fail "Native Image input does not match Rockpool HEAD"
grep -F -x -- "mobileapp_commit=$mobileapp_commit" \
    "$native_dir/.build-provenance" >/dev/null || \
    fail "Native Image input does not match mobileapp HEAD"
if [ "$(git -C "$project_dir" rev-parse HEAD)" != "$root_commit" ] || \
    [ "$(git -C "$mobileapp_dir" rev-parse HEAD)" != "$mobileapp_commit" ]; then
    fail "source revisions changed during release preflight"
fi
source_date_epoch=$(git -C "$project_dir" show -s --format=%ct "$root_commit") || \
    fail "cannot determine Rockpool source timestamp"
temporary_dir=$(mktemp -d "${TMPDIR:-/tmp}/rockpool-source.XXXXXX") || \
    fail "cannot create temporary directory"
temporary_archive=$(mktemp "$output_dir/.${archive_name}.XXXXXX") || {
    rm -rf "$temporary_dir"
    fail "cannot create temporary archive"
}

cleanup()
{
    rm -rf "$temporary_dir"
    rm -f "$temporary_archive"
}

trap cleanup 0 HUP INT TERM

git -C "$project_dir" archive --format=tar --prefix="$archive_prefix/" \
    "$root_commit" > "$temporary_dir/root.tar" || \
    fail "cannot archive Rockpool HEAD"
tar -C "$temporary_dir" -xf "$temporary_dir/root.tar" || \
    fail "cannot unpack Rockpool HEAD"
git -C "$mobileapp_dir" archive --format=tar \
    --prefix="$archive_prefix/libpebble3d/mobileapp/" "$mobileapp_commit" \
    > "$temporary_dir/mobileapp.tar" || fail "cannot archive mobileapp HEAD"
tar -C "$temporary_dir" -xf "$temporary_dir/mobileapp.tar" || \
    fail "cannot unpack mobileapp HEAD"
mkdir -p "$temporary_dir/$archive_prefix/rpm/native" || \
    fail "cannot create Native Image archive directory"
cp -a "$native_dir/." "$temporary_dir/$archive_prefix/rpm/native/" || \
    fail "cannot add verified Native Image input to source archive"
sh "$native_verifier" "$temporary_dir/$archive_prefix/rpm/native" committed >/dev/null
grep -F -x -- "rockpool_commit=$root_commit" \
    "$temporary_dir/$archive_prefix/rpm/native/.build-provenance" >/dev/null || \
    fail "archived Native Image input does not match Rockpool HEAD"
grep -F -x -- "mobileapp_commit=$mobileapp_commit" \
    "$temporary_dir/$archive_prefix/rpm/native/.build-provenance" >/dev/null || \
    fail "archived Native Image input does not match mobileapp HEAD"
tar -C "$temporary_dir" --sort=name --owner=0 --group=0 --numeric-owner \
    --mode='u+rwX,go+rX,go-w' --mtime="@$source_date_epoch" \
    -cJf "$temporary_archive" -- "$archive_prefix" || \
    fail "cannot compress source archive"
# Revalidate after constructing the archive so a concurrent checkout/worktree edit cannot make
# the preflight describe different source than the captured immutable commits.
sh "$checker" --require-committed
sh "$native_verifier" "$native_dir" committed >/dev/null
if [ "$(git -C "$project_dir" rev-parse HEAD)" != "$root_commit" ] || \
    [ "$(git -C "$mobileapp_dir" rev-parse HEAD)" != "$mobileapp_commit" ]; then
    fail "source revisions changed while creating the source archive"
fi
chmod 0644 "$temporary_archive" || fail "cannot set source archive permissions"
if ! ln "$temporary_archive" "$archive_path"; then
    fail "refusing to overwrite or cannot publish $archive_path"
fi
rm -f "$temporary_archive"

trap - 0 HUP INT TERM
rm -rf "$temporary_dir"
printf '%s\n' "$archive_path"
