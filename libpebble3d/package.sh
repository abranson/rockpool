#!/bin/sh
# Build and package libpebble3d into an aarch64 RPM. Output: ./RPMS/.
# --reuse-current-build is an explicit escape hatch after a verified build.sh run.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
VER="${VER:-2.0}"

case $VER in
    ''|[!0-9A-Za-z]*|*[!0-9A-Za-z.+~_]*)
        echo "error: VER must contain only RPM-version-safe characters" >&2
        exit 1
        ;;
esac

if [ "$#" -gt 1 ]; then
    echo "usage: ${0##*/} [--reuse-current-build]" >&2
    exit 1
fi
case ${1-} in
    '')
        package_mode=fresh
        ;;
    --reuse-current-build)
        package_mode=reuse
        ;;
    *)
        echo "usage: ${0##*/} [--reuse-current-build]" >&2
        exit 1
        ;;
esac

mkdir -p "$HERE/RPMS"
# Remove visible stale output before any release preflight/build step. A failed
# attempt must leave no old RPM that can be mistaken for the requested release.
rm -f "$HERE"/RPMS/libpebble3d-*.rpm

checker="$HERE/tests/check-contract-artifacts.sh"
if [ ! -r "$checker" ]; then
    echo "error: missing readable release preflight: $checker" >&2
    exit 1
fi
sh "$checker" --require-committed

require_current_build()
{
    build_dir=$1
    manifest=$build_dir/.build-provenance
    if [ ! -x "$build_dir/libpebble3d" ] || \
        [ ! -r "$build_dir/libpebble3d-platform-loader.so" ] || \
        [ ! -r "$manifest" ]; then
        echo "error: no complete current build to reuse; run ./build.sh first" >&2
        exit 1
    fi

    root_commit=$(git -C "$HERE/.." rev-parse HEAD) || exit 1
    mobileapp_commit=$(git -C "$HERE/mobileapp" rev-parse HEAD) || exit 1
    if ! grep -F -x -- "rockpool_commit=$root_commit" \
            "$manifest" >/dev/null || \
        ! grep -F -x -- "mobileapp_commit=$mobileapp_commit" \
            "$manifest" >/dev/null; then
        echo "error: out/ does not match the current source; run ./build.sh first" >&2
        exit 1
    fi

    if ! grep -F -x -- "format=2" "$manifest" >/dev/null || \
        ! grep -F -x -- "source_mode=committed" "$manifest" >/dev/null || \
        grep -E -v '^(format=2|source_mode=committed|rockpool_commit=[0-9a-f]+|mobileapp_commit=[0-9a-f]+|builder_image_id=sha256:[0-9a-f]{64}|artifact_sha256=[0-9a-f]{64} [0-9A-Za-z._+-]+)$' \
            "$manifest" | grep -q .; then
        echo "error: out/ was not produced by a committed release build" >&2
        exit 1
    fi

    builder_image_id=$(sed -n 's/^builder_image_id=//p' "$manifest")
    if [ -z "$builder_image_id" ] || \
        ! docker image inspect "$builder_image_id" >/dev/null 2>&1; then
        echo "error: completed-build Native Image builder is unavailable" >&2
        exit 1
    fi

    expected_names=$(sed -n 's/^artifact_sha256=[0-9a-f]* //p' "$manifest")
    actual_names=$(find "$build_dir" -mindepth 1 -maxdepth 1 -type f \
        ! -name .build-provenance -printf '%f\n' | LC_ALL=C sort)
    if [ -z "$expected_names" ] || [ "$actual_names" != "$expected_names" ] || \
        find "$build_dir" -mindepth 1 -maxdepth 1 ! -type f -print | grep -q .; then
        echo "error: out/ artifact inventory does not match its completed-build manifest" >&2
        exit 1
    fi
    sed -n 's/^artifact_sha256=//p' "$manifest" |
        while IFS=' ' read -r expected_sha artifact; do
            actual_sha=$(sha256sum "$build_dir/$artifact" | awk '{ print $1 }')
            if [ "$actual_sha" != "$expected_sha" ]; then
                echo "error: out/$artifact does not match its completed-build manifest" >&2
                exit 1
            fi
        done
}

