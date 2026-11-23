# Droidpool

Droidpool is the Android-side PebbleKit bridge. It runs a foreground service,
receives the classic PebbleKit SEND/ACK/NACK broadcasts, and exposes a single
TLS server on `127.0.0.1:51841` for Rockpool. The host opens with a canonical
UUID and the displayed pairing token. The certificate is generated in
AndroidKeyStore and the pairing code is the unpadded base64url encoding of the
first 16 bytes of SHA-256 over the certificate DER followed by a private random
16-byte token.

The service uses legacy PebbleKit broadcast actions and emits RECEIVE,
RECEIVE_ACK and RECEIVE_NACK using the standard extras. It currently sends
legacy broadcasts without a package restriction because arbitrary UUID apps
can be registered by third-party companions. Rockpool routes dictionaries for the active watch app on the paired watch after
checking its legacy companion permission. Open the watch app manually. Support
currently covers classic AppMessage broadcasts, including acknowledgements.
The connection information provider and Android-initiated watch app launch are
outside this implementation.

## Local build

Set `ANDROID_SDK_ROOT` to an SDK containing API 35 and build-tools 35.0.0,
then run `./build-apk.sh`. The debug APK is emitted under `build/` and uses a
generated local keystore. For a stable release build, set
`DROIDPOOL_RELEASE=1`, `DROIDPOOL_KEYSTORE`, `DROIDPOOL_ALIAS` and
`DROIDPOOL_STOREPASS`. No signing key or binary dependency is stored here.

The script stages the verified APK at `rpm/droidpool/Droidpool.apk` for the
Rockpool RPM build. GitHub Actions builds Droidpool from the same checkout as
the RPM. Branch builds use a development signature. Tag builds require the
repository secrets `DROIDPOOL_KEYSTORE_BASE64` (base64 of the keystore file),
`DROIDPOOL_ALIAS` and `DROIDPOOL_STOREPASS`. Set `DROIDPOOL_KEYPASS` when the
key has its own password. Keep a backup of the original keystore for updates.
