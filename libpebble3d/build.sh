#!/bin/sh
# Build libpebble3d as a GraalVM native image. Output: ./out/libpebble3d
# MOBILEAPP=<checkout> builds from a dev checkout instead of the pinned submodule.
# --release requires committed inputs and produces package-reusable provenance.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
MOBILEAPP="${MOBILEAPP:-$HERE/mobileapp}"
OUT="$HERE/out"
OUT_TEMP=
PREVIOUS_OUT=
SOURCE_TEMP=
BUILDER_IID_FILE=
BUILD_HERE=$HERE
BUILD_MOBILEAPP=$MOBILEAPP
release_build=false

case ${1-} in
    '')
        ;;
    --release)
        release_build=true
        ;;
    *)
        echo "usage: ${0##*/} [--release]" >&2
        exit 1
        ;;
esac
if [ "$#" -gt 1 ]; then
    echo "usage: ${0##*/} [--release]" >&2
    exit 1
fi

cleanup()
{
    if [ -n "$OUT_TEMP" ]; then
        rm -rf "$OUT_TEMP"
    fi
    if [ -n "$PREVIOUS_OUT" ]; then
        if [ ! -e "$OUT" ]; then
            mv "$PREVIOUS_OUT" "$OUT" || true
        else
            rm -rf "$PREVIOUS_OUT"
        fi
    fi
    if [ -n "$SOURCE_TEMP" ]; then
        rm -rf "$SOURCE_TEMP"
    fi
    if [ -n "$BUILDER_IID_FILE" ]; then
        rm -f "$BUILDER_IID_FILE"
    fi
}

trap cleanup 0 HUP INT TERM

if [ ! -d "$MOBILEAPP/libpebble3" ]; then
    echo "error: mobileapp checkout not found at $MOBILEAPP" >&2
    echo "hint: git submodule update --init libpebble3d/mobileapp (or set MOBILEAPP=...)" >&2
    exit 1
fi

if [ "$release_build" = true ] && [ "$MOBILEAPP" != "$HERE/mobileapp" ]; then
    echo "error: --release requires the committed mobileapp submodule" >&2
    exit 1
fi

root_commit=$(git -C "$HERE/.." rev-parse HEAD) || {
    echo "error: cannot determine Rockpool source revision" >&2
    exit 1
}
if [ "$release_build" = true ]; then
    mobileapp_commit=$(git -C "$HERE/.." rev-parse "$root_commit:libpebble3d/mobileapp") || {
        echo "error: cannot determine pinned mobileapp source revision" >&2
        exit 1
    }
else
    mobileapp_commit=$(git -C "$MOBILEAPP" rev-parse HEAD) || {
        echo "error: cannot determine mobileapp source revision" >&2
        exit 1
    }
fi
mobileapp_version_code=$(git -C "$MOBILEAPP" rev-list --count "$mobileapp_commit") || {
    echo "error: cannot determine mobileapp source version code" >&2
    exit 1
}
case $mobileapp_version_code in
    ''|*[!0-9]*|0)
        echo "error: invalid mobileapp source version code" >&2
        exit 1
        ;;
esac
mobileapp_git_hash=
if [ "$release_build" = true ]; then
    mobileapp_git_hash=$(git -C "$MOBILEAPP" describe --always "$mobileapp_commit") || {
        echo "error: cannot determine mobileapp source identity" >&2
        exit 1
    }
    if [ -z "$mobileapp_git_hash" ]; then
        echo "error: empty mobileapp source identity" >&2
        exit 1
    fi
fi

if [ "$release_build" = true ]; then
    # Never feed the hour-long native build from mutable worktree paths or shared Gradle output.
    # Both source trees and the resulting jvmDist live in one unique commit-derived snapshot.
    SOURCE_TEMP=$(mktemp -d "${TMPDIR:-/tmp}/libpebble3d-release-source.XXXXXX")
    git -C "$HERE/.." archive "$root_commit" | tar -x -C "$SOURCE_TEMP"
    git -C "$MOBILEAPP" archive \
        --prefix=libpebble3d/mobileapp/ "$mobileapp_commit" | tar -x -C "$SOURCE_TEMP"
    BUILD_HERE=$SOURCE_TEMP/libpebble3d
    BUILD_MOBILEAPP=$BUILD_HERE/mobileapp
    # Validate the captured sources, not the checkout that may change during compilation.
    sh "$BUILD_HERE/tests/check-contract-artifacts.sh"
fi

# AGP needs an SDK location to configure the project even though only the jvm
# target gets built; the submodule has no local.properties.
if [ -z "$ANDROID_HOME" ] && [ ! -f "$MOBILEAPP/local.properties" ]; then
    for sdk in "$HOME/Library/Android/sdk" "$HOME/Android/Sdk"; do
        if [ -d "$sdk" ]; then
            export ANDROID_HOME="$sdk"
            break
        fi
    done
    if [ -z "$ANDROID_HOME" ]; then
        echo "error: no Android SDK found; set ANDROID_HOME or create $MOBILEAPP/local.properties" >&2
        exit 1
    fi
fi

echo "== gradle jvmDist (daemon composite build; libpebble3 from $BUILD_MOBILEAPP)"
(cd "$BUILD_HERE/daemon" && \
    MOBILEAPP="$BUILD_MOBILEAPP" \
    LIBPEBBLE3_ARCHIVE_GIT_HASH="$mobileapp_git_hash" \
    LIBPEBBLE3_ARCHIVE_VERSION_CODE="$mobileapp_version_code" \
    ./gradlew jvmDist)

