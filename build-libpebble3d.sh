#!/bin/sh
# Build and stage the non-Sailfish part of Rockpool for the normal mb2 build.
set -eu

program=${0##*/}
project_dir=$(CDPATH= cd "$(dirname "$0")" && pwd -P)
release=false
reuse=false

usage()
{
    cat >&2 <<EOF
usage: $program [--release] [--reuse]

Run this script on the normal host. It builds the AArch64 libpebble3d Native
Image and stages the verified artifacts in rpm/native/. Afterwards, build the
complete package set normally inside the Sailfish Platform SDK:

  mb2 -t TARGET --no-vcs-apply build

--release snapshots Rockpool HEAD and its pinned mobileapp commit; local edits are excluded.
--reuse verifies and stages an existing libpebble3d/out/ build.
EOF
    exit 1
}

require_command()
{
    command -v "$1" >/dev/null 2>&1 || {
        printf '%s: required command is unavailable: %s\n' "$program" "$1" >&2
        exit 1
    }
}

require_java()
{
    if [ -n "${JAVA_HOME:-}" ]; then
        [ -x "$JAVA_HOME/bin/java" ] || {
            printf '%s: JAVA_HOME does not contain an executable bin/java: %s\n' \
                "$program" "$JAVA_HOME" >&2
            exit 1
        }
    else
        require_command java
    fi
}

while [ "$#" -gt 0 ]; do
    case $1 in
        --release)
            release=true
            ;;
        --reuse)
            reuse=true
            ;;
        *)
            usage
            ;;
    esac
    shift
done

for prerequisite in sed grep find sort sha256sum file mktemp; do
    require_command "$prerequisite"
done

if [ "$reuse" = false ]; then
    require_java
    for prerequisite in git awk docker; do
        require_command "$prerequisite"
    done
    if [ "$release" = true ]; then
        "$project_dir/libpebble3d/build.sh" --release
    else
        "$project_dir/libpebble3d/build.sh"
    fi
fi

if [ "$release" = true ]; then
    sh "$project_dir/rpm/stage-native-artifacts.sh" --release
else
    sh "$project_dir/rpm/stage-native-artifacts.sh"
fi

printf '%s\n' '== non-Sailfish build complete: rpm/native/'
printf '%s\n' '== next, inside the Sailfish SDK:'
printf '%s\n' 'mb2 -t TARGET --no-vcs-apply build'
