/*
 * SPDX-License-Identifier: Apache-2.0
 */

import java.io.FileInputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;
import org.freedesktop.dbus.FileDescriptor;
import org.freedesktop.dbus.annotations.DBusInterfaceName;
import org.freedesktop.dbus.connections.impl.DBusConnection;
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder;
import org.freedesktop.dbus.interfaces.DBusInterface;
import org.freedesktop.dbus.transport.junixsocket.JUnixSocketSocketProvider;
import org.newsclub.net.unix.FileDescriptorCast;

/** A small JVM/Native Image round trip for the exact Unix-FD transport used by the daemon. */
public final class NativeFileDescriptorProbe {
    private static final String BUS_NAME = "io.rebble.libpebble3.tests.NativeFileDescriptorProbe";
    private static final String OBJECT_PATH = "/io/rebble/libpebble3/tests/NativeFileDescriptorProbe";
    private static final byte[] EXPECTED = "rockpool-native-fd".getBytes();

    @DBusInterfaceName("io.rebble.libpebble3.tests.NativeFileDescriptorProbe1")
    public interface Probe extends DBusInterface {
        byte[] Read(FileDescriptor descriptor);
    }

    private static final class ProbeObject implements Probe {
        private volatile int receivedDescriptor = -1;

        @Override
        public String getObjectPath() {
            return OBJECT_PATH;
        }

        @Override
        public byte[] Read(FileDescriptor descriptor) {
            receivedDescriptor = descriptor.getIntFileDescriptor();
            JUnixSocketSocketProvider provider = new JUnixSocketSocketProvider();
            try {
                java.io.FileDescriptor received = descriptor.toJavaFileDescriptor(provider);
                java.io.FileDescriptor duplicate;
                try {
                    duplicate = FileDescriptorCast.duplicating(received).getFileDescriptor();
                } finally {
                    new FileInputStream(received).close();
                }
                try (FileInputStream input = new FileInputStream(duplicate)) {
                    return input.readNBytes(4 * 1024);
                }
            } catch (Exception e) {
                throw new IllegalStateException("could not read received descriptor", e);
            }
        }
    }

    public static void main(String[] args) throws Exception {
        String address = System.getenv("DBUS_SESSION_BUS_ADDRESS");
        if (address == null || address.isBlank()) {
            throw new IllegalStateException("DBUS_SESSION_BUS_ADDRESS is unavailable");
        }
        Path payload = Files.createTempFile("rockpool-native-fd-", ".bin");
        DBusConnection service = null;
        DBusConnection client = null;
        try {
            Files.write(payload, EXPECTED);
            service = DBusConnectionBuilder.forAddress(address).withShared(false).build();
            client = DBusConnectionBuilder.forAddress(address).withShared(false).build();
            if (!service.isFileDescriptorSupported() || !client.isFileDescriptorSupported()) {
                throw new IllegalStateException("Unix-FD negotiation failed");
            }
            service.requestBusName(BUS_NAME);
            ProbeObject object = new ProbeObject();
            service.exportObject(OBJECT_PATH, object);
            Probe remote = client.getRemoteObject(BUS_NAME, OBJECT_PATH, Probe.class, false);
            JUnixSocketSocketProvider provider = new JUnixSocketSocketProvider();
            try (FileInputStream input = new FileInputStream(payload.toFile())) {
                FileDescriptor outgoing = FileDescriptor.fromJavaFileDescriptor(input.getFD(), provider);
                byte[] actual = remote.Read(outgoing);
                if (!Arrays.equals(EXPECTED, actual)) {
                    throw new IllegalStateException("descriptor content did not survive D-Bus");
                }
                if (outgoing.getIntFileDescriptor() == object.receivedDescriptor) {
                    throw new IllegalStateException("descriptor was not transferred with SCM_RIGHTS");
                }
            }
        } finally {
            if (client != null) {
                client.disconnect();
            }
            if (service != null) {
                service.disconnect();
            }
            Files.deleteIfExists(payload);
        }
    }

    private NativeFileDescriptorProbe() {
    }
}