case $package_mode in
    fresh)
        "$HERE/build.sh" --release
        ;;
    reuse)
        echo "== reusing explicitly accepted out/ build"
        ;;
esac

sh "$checker" --require-committed
require_current_build "$HERE/out"

package_input=
package_source=
package_output=
publication_temp=

cleanup()
{
    [ -z "$publication_temp" ] || rm -f "$publication_temp"
    [ -z "$package_input" ] || rm -rf "$package_input"
    [ -z "$package_source" ] || rm -rf "$package_source"
    [ -z "$package_output" ] || rm -rf "$package_output"
}

trap cleanup 0 HUP INT TERM

package_input=$(mktemp -d "$HERE/RPMS/.input.XXXXXX")
package_source=$(mktemp -d "$HERE/RPMS/.source.XXXXXX")
package_output=$(mktemp -d "$HERE/RPMS/.package.XXXXXX")

# Consume a user-owned snapshot and verify it again after copying. The Docker build can therefore
# neither package an artifact added after validation nor race a changing out/ file.
cp "$HERE/out/.build-provenance" "$package_input/.build-provenance"
sed -n 's/^artifact_sha256=[0-9a-f]* //p' "$HERE/out/.build-provenance" |
    while IFS= read -r artifact; do
        cp "$HERE/out/$artifact" "$package_input/$artifact"
    done
require_current_build "$package_input"

# Package metadata is read from the same immutable root commit recorded by build.sh, never from
# a live worktree that can change after the release preflight.
git -C "$HERE/.." archive "$root_commit" libpebble3d/rpm |
    tar -x -C "$package_source"
package_rpm="$package_source/libpebble3d/rpm"
if [ ! -r "$package_rpm/libpebble3d.spec" ]; then
    echo "error: committed daemon RPM metadata snapshot is incomplete" >&2
    exit 1
fi

docker run --rm --platform linux/arm64 \
    -e VER="$VER" \
    -v "$package_input":/out:ro \
    -v "$package_rpm":/rpm:ro \
    -v "$package_output":/RPMS \
    "$builder_image_id" sh -ec '
        mkdir -p /tmp/rpmbuild/SOURCES
        cp /rpm/libpebble3d.service /rpm/bluetooth-experimental.conf \
            /tmp/rpmbuild/SOURCES/
        rpmbuild -bb /rpm/libpebble3d.spec \
            --define "_topdir /tmp/rpmbuild" \
            --define "ver $VER" \
            --define "debug_package %{nil}" \
            --define "_binary_payload w6.gzdio" \
            --define "__os_install_post %{nil}" \
            --define "_build_id_links none" \
            --target aarch64
        cp /tmp/rpmbuild/RPMS/aarch64/*.rpm /RPMS/
    '

set -- "$package_output"/libpebble3d-"$VER"-*.aarch64.rpm
if [ "$1" = "$package_output/libpebble3d-$VER-*.aarch64.rpm" ] || \
    [ "$#" -ne 1 ] || [ ! -f "$1" ]; then
    echo "error: RPM build did not produce exactly one libpebble3d $VER aarch64 package" >&2
    exit 1
fi
published_rpm="$HERE/RPMS/${1##*/}"
publication_temp=$(mktemp "$HERE/RPMS/.rpm.XXXXXX")
cp "$1" "$publication_temp"
chmod 0644 "$publication_temp"
if ! ln "$publication_temp" "$published_rpm"; then
    echo "error: refusing to overwrite or cannot publish $published_rpm" >&2
    exit 1
fi
rm -f "$publication_temp"
publication_temp=
trap - 0 HUP INT TERM
cleanup

echo "== done:"
ls -lh "$HERE/RPMS"/libpebble3d-*.rpm
