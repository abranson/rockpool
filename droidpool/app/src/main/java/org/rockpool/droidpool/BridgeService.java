package org.rockpool.droidpool;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.IBinder;
import android.util.Base64;
import android.util.Log;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.Closeable;
import java.io.EOFException;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.atomic.AtomicInteger;
import javax.net.ssl.KeyManagerFactory;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLServerSocket;
import javax.net.ssl.SSLSocket;

public final class BridgeService extends Service {
    static void start(Context context) {
        Intent service = new Intent(context, BridgeService.class);
        if (Build.VERSION.SDK_INT >= 26) context.startForegroundService(service);
        else context.startService(service);
    }

    private static final String PREFIX = "com.getpebble.action.app.";
    private static final String TX = "transaction_id";
    private static final String DATA = "msg_data";
    private static final int MAX_FRAME = 65536;
    private static final int CAPACITY = 32;
    private final AtomicInteger nextTransaction = new AtomicInteger();
    private volatile boolean stopping;
    private volatile SSLServerSocket listener;
    private volatile SSLSocket peer;
    private volatile Session active;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override public void onReceive(Context context, Intent intent) {
            Session session = active;
            if (session == null) return;
            try {
                session.fromCompanion(intent);
            } catch (RuntimeException | JSONException ignored) {
                // A malformed broadcast from another app must not stop the service.
            }
        }
    };

    @Override public void onCreate() {
        super.onCreate();
        Notification.Builder builder;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager manager = getSystemService(NotificationManager.class);
            manager.createNotificationChannel(new NotificationChannel("droidpool", "Droidpool",
                    NotificationManager.IMPORTANCE_LOW));
            builder = new Notification.Builder(this, "droidpool");
        } else {
            builder = new Notification.Builder(this);
        }
        startForeground(1, builder.setContentTitle("Droidpool")
                .setContentText("Listening for Rockpool")
                .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth).build());
        if (!DroidpoolState.enabled(this)) {
            stopSelf();
            return;
        }
        IntentFilter filter = new IntentFilter();
        filter.addAction(PREFIX + "SEND");
        filter.addAction(PREFIX + "ACK");
        filter.addAction(PREFIX + "NACK");
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(receiver, filter, RECEIVER_EXPORTED);
        else registerReceiver(receiver, filter);
        registered = true;
        new Thread(this::serve, "Droidpool listener").start();
    }

    private boolean registered;

    private void serve() {
        try {
            KeyStore store = KeyStore.getInstance("AndroidKeyStore");
            store.load(null);
            DroidpoolState.ensureKey(store);
            KeyManagerFactory managers = KeyManagerFactory.getInstance(KeyManagerFactory.getDefaultAlgorithm());
            managers.init(store, null);
            SSLContext tls = SSLContext.getInstance("TLS");
            tls.init(managers.getKeyManagers(), null, null);
            try (SSLServerSocket server = (SSLServerSocket) tls.getServerSocketFactory().createServerSocket()) {
                listener = server;
                server.setReuseAddress(true);
                server.bind(new InetSocketAddress("127.0.0.1", 51841));
                while (!stopping) {
                    try (SSLSocket socket = (SSLSocket) server.accept()) {
                        peer = socket;
                        if (stopping) break;
                        socket.setSoTimeout(5000);
                        BufferedReader input = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.UTF_8));
                        JSONObject open = read(input);
                        if (!"open".equals(open.getString("op"))) continue;
                        byte[] offered = Base64.decode(open.getString("token"), Base64.URL_SAFE | Base64.NO_WRAP);
                        if (!MessageDigest.isEqual(offered, DroidpoolState.token(this))) continue;
                        UUID uuid = UUID.fromString(open.getString("uuid"));
                        socket.setSoTimeout(0);
                        new Session(socket, uuid).run(input);
                    } catch (IOException | JSONException | IllegalArgumentException ignored) {
                        // A closed or invalid client does not retire the listening socket.
                    } finally {
                        active = null;
                        peer = null;
                    }
                }
            }
        } catch (Exception e) {
            if (!stopping) {
                Log.w("Droidpool", "Could not start listener", e);
                stopSelf();
            }
        }
    }

    private static JSONObject read(BufferedReader reader) throws IOException, JSONException {
        StringBuilder text = new StringBuilder();
        while (text.length() < MAX_FRAME) {
            int c = reader.read();
            if (c < 0) throw new EOFException();
            if (c == '\n') return new JSONObject(text.toString());
            text.append((char)c);
        }
        throw new IOException("Frame too large");
    }

    private final class Session {
        final SSLSocket socket;
        final UUID uuid;
        final Map<Integer,Integer> sent = new HashMap<>();
        final Map<Integer,Integer> received = new HashMap<>();
        final ArrayBlockingQueue<String> queue = new ArrayBlockingQueue<>(CAPACITY);
        Session(SSLSocket socket, UUID uuid) { this.socket = socket; this.uuid = uuid; }

        JSONObject frame(String op) throws JSONException {
            return new JSONObject().put("op", op).put("uuid", uuid.toString());
        }

        void run(BufferedReader input) throws IOException, JSONException {
            final OutputStream output = socket.getOutputStream();
            write(output, frame("ready").toString());
            active = this;
            Thread writer = new Thread(() -> {
                try {
                    while (!Thread.currentThread().isInterrupted()) write(output, queue.take());
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                } catch (IOException failed) {
                    close(socket);
                }
            }, "Droidpool writer");
            writer.start();
            try {
                while (!stopping) fromWatch(read(input));
            } finally {
                active = null;
                close(socket);
                writer.interrupt();
            }
        }

        void write(OutputStream output, String frame) throws IOException {
            output.write((frame + "\n").getBytes(StandardCharsets.UTF_8));
            output.flush();
        }

        synchronized void fromCompanion(Intent intent) throws JSONException {
            if (active != this) return;
            int tx = intent.getIntExtra(TX, -1);
            if ((PREFIX + "SEND").equals(intent.getAction())) {
                if (!uuid.equals(intent.getSerializableExtra("uuid"))) return;
                String raw = intent.getStringExtra(DATA);
                if (raw == null || raw.length() > MAX_FRAME) return;
                JSONArray data = new JSONArray(raw);
                int local = nextTransaction.incrementAndGet();
                if (sent.size() < CAPACITY && queue.offer(frame("data").put("tx", local).put("data", data).toString()))
                    sent.put(local, tx);
                else result(false, tx);
            } else {
                Integer watchTx = received.remove(tx);
                if (watchTx != null && !queue.offer(frame((PREFIX + "ACK").equals(intent.getAction()) ? "ack" : "nack")
                        .put("tx", watchTx).toString())) close(socket);
            }
        }

        synchronized void fromWatch(JSONObject message) throws JSONException, IOException {
            if (!uuid.toString().equals(message.getString("uuid"))) throw new IOException("Wrong watch app");
            String op = message.getString("op");
            int tx = message.getInt("tx");
            if ("data".equals(op)) {
                if (tx < 0 || tx > 255) throw new IOException("Invalid watch transaction");
                // Classic ACK intents carry only an eight-bit ID. Allocate one for this delivery.
                int local;
                do { local = nextTransaction.incrementAndGet() & 255; } while (received.containsKey(local) && received.size() < 256);
                if (received.size() >= CAPACITY) {
                    if (!queue.offer(frame("nack").put("tx",tx).toString())) throw new IOException("Queue full");
                    return;
                }
                received.put(local, tx);
                sendBroadcast(new Intent(PREFIX + "RECEIVE").putExtra("uuid", uuid).putExtra(TX, local)
                        .putExtra(DATA, message.getJSONArray("data").toString()));
                final int id = local;
                handler.postDelayed(() -> {
                    synchronized (Session.this) {
                        if (active == Session.this && received.get(id) != null && received.get(id) == tx) {
                            received.remove(id);
                            try {
                                if (!queue.offer(frame("nack").put("tx", tx).toString())) close(socket);
                            } catch (JSONException ignored) { close(socket); }
                        }
                    }
                }, 8000);
            } else if ("ack".equals(op) || "nack".equals(op)) {
                Integer original = sent.remove(tx);
                if (original != null) result("ack".equals(op), original);
            } else throw new IOException("Unknown operation");
        }

        void result(boolean ok, int tx) {
            sendBroadcast(new Intent(PREFIX + (ok ? "RECEIVE_ACK" : "RECEIVE_NACK"))
                    .putExtra("uuid", uuid).putExtra(TX, tx));
        }
    }

    private final android.os.Handler handler = new android.os.Handler(android.os.Looper.getMainLooper());

    private static void close(Closeable object) {
        try { if (object != null) object.close(); } catch (IOException ignored) { }
    }

    @Override public int onStartCommand(Intent intent, int flags, int id) { return START_STICKY; }

    @Override public void onDestroy() {
        stopping = true;
        if (registered) unregisterReceiver(receiver);
        close(listener);
        close(peer);
        handler.removeCallbacksAndMessages(null);
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
