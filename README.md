# Rockpool, Pebble support for Sailfish

[TMO thread](http://talk.maemo.org/showthread.php?t=96490) [Openrepos](https://openrepos.net/content/abranson/rockpool)

Rockpool provides the Sailfish UI and platform integration for Pebble watches.
Since version 2.0, its watch daemon is the bundled `libpebble3d` service;
Rockpool communicates with it through the `io.rebble.libpebble3` D-Bus API.

## Features

Rockpool provides watch discovery, pairing and connection management; app and
watchface management; notifications; calls and media controls; screenshots;
watch firmware and language workflows; health, profile and timeline settings;
keyless forecasts for saved weather locations; developer tools; and Rebble
account integration. Availability is reported at runtime because several
workflows depend on the connected watch, the installed platform provider, or
transport support.

See the [capability registry](libpebble3d/README.md#capability-registry) for the runtime
contract and the [functional parity matrix](libpebble3d/README.md#functional-parity-matrix)
for implemented and intentionally pending replacement workflows. In
particular, clients must not assume that calendar sync, contacts, location,
message replies, or Unix-FD installation are available when their capability
is absent.

## Building

Rockpool has a two-phase build because GraalVM Native Image cannot
cross-compile and does not belong in a Sailfish SDK target:

1. On the normal host, build the AArch64 `libpebble3d` executable and JNI
   loader in the pinned AArch64 Docker/GraalVM environment.
2. Inside the Sailfish Platform SDK, build the UI and platform provider and
   package those prebuilt native artifacts in the same RPM transaction.

The repository-root
[`build-libpebble3d.sh`](build-libpebble3d.sh) handles only the first phase.
After it succeeds, use `mb2` normally for the entire Sailfish build. Do not use
any separate daemon packaging step; the Sailfish SDK is the sole RPM builder.

### Prerequisites

Clone with submodules, or initialize the pinned libpebble3 checkout after an
existing clone:

```sh
git submodule update --init libpebble3d/mobileapp
```

The normal host needs:

- a POSIX shell and the usual Git/build utilities: Git, `awk`, `sed`, `find`,
  `tar`, `xz`, `sha256sum`, and `file`;
- JDK 17 for Gradle (set `JAVA_HOME` when it is not the default Java);
- an Android SDK with Android SDK Platform 36 installed and its licenses
  accepted, because the Android Gradle plugin configures the shared libpebble3
  project even for JVM-only tasks (set `ANDROID_HOME`, or add `sdk.dir` to
  `libpebble3d/mobileapp/local.properties`);
- Docker, permission to use its daemon, network access for the first Gradle
  and image build, and approximately 15 GiB of free disk space; and
- an AArch64 Docker runtime. On x86-64, install binfmt/QEMU support once and
  verify it:

```sh
docker run --privileged --rm tonistiigi/binfmt --install arm64
docker run --rm --platform linux/arm64 arm64v8/debian:bookworm uname -m
```

Allow at least 8 GiB of memory for Docker; 12 GiB is recommended. An emulated
x86-64 Native Image build commonly takes 60–90 minutes, while a native AArch64
host should be faster. GraalVM itself, an AArch64 compiler, and the native
Linux libraries are pinned in `libpebble3d/Dockerfile` and are not host
prerequisites.

The packaging phase needs a current Sailfish Platform SDK, `mb2`, and a clean,
project-specific AArch64 target created from the Sailfish release being
targeted. Do not install project packages into a base, shared, or default SDK
target. The complete SDK build dependency list is authoritative in
[`rpm/rockpool.spec`](rpm/rockpool.spec): Qt 5 Core, DBus, QML, Quick, Network,
Positioning and Contacts; `dbus-1`; `mlite5`; `libmkcal-qt5`;
`KF5CalendarCore`; `sailfishapp` 0.0.10 or newer; `desktop-file-utils`;
`qt5-qttools-linguist`; and `file`. The package requires
`sailfish-components-webview-qt5` at runtime. Rockpool uses its QML plugin and
does not link directly to the browser-generation-specific `qt5embedwidget`
library.
Missing packages must be made available to that disposable target rather than
added to the SDK base.

### Development build

From the repository root on the normal host:

```sh
export JAVA_HOME=/path/to/jdk-17
export ANDROID_HOME=/path/to/Android/Sdk
./build-libpebble3d.sh
```

The GraalVM build first creates `libpebble3d/out/libpebble3d` and
`libpebble3d/out/libpebble3d-platform-loader.so`. The script verifies them and
stages the spec inputs as `rpm/native/libpebble3d`,
`rpm/native/libpebble3d-platform-loader.so`, and
`rpm/native/.build-provenance`. The entire `rpm/native/` directory is ignored
by Git. To restage an already completed and verified `libpebble3d/out/` build,
use `./build-libpebble3d.sh --reuse`.

Open the same checkout inside the Sailfish Platform SDK and run:

```sh
mb2 -t TARGET --no-vcs-apply build
```

Replace `TARGET` with the exact name reported by the SDK, for example
`aarch64`. No Rockpool wrapper is involved in the Sailfish phase.

The SDK build produces one `rockpool` RPM containing the Silica UI, Native
Image daemon and JNI loader, Sailfish provider, launcher and helper, service
files, and BlueZ drop-in. The development RPM is written below `RPMS/` by `mb2`.
Runtime dependencies such as `systemd-user-session-targets`, Geoclue, and Qt
libraries are recorded in the RPM metadata and should be resolved by the
device package manager.

### Release build

A release requires the Rockpool tree and `libpebble3d/mobileapp` submodule to
be clean and committed, with the submodule gitlink matching its checkout.
Build the immutable Native Image input on the host after the release commits:

```sh
./build-libpebble3d.sh --release
```

Then, in the Sailfish Platform SDK:

```sh
mb2 -t TARGET --no-vcs-apply build
```

`--release` rejects development or stale native artifacts and verifies their
source commits, builder image, ABI versions, complete file inventory, and
SHA-256 digests. `mb2` writes the resulting package below `RPMS/`. If a
reproducible source archive is also required, run
`rpm/create-source-archive.sh 2.0` after the release native build; the archive
contains the same verified native input.

More detail about the Native Image stages and platform ABI is in
[`libpebble3d/README.md`](libpebble3d/README.md).

## The thanks

* Ruslan N. Marchenko - Sailfish UI, Developer mode and much more
* Javispedro - Contributor to Pebbled, author of Saltoq and libwatchfish.
* Michael Zanetti - Author of RockWork
* Tomasz Sterna - Author of Pebbled
* Brian Douglass - RockWork contributor
* Katharine Berry - Pebble authority
* Robert Meijers, Philipp Andreas - Hints and tips
* Christopher Frost, Kristjan Räts, István Hovai, Allan Nordhøy, Omar Anwar Aglan, J Li, Nathan Follens, Olexandr Nesterenko, ssantos, Jakob Jespersen - Translators


## Watch artwork provenance

The `pebble-2-duo-*.png`, `pebble-time-2-*.png`, and
`pebble-round-2-*.png` frames are rendered from the public external CAD models
in the Core Devices hardware repository:

- `watch/Pebble 2 Duo (asterix)/20250918 Pebble 2 Duo - Solid model.STL`
- `watch/Pebble Time 2 (obelix)/2026-04-08 Pebble Time 2 - 3D CAD Solid
  Model.STL`
- `watch/Pebble Round 2 (getafix)/Pebble Round 2 - External 3D CAD -
  14mm.stl`
- `watch/Pebble Round 2 (getafix)/Pebble Round 2 - External 3D CAD -
  20mm.stl`

Source: <https://github.com/coredevices/hardware>

The repository states that its files are free to use to research, learn about,
code, and hack on Core Devices products. Copyright Core Devices 2025.

Source SHA-256 checksums:

- Pebble 2 Duo STL:
  `fa9b42bf877c0012eb3d4dd05425ae1e89f5af8d72e876446a62f3b1f23edb5a`
- Time 2 STL:
  `fb7c75e955e26de21611c81f73eb72bfe24a89df8b0b5064c730c88cecfa6311`
- Round 2 14mm STL:
  `917d40b36cef9369edbd488a5a743a06e1c895309e695f766e6d06ffa98d82dc`
- Round 2 20mm STL:
  `df4be31aeb930c3ee3d2abd7ef06cc4c6e7bcbd79331e636e0ec081244b6a3d6`

Run `ui/artwork/render-core-watch-frames.py` with the four downloaded STL paths to
regenerate the transparent UI frames. The renderer uses only NumPy and Pillow;
it colours the case and strap for each protocol colour and leaves the exact
watch display area transparent for Rockpool's live screenshot overlay.

Pebble 2 Duo is rendered on the legacy 236x372 frame canvas with its native
144x168 display opening and 22mm strap. Protocol colours 34 and 35 use the
same official case geometry with black and white finishes.

Round 2 protocol colours 40 through 43 are, in order: Black 20mm, Silver 20mm,
Gold 14mm, and Silver 14mm. Each image is rendered from the matching case and
strap-width CAD rather than treating the colour as independent of watch size.
