#!/bin/sh
# Copy a verified Native Image output into the ignored Sailfish packaging input.
set -eu

program=${0##*/}
script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
project_dir=$(CDPATH= cd "$script_dir/.." && pwd -P)
source_dir=$project_dir/libpebble3d/out
destination=$script_dir/native
required_mode=any

case ${1-} in
    '')
        ;;
    --release)
        required_mode=committed
        ;;
    *)
        printf 'usage: %s [--release]\n' "$program" >&2
        exit 1
        ;;
esac
if [ "$#" -gt 1 ]; then
    printf 'usage: %s [--release]\n' "$program" >&2
    exit 1
fi

sh "$script_dir/verify-native-artifacts.sh" "$source_dir" "$required_mode" >/dev/null

temporary=$(mktemp -d "$script_dir/.native.XXXXXX")
previous=

cleanup()
{
    [ -z "$temporary" ] || rm -rf "$temporary"
    if [ -n "$previous" ]; then
        if [ ! -e "$destination" ]; then
            mv "$previous" "$destination" || true
        else
            rm -rf "$previous"
        fi
    fi
}
trap cleanup 0 HUP INT TERM

cp "$source_dir/.build-provenance" "$temporary/.build-provenance"
sed -n 's/^artifact_sha256=[0-9a-f]* //p' "$source_dir/.build-provenance" |
    while IFS= read -r artifact; do
        cp "$source_dir/$artifact" "$temporary/$artifact"
    done
chmod 0644 "$temporary/.build-provenance"
chmod 0755 "$temporary/libpebble3d" "$temporary"/*.so
sh "$script_dir/verify-native-artifacts.sh" "$temporary" "$required_mode" >/dev/null

previous=$script_dir/.native.previous.$$
[ ! -e "$previous" ] || {
    printf '%s: stale staging backup exists: %s\n' "$program" "$previous" >&2
    exit 1
}
if [ -e "$destination" ]; then
    mv "$destination" "$previous"
fi
mv "$temporary" "$destination"
temporary=
if [ -n "$previous" ]; then
    rm -rf "$previous"
    previous=
fi
trap - 0 HUP INT TERM

printf '%s\n' "$destination"