echo "== builder image"
BUILDER_IID_FILE=$(mktemp "${TMPDIR:-/tmp}/libpebble3d-builder-image.XXXXXX")
docker build --platform linux/arm64 --iidfile "$BUILDER_IID_FILE" \
    -t libpebble3d-builder "$BUILD_HERE"
builder_image_id=$(cat "$BUILDER_IID_FILE") || {
    echo "error: cannot determine Native Image builder identity" >&2
    exit 1
}
if ! printf '%s\n' "$builder_image_id" | grep -E -q '^sha256:[0-9a-f]{64}$'; then
    echo "error: invalid Native Image builder identity" >&2
    exit 1
fi

echo "== trace + native-image"
OUT_TEMP=$(mktemp -d "$HERE/.out.XXXXXX")
docker run --rm --platform linux/arm64 \
    -e NI_THREADS="${NI_THREADS:-4}" \
    -e FD_PROBE_BUILDER_ID="$builder_image_id" \
    -e FD_PROBE_CACHE=/probe-cache \
    -v rockpool-native-fd-probe-cache:/probe-cache \
    -v "$BUILD_HERE/daemon/build/jvmDist":/dist:ro \
    -v "$BUILD_HERE":/work:ro \
    -v "$OUT_TEMP":/out \
    "$builder_image_id" sh /work/build-native.sh

if [ ! -x "$OUT_TEMP/libpebble3d" ] || \
    [ ! -r "$OUT_TEMP/libpebble3d-platform-loader.so" ]; then
    echo "error: native build did not produce a complete output" >&2
    exit 1
fi

if [ "$release_build" = true ]; then
    # Provenance describes the captured commits even if the checkout has moved on.
    source_mode=committed
else
    source_mode=development
fi

artifact_names=$(find "$OUT_TEMP" -mindepth 1 -maxdepth 1 -type f \
    ! -name .build-provenance -printf '%f\n' | LC_ALL=C sort)
if [ -z "$artifact_names" ]; then
    echo "error: native build produced no package artifacts" >&2
    exit 1
fi
if find "$OUT_TEMP" -mindepth 1 -maxdepth 1 ! -type f -print | grep -q .; then
    echo "error: native build output contains a non-regular entry" >&2
    exit 1
fi
{
    platform_abi_major=$(sed -n \
        's/^#define LP3_PLATFORM_ABI_MAJOR \([0-9][0-9]*\)u$/\1/p' \
        "$BUILD_HERE/include/libpebble3d-platform.h")
    platform_abi_minor=$(sed -n \
        's/^#define LP3_PLATFORM_ABI_MINOR \([0-9][0-9]*\)u$/\1/p' \
        "$BUILD_HERE/include/libpebble3d-platform.h")
    launcher_abi=$(sed -n \
        's/^#define LP3_LAUNCHER_VERSION UINT16_C(\([0-9][0-9]*\))$/\1/p' \
        "$BUILD_HERE/include/libpebble3d-launcher-wire.h")
    wire_major=$(sed -n \
        's/^static const uint16_t kMajor = \([0-9][0-9]*\);$/\1/p' \
        "$BUILD_HERE/../platform-sailfish/common/wire.h")
    wire_minor=$(sed -n \
        's/^static const uint16_t kMinor = \([0-9][0-9]*\);$/\1/p' \
        "$BUILD_HERE/../platform-sailfish/common/wire.h")
    for value in "$platform_abi_major" "$platform_abi_minor" "$launcher_abi" \
        "$wire_major" "$wire_minor"; do
        case $value in
            ''|*[!0-9]*)
                echo "error: cannot determine packaged ABI versions" >&2
                exit 1
                ;;
        esac
    done

    printf 'format=3\n'
    printf 'source_mode=%s\n' "$source_mode"
    printf 'target_arch=aarch64\n'
    printf 'rockpool_commit=%s\n' "$root_commit"
    printf 'mobileapp_commit=%s\n' "$mobileapp_commit"
    printf 'builder_image_id=%s\n' "$builder_image_id"
    printf 'platform_abi=%s.%s\n' "$platform_abi_major" "$platform_abi_minor"
    printf 'launcher_abi=%s\n' "$launcher_abi"
    printf 'sailfish_wire=%s.%s\n' "$wire_major" "$wire_minor"
    printf '%s\n' "$artifact_names" | while IFS= read -r artifact; do
        case $artifact in
            ''|*[!0-9A-Za-z._+-]*)
                echo "error: unsafe native build artifact name: $artifact" >&2
                exit 1
                ;;
        esac
        artifact_sha256=$(sha256sum "$OUT_TEMP/$artifact" | awk '{ print $1 }')
        printf 'artifact_sha256=%s %s\n' "$artifact_sha256" "$artifact"
    done
} > "$OUT_TEMP/.build-provenance"

PREVIOUS_OUT="$HERE/.out.previous.$$"
if [ -e "$PREVIOUS_OUT" ]; then
    echo "error: refusing to overwrite stale output backup $PREVIOUS_OUT" >&2
    exit 1
fi
if [ -e "$OUT" ]; then
    mv "$OUT" "$PREVIOUS_OUT"
fi
if ! mv "$OUT_TEMP" "$OUT"; then
    if [ -e "$PREVIOUS_OUT" ]; then
        mv "$PREVIOUS_OUT" "$OUT"
        PREVIOUS_OUT=
    fi
    exit 1
fi
OUT_TEMP=
if [ -e "$PREVIOUS_OUT" ]; then
    rm -rf "$PREVIOUS_OUT"
fi
PREVIOUS_OUT=

echo "== done: $OUT/libpebble3d"
