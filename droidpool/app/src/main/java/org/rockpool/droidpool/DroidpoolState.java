package org.rockpool.droidpool;

import android.content.Context;
import android.util.Base64;
import java.math.BigInteger;
import java.security.KeyPairGenerator;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.security.spec.ECGenParameterSpec;
import java.util.Date;
import javax.security.auth.x500.X500Principal;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;

final class DroidpoolState {
    private static final String PREFS = "droidpool";
    private static final String ALIAS = "droidpool-server";
    private DroidpoolState() { }
    static boolean enabled(Context c) { return c.getSharedPreferences(PREFS, 0).getBoolean("enabled", false); }
    static void setEnabled(Context c, boolean enabled) { c.getSharedPreferences(PREFS, 0).edit().putBoolean("enabled", enabled).apply(); }
    static synchronized byte[] token(Context c) {
        String value = c.getSharedPreferences(PREFS, 0).getString("token", null);
        if (value == null) { byte[] bytes = new byte[16]; new SecureRandom().nextBytes(bytes); value = Base64.encodeToString(bytes, Base64.NO_WRAP | Base64.URL_SAFE); c.getSharedPreferences(PREFS, 0).edit().putString("token", value).apply(); }
        return Base64.decode(value, Base64.NO_WRAP | Base64.URL_SAFE);
    }
    static String pairingCode(Context c) {
        try { KeyStore store = KeyStore.getInstance("AndroidKeyStore"); store.load(null); ensureKey(store); byte[] digest = MessageDigest.getInstance("SHA-256").digest(store.getCertificate(ALIAS).getEncoded()); byte[] code = new byte[32]; System.arraycopy(digest, 0, code, 0, 16); System.arraycopy(token(c), 0, code, 16, 16); return Base64.encodeToString(code, Base64.NO_WRAP | Base64.URL_SAFE | Base64.NO_PADDING); } catch (Exception e) { return "TLS setup unavailable"; }
    }
    static synchronized void ensureKey(KeyStore store) throws Exception {
        if (store.containsAlias(ALIAS) &&
                "EC".equals(store.getCertificate(ALIAS).getPublicKey().getAlgorithm())) return;
        KeyPairGenerator generator = KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_EC, "AndroidKeyStore");
        generator.initialize(new KeyGenParameterSpec.Builder(ALIAS, KeyProperties.PURPOSE_SIGN)
                .setAlgorithmParameterSpec(new ECGenParameterSpec("secp256r1"))
                // Conscrypt signs the TLS handshake digest with NONEwithECDSA.
                .setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256)
                .setCertificateSubject(new X500Principal("CN=Droidpool"))
                .setCertificateSerialNumber(BigInteger.ONE)
                .setCertificateNotBefore(new Date())
                .setCertificateNotAfter(new Date(System.currentTimeMillis() + 315360000000L))
                .build());
        generator.generateKeyPair();
    }
}
