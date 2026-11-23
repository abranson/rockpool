#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:?set ANDROID_SDK_ROOT or ANDROID_HOME}}
BT="$SDK/build-tools/35.0.0"
PLATFORM="$SDK/platforms/android-35/android.jar"
OUT="$ROOT/build"
rm -rf "$OUT/compiled" "$OUT/generated" "$OUT/classes" "$OUT/dex" "$OUT/apk" "$OUT/unsigned.apk" "$OUT/aligned.apk"
mkdir -p "$OUT/compiled" "$OUT/generated" "$OUT/classes" "$OUT/dex" "$OUT/apk"
"$BT/aapt2" compile --dir "$ROOT/app/src/main/res" -o "$OUT/compiled/res.zip"
"$BT/aapt2" link -0 arsc -o "$OUT/base.apk" --manifest "$ROOT/app/src/main/AndroidManifest.xml" -I "$PLATFORM" --java "$OUT/generated" "$OUT/compiled/res.zip"
find "$ROOT/app/src/main/java" "$OUT/generated" -name '*.java' -print0 | xargs -0 javac -source 8 -target 8 -classpath "$PLATFORM" -d "$OUT/classes"
jar cf "$OUT/classes.jar" -C "$OUT/classes" .
"$BT/d8" --lib "$PLATFORM" --output "$OUT/dex" "$OUT/classes.jar"
unzip -q "$OUT/base.apk" -d "$OUT/apk"
cp "$OUT/dex/classes.dex" "$OUT/apk/"
(cd "$OUT/apk" && zip -q -0 -r "$OUT/unsigned.apk" .)
"$BT/zipalign" -p -f 4 "$OUT/unsigned.apk" "$OUT/aligned.apk"
if [ "${DROIDPOOL_RELEASE:-0}" = 1 ]; then
  : "${DROIDPOOL_KEYSTORE:?set DROIDPOOL_KEYSTORE for release signing}"
  : "${DROIDPOOL_ALIAS:?set DROIDPOOL_ALIAS for release signing}"
  : "${DROIDPOOL_STOREPASS:?set DROIDPOOL_STOREPASS for release signing}"
  export DROIDPOOL_KEYPASS="${DROIDPOOL_KEYPASS:-$DROIDPOOL_STOREPASS}"
  "$BT/apksigner" sign --key-pass env:DROIDPOOL_KEYPASS --ks "$DROIDPOOL_KEYSTORE" --ks-key-alias "$DROIDPOOL_ALIAS" --ks-pass "env:DROIDPOOL_STOREPASS" --out "$OUT/droidpool-release.apk" "$OUT/aligned.apk"
else
  KEY="$OUT/debug.keystore"
  [ -f "$KEY" ] || keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname 'CN=Android Debug,O=Android,C=US' >/dev/null 2>&1
  "$BT/apksigner" sign --ks "$KEY" --ks-pass pass:android --out "$OUT/droidpool-debug.apk" "$OUT/aligned.apk"
fi
if [ "${DROIDPOOL_RELEASE:-0}" = 1 ]; then APK="$OUT/droidpool-release.apk"; else APK="$OUT/droidpool-debug.apk"; fi
"$BT/zipalign" -c -P 4 -v 4 "$APK"
"$BT/apksigner" verify --verbose "$APK"
sha256sum "$APK"

# The Sailfish SDK consumes the verified APK without the Android toolchain.
install -D -m 0644 "$APK" "$ROOT/../rpm/droidpool/Droidpool.apk"
