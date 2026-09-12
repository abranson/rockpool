#!/bin/sh
# Cache only the standalone native SCM_RIGHTS probe, never the daemon or its tracing metadata.
set -eu

work=${FD_PROBE_WORK:-/work}
libs=${FD_PROBE_LIBS:-/dist/libs}
cache=${FD_PROBE_CACHE:-}
source=$work/tests/NativeFileDescriptorProbe.java
scratch=$(mktemp -d)
publish=
cleanup() {
    rm -rf "$scratch"
    if [ -n "$publish" ]; then rm -rf "$publish"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

# Record the exact dependency set, including names, additions and removals.
find "$libs" -maxdepth 1 -type f \( -name 'dbus-java-*.jar' -o \
    -name 'junixsocket-*.jar' -o -name 'slf4j-*.jar' \) | LC_ALL=C sort > "$scratch/jars"
[ -s "$scratch/jars" ] || { echo 'error: no descriptor probe dependencies' >&2; exit 1; }
sha256sum "$0" "$source" > "$scratch/checksums"
while IFS= read -r jar; do
    sha256sum "$jar" >> "$scratch/checksums"
done < "$scratch/jars"
cp "$scratch/checksums" "$scratch/inputs"
printf '%s\n' "${FD_PROBE_BUILDER_ID:-}" "$(uname -m)" >> "$scratch/inputs"
key=$(sha256sum "$scratch/inputs" | cut -d ' ' -f 1)
entry=
if [ -n "$cache" ]; then
    # The immutable Docker image ID covers GraalVM, javac, native libraries and toolchain.
    case ${FD_PROBE_BUILDER_ID:-} in
        sha256:*) ;;
        *) echo 'error: descriptor cache requires a builder image ID' >&2; exit 1 ;;
    esac
    mkdir -p "$cache"
    entry=$cache/$key
    if [ -x "$entry/native-fd-probe" ] &&
        (cd "$entry" && sha256sum -c SHA256SUMS >/dev/null 2>&1); then
        echo '== descriptor probe cache hit; running native round trip'
        "$entry/native-fd-probe"
        exit 0
    fi
fi

echo '== descriptor probe cache miss; compiling and running native round trip'
classes=$scratch/classes
agent=$scratch/agent
classpath=$(paste -sd : "$scratch/jars")
mkdir -p "$classes" "$agent"
javac -cp "$classpath" -d "$classes" "$source"
java -agentlib:native-image-agent=config-output-dir="$agent" \
    -cp "$classes:$classpath" NativeFileDescriptorProbe
if [ ! -s "$agent/reachability-metadata.json" ]; then
    echo 'error: descriptor probe produced no Native Image metadata' >&2
    exit 1
fi
# Worker count changes resource use, not the probe's source/ABI contract, so it is not a cache key.
# The options themselves are covered by the hash of this script.
native-image \
    -H:ConfigurationFileDirectories="$agent" \
    -H:NumberOfThreads="${NI_THREADS:-4}" \
    -J-XX:MaxRAMPercentage=75 \
    -cp "$classes:$classpath" \
    -o "$scratch/native-fd-probe" \
    --no-fallback \
    -H:+ReportExceptionStackTraces \
    NativeFileDescriptorProbe
"$scratch/native-fd-probe"

if [ -n "$entry" ]; then
    sha256sum -c "$scratch/checksums" >/dev/null
    # Publish only after a successful run. A partial build can never become a cache hit.
    publish=$(mktemp -d "$cache/.publish.XXXXXX")
    cp "$scratch/native-fd-probe" "$publish/native-fd-probe"
    (cd "$publish" && sha256sum native-fd-probe > SHA256SUMS)
    # Another build may publish the same key concurrently; its complete entry is sufficient.
    if [ -e "$entry" ]; then
        if ! (cd "$entry" && sha256sum -c SHA256SUMS >/dev/null 2>&1); then
            rm -rf "$entry"
        fi
    fi
    if mv -T "$publish" "$entry" 2>/dev/null; then
        publish=
    elif [ ! -x "$entry/native-fd-probe" ] ||
        ! (cd "$entry" && sha256sum -c SHA256SUMS >/dev/null 2>&1); then
        echo 'error: could not publish descriptor probe cache' >&2
        exit 1
    fi
fi
