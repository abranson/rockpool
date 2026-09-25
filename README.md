# Rockpool

Pebble watch support for SailfishOS.

Rockpool combines a Sailfish Silica app with a background watch service and
native Sailfish integration. It handles pairing, apps and watchfaces,
notifications, and the phone services that make a Pebble useful day to day.

[Community discussion](http://talk.maemo.org/showthread.php?t=96490) ·
[OpenRepos listing](https://openrepos.net/content/abranson/rockpool)

## Using Rockpool

Install a `rockpool` RPM built for your device architecture and SailfishOS
release, using the device package manager so runtime dependencies are resolved.
The current source build produces AArch64 packages. The UI, watch daemon and
Sailfish provider ship together in one RPM.

Enable Bluetooth, open Rockpool, and use watch discovery to pair your Pebble.
Rockpool can also import existing Bluetooth bonds. Sign in to Rebble for
account-backed services. The background service maintains watch integration
independently of the app window.

Available workflows include:

- Watch discovery, pairing, connection management and screenshots.
- Apps and watchfaces, app configuration, and firmware and language installation.
- Notification forwarding, supported actions and message replies.
- Call and media controls, contacts, and favorite-contact Send Text actions.
- Calendar integration, timeline settings and saved-location weather forecasts.
- Health history and settings, Quiet Time, profile switching and developer tools.

Availability depends on the watch, transport and healthy platform services.
Health history and several settings are shared across the account; they do not
represent independent per-watch data. App installation, removal and ordering
currently require exactly one paired watch; independent ordering for multiple
watches remains pending.
Message replies are restricted to supported Sailfish Messages notifications;
an arbitrary application's notification does not automatically support replies.

Image previews require the notification producer to supply
`x-nemo-image-preview-path` or `x-nemo-image-preview-data`. App icons and large
icons are not treated as preview images. Images are bounded and forwarded only
where the watch supports them; text notifications remain available otherwise.

The [functional parity matrix](libpebble3d/README.md#functional-parity-matrix) records
implemented workflows and remaining gaps. The
[capability registry](libpebble3d/README.md#capability-registry) describes how clients
discover availability at runtime.

## How it fits together

Since version 2.0, the bundled `libpebble3d` service owns watch communication.
It embeds the Kotlin libpebble3 library and is compiled to a native executable
with GraalVM; the phone does not need a Java runtime.

The Silica UI talks to the daemon's private `org.rockpool` compatibility
interface. The daemon also exposes the generic `io.rebble.libpebble3` session-bus
API. Sailfish integration lives in Rockpool's platform provider and isolated
helper, including notifications, contacts, calendars, calls and location.

| Source | Purpose |
| --- | --- |
| [ui/](ui/) | Silica UI and its C++ D-Bus client |
| [libpebble3d/daemon/](libpebble3d/daemon/) | Daemon, public API and Rockpool compatibility layer |
| [libpebble3d/mobileapp/](libpebble3d/mobileapp/) | Pinned libpebble3 submodule |
| [platform-sailfish/](platform-sailfish/) | Sailfish provider, helper and launcher |
| [libpebble3d/api/](libpebble3d/api/) | Public D-Bus contract and capabilities |
| [libpebble3d/README.md#sailfish-platform-launcher-and-host-wire-protocols](libpebble3d/README.md#sailfish-platform-launcher-and-host-wire-protocols) | Provider/helper protocol |
| [rpm/rockpool.spec](rpm/rockpool.spec) | Unified package, dependencies and SDK checks |

See the [daemon documentation](libpebble3d/README.md) for native build internals
and platform ABI details. Sailfish C++ and QML must remain compatible with
Qt 5.6.

## Building

There are two build stages:

1. **Normal host:** build the AArch64 daemon and JNI loader using Gradle and
   the repository's Docker/GraalVM builder.
2. **Sailfish Platform SDK:** compile the UI and Sailfish provider, run their
   checks, and package everything in a single `rockpool` RPM.

Native Image cannot cross-compile. On an x86-64 host, the first stage therefore
requires working ARM64 container emulation. GraalVM does not belong inside the
Sailfish SDK target.

### Prepare the host

Initialize the pinned library checkout from the repository root:

```sh
git submodule update --init libpebble3d/mobileapp
```

Install these prerequisites:

- JDK 17 for the host Gradle build.
- An Android SDK with the platform selected by
  [the pinned version catalog](libpebble3d/mobileapp/gradle/libs.versions.toml)
  (currently API 37) and accepted SDK licenses. The shared project's Android
  Gradle plugin is configured even for JVM builds.
- Docker with permission to use its daemon and an AArch64 container runtime.
  On x86-64, configure QEMU/binfmt support before building.
- Git and standard build utilities, including `awk`, `sed`, `find`, `tar`,
  `xz`, `sha256sum` and `file`. Initial dependency downloads need network access.

Allow at least 8 GiB of memory for Docker, preferably 12 GiB, and about 15 GiB
of free disk space. Emulated Native Image builds can take well over an hour.
The build defaults to four compiler workers; `NI_THREADS` overrides that count.
More workers can increase memory use.

### Build and stage the daemon

Run on the normal host, from the repository root:

```sh
export JAVA_HOME=/path/to/jdk-17
export ANDROID_HOME=/path/to/Android/Sdk
./build-libpebble3d.sh
```

The script builds native output in `libpebble3d/out/`, verifies it, and stages
it under `rpm/native/` with a `.build-provenance` manifest. Both output
directories are generated build inputs, not packages to install on a phone.
The builder and native dependencies are defined in
[libpebble3d/Dockerfile](libpebble3d/Dockerfile).

### Build the Sailfish RPM

Use a current Sailfish Platform SDK with a dedicated, disposable AArch64 target
for the intended Sailfish release. Keep SDK base and shared targets pristine;
resolve project dependencies in the project target. The authoritative
`BuildRequires` and runtime dependencies are in
[rpm/rockpool.spec](rpm/rockpool.spec).

Open the same checkout inside the SDK and run:

```sh
mb2 -t TARGET --no-fix-version build
```

Replace `TARGET` with your project target's exact name. `--no-fix-version`
preserves the version in the spec rather than deriving one from older Git tags.
The resulting RPM is written below `RPMS/`. Install that RPM through the device
package manager before testing the packaged application.

### Which stage needs rebuilding?

| Changed code | Required build |
| --- | --- |
| Daemon Kotlin, libpebble3, JNI loader or native build inputs | Native stage, then SDK RPM |
| Sailfish UI or provider implementation, with unchanged ABI | SDK RPM using compatible staged native output |
| Provider ABI or wire contract | Rebuild both stages together |
| Documentation only | No runtime rebuild |

To restage a previously completed native build:

```sh
./build-libpebble3d.sh --reuse
```

`--reuse` verifies and copies existing output; it does **not** compile source
changes. Use it only when that native build is still appropriate for the
changes being packaged. Never use it to pick up new Kotlin or JNI code.

### Release builds

Commit any source changes that should be included first. The release build
snapshots Rockpool HEAD and its pinned mobileapp commit; uncommitted changes
are excluded. On the normal host:

```sh
./build-libpebble3d.sh --release
```

This validates and builds the captured source snapshot, recording source
commits, builder identity, ABI versions, the output inventory and SHA-256
digests. Later checkout edits or commits do not invalidate the native build.
Then run the same SDK `mb2` command above.
Development native output cannot substitute for release-verified artifacts.

To create a source archive containing verified release native inputs, use the
version from the spec; for the current version:

```sh
rpm/create-source-archive.sh 2.0.0
```

The archive step requires clean release sources, the submodule at its committed
revision, and staged native output whose recorded commits match the checkout.

### Updating translations

RPM builds compile the checked-in translation catalogues without updating them.
After changing UI text, refresh them from the repository root with Qt's `lupdate`:

```sh
lupdate ui/*.cpp ui/*.h ui/qml -recursive -extensions qml,js,cpp,h -ts ui/translations/*.ts
```

Review and commit the catalogue changes so Weblate can expose new strings to
translators once they are published.

## Validation and troubleshooting

From the repository root, check the public contract and build-artifact rules:

```sh
sh libpebble3d/tests/check-contract-artifacts.sh
```

Add `--require-committed` when validating committed release source. For daemon
JVM tests, with the same JDK and Android SDK environment as the build:

```sh
cd libpebble3d/daemon
./gradlew test
```

The SDK RPM build runs the provider's Qt tests in its `%check` section. Host
JVM tests do not replace an AArch64 Native Image build or device testing.
After installing an RPM, exercise pairing/reconnection, notification delivery,
and whichever platform workflows changed.

Run these commands as the logged-in Sailfish user to inspect the daemon:

```sh
systemctl --user status libpebble3d.service
journalctl --user -u libpebble3d.service -b
```

For reports, include the installed RPM version, SailfishOS version, watch and
firmware, transport, reproduction steps and relevant log excerpt. Remove
personal notification, contact and account information before sharing logs.

If a native build stops with `exec format error`, check ARM64 container
emulation. If packaging rejects staged artifacts, inspect the provenance error
and rebuild the required stage; do not bypass artifact verification.

## Thanks

* JPlexer - Getting libpebble3 building and working with Sailfish OS. And the awesome watchface.
* Ruslan N. Marchenko - Original Sailfish UI, Developer mode and much more
* Javispedro - Contributor to Pebbled, author of Saltoq and libwatchfish.
* Michael Zanetti - Author of RockWork
* Tomasz Sterna - Author of Pebbled
* Brian Douglass - RockWork contributor
* Katharine Berry - Old Pebble authority
* Robert Meijers, Philipp Andreas - Hints and tips
* Christopher Frost, Kristjan Räts, István Hovai, Allan Nordhøy, Omar Anwar Aglan, J Li, Nathan Follens, Olexandr Nesterenko, ssantos, Jakob Jespersen - Translators

## Watch artwork

The Pebble 2 Duo, Time 2 and Round 2 frames are rendered from the public CAD
models in [Core Devices' hardware repository](https://github.com/coredevices/hardware).
Source models: copyright Core Devices 2025; usage terms are in that repository.

The [frame renderer](ui/artwork/render-core-watch-frames.py) uses NumPy and
Pillow to generate the images with transparent openings for live screenshots.
