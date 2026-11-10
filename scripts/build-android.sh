#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
: "${JAVA_HOME:?Set JAVA_HOME to a JDK 21 installation}"
: "${ANDROID_HOME:?Set ANDROID_HOME to the Android SDK (platform 37)}"
python3 "$root/scripts/prepare-android.py"
python3 - "$root" <<'PY'
from pathlib import Path
import os, secrets, subprocess, sys
private = Path.home() / '.config/sufficit/phone-android-signing'
private.mkdir(mode=0o700, parents=True, exist_ok=True)
password, key = private / 'password', private / 'pilot.jks'
if not password.exists():
    password.write_text(secrets.token_urlsafe(40))
    password.chmod(0o600)
if not key.exists():
    env = dict(os.environ, SUFFICIT_PHONE_STORE_PASSWORD=password.read_text())
    subprocess.run([str(Path(os.environ['JAVA_HOME']) / 'bin/keytool'), '-genkeypair',
        '-keystore', str(key), '-storepass:env', 'SUFFICIT_PHONE_STORE_PASSWORD',
        '-keypass:env', 'SUFFICIT_PHONE_STORE_PASSWORD', '-alias', 'sufficit-phone-pilot',
        '-keyalg', 'RSA', '-keysize', '3072', '-validity', '10000',
        '-dname', 'CN=Sufficit Phone Pilot, O=Sufficit, C=BR'], env=env, check=True)
    key.chmod(0o600)
p = Path(sys.argv[1]) / 'build-android-app/keystore.properties'
p.write_text('storeFile=' + str(key) + '\nstorePassword=' + password.read_text()
             + '\nkeyAlias=sufficit-phone-pilot\nkeyPassword=' + password.read_text() + '\n')
p.chmod(0o600)
PY
cd "$root/build-android-app"
./gradlew assembleRelease bundleRelease --no-daemon --max-workers=4
version=$(sed -n "s/^VERSION_NAME = '\(.*\)'$/\1/p" "$root/scripts/prepare-android.py")
mkdir -p "$root/dist-android"
apk="$root/dist-android/sufficit-phone-android-$version-release.apk"
aab="$root/dist-android/sufficit-phone-android-$version-release.aab"
cp "app/build/outputs/apk/release/sufficit-phone-android-$version-release.apk" "$apk"
cp app/build/outputs/bundle/release/app-release.aab "$aab"
"$ANDROID_HOME/build-tools/37.0.0/apksigner" verify --verbose --print-certs "$apk"
"$JAVA_HOME/bin/keytool" -printcert -jarfile "$aab" | grep -i sha256
cd "$root/dist-android"
sha256sum ./*.apk ./*.aab > SHA256SUMS
