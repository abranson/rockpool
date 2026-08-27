#!/bin/sh
# Runs inside the builder container (mounts: /dist jars, /work this dir, /out).
set -e

CP=$(find /dist/libs -name '*.jar' | sort | tr '\n' ':')
PLATFORM_LIBDIR=${LP3_PLATFORM_LIBDIR:-/usr/lib64}
PLATFORM_DIRECTORY="${PLATFORM_LIBDIR}/libpebble3d/platforms"

# Exercise the two native bridges with the same compiler, headers and target architecture used
# for the packaged loader. These are fast white-box tests and catch ABI-layout, queue and
# cancellation regressions before the much more expensive Native Image build begins.
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" -I/work/include \
  -DLP3_PLATFORM_DIRECTORY=\""$PLATFORM_DIRECTORY"\" \
  -o /tmp/platform-loader-event-test \
  /work/tests/platform_loader_event_test.c -ldl -pthread
/tmp/platform-loader-event-test
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" -I/work/include \
  -o /tmp/rfcomm-socket-test /work/tests/rfcomm_socket_test.c -pthread
/tmp/rfcomm-socket-test

# The provider loader stays outside the Native Image.  It is deliberately a
# small C/JNI bridge so the image can validate and dlopen a package-owned ABI
# provider without giving JVM code arbitrary shared-library loading powers.
${CC:-cc} -std=c11 -O2 -fPIC -shared -Wall -Wextra -Werror \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/linux" -I/work/include \
  -DLP3_PLATFORM_DIRECTORY=\""$PLATFORM_DIRECTORY"\" \
  -o /out/libpebble3d-platform-loader.so \
  /work/native/platform_loader.c /work/native/rfcomm_socket.c -ldl -pthread
# The tracing JVM uses the same fixed installation path as the final daemon.
# It sees /out but RPM installation has not happened yet.
install -D -m 0755 /out/libpebble3d-platform-loader.so \
  /usr/libexec/libpebble3d/libpebble3d-platform-loader.so

# Trace reachability metadata on the JVM against live system + session buses;
# hardware-only paths are covered by the hand-written configs in /work.
dbus-uuidgen > /etc/machine-id
mkdir -p /run/dbus
dbus-daemon --system --fork
(/usr/libexec/bluetooth/bluetoothd --nodetach 2>/dev/null \
    || /usr/sbin/bluetoothd --nodetach) &
# Session bus: exercises the io.rebble.libpebble3 export plus the isolated compatibility
# adapter and notification monitor paths.
export DBUS_SESSION_BUS_ADDRESS=$(dbus-daemon --session --fork --print-address)
sleep 1

AGENT_DIR=/tmp/agent-config
mkdir -p "$AGENT_DIR"
echo "tracing (45s)..."
# preferIPv4Stack: docker containers have no IPv6 route; the JVM tries IPv6 first
# and the HTTPS trace request would hang instead of exercising TLS.
set +e
timeout 45 env LIBPEBBLE3D_AUTOCONNECT=1 LIBPEBBLE3D_TRACE_HTTP=1 \
    java -Djava.net.preferIPv4Stack=true \
    -agentlib:native-image-agent=config-output-dir="$AGENT_DIR" \
    -cp "$CP" io.rebble.libpebblecommon.Daemon > /tmp/trace-run.log 2>&1
trace_status=$?
set -e
if [ "$trace_status" -ne 124 ]; then
    echo "error: tracing daemon exited early with status $trace_status; trace log tail:" >&2
    tail -20 /tmp/trace-run.log >&2
    exit 1
fi
if [ ! -s "$AGENT_DIR/reachability-metadata.json" ]; then
    echo "error: tracing produced no metadata; trace log tail:" >&2
    tail -20 /tmp/trace-run.log >&2
    exit 1
fi

# Prove that the exact dbus-java + junixsocket stack can negotiate and transfer
# SCM_RIGHTS descriptors in an AArch64 Native Image.  A JVM pass alone is not
# enough: the native executable also needs the junixsocket JNI and reflection
# metadata to survive closed-world analysis.
FD_PROBE_CLASSES=/tmp/native-fd-probe-classes
FD_PROBE_AGENT=/tmp/native-fd-probe-agent
FD_PROBE_CP=$(find /dist/libs -maxdepth 1 -type f \( \
  -name 'dbus-java-*.jar' -o \
  -name 'junixsocket-*.jar' -o \
  -name 'slf4j-*.jar' \
  \) | sort | tr '\n' ':')
mkdir -p "$FD_PROBE_CLASSES" "$FD_PROBE_AGENT"
javac -cp "$FD_PROBE_CP" -d "$FD_PROBE_CLASSES" \
  /work/tests/NativeFileDescriptorProbe.java
java -agentlib:native-image-agent=config-output-dir="$FD_PROBE_AGENT" \
  -cp "$FD_PROBE_CLASSES:$FD_PROBE_CP" NativeFileDescriptorProbe
if [ ! -s "$FD_PROBE_AGENT/reachability-metadata.json" ]; then
    echo "error: descriptor probe produced no Native Image metadata" >&2
    exit 1
fi
native-image \
  -H:ConfigurationFileDirectories="$FD_PROBE_AGENT" \
  -H:NumberOfThreads="${NI_THREADS:-4}" \
  -J-XX:MaxRAMPercentage=75 \
  -cp "$FD_PROBE_CLASSES:$FD_PROBE_CP" \
  -o /tmp/native-fd-probe \
  --no-fallback \
  -H:+ReportExceptionStackTraces \
  NativeFileDescriptorProbe
/tmp/native-fd-probe

# Docker Desktop caps this VM near 7.75GB and peak RSS sits right at it. The default
# all-cores parallelism holds one method graph per thread and oversubscribes the CPU, so the
# builder GC-thrashes and trivial methods blow past native-image's 300s per-method wall-clock
# limit. Capping worker threads cuts peak memory and contention, trading build time for reliability.
native-image \
  -H:ConfigurationFileDirectories="$AGENT_DIR" \
  -H:ReflectionConfigurationFiles=/work/reflect-config.json \
  -H:ResourceConfigurationFiles=/work/resource-config.json \
  -H:JNIConfigurationFiles=/work/jni-config.json \
  -H:DynamicProxyConfigurationFiles=/work/proxy-config.json \
  -H:NumberOfThreads="${NI_THREADS:-4}" \
  -J-XX:MaxRAMPercentage=75 \
  -cp "$CP" \
  -o /out/libpebble3d \
  --no-fallback \
  --enable-url-protocols=http,https \
  -H:+ReportExceptionStackTraces \
  "$@" \
  io.rebble.libpebblecommon.Daemon

file /out/libpebble3d
