# libpebble3d

The unprivileged Pebble daemon owns the `org.rockpool` session-bus API.  During
the migration release it additionally exports the isolated `org.rockwork`
compatibility service on a different D-Bus connection.  The public contract,
platform ABI, and provider wire protocol are under `api/`, `include/`, and
`docs/`.

The canonical platform-provider header remains in this source tree so the
Native Image JNI loader can compile against it directly.  The Rockpool source
RPM packages the same file as `libpebble3d-platform-devel`; the daemon RPM
provides only the matching runtime ABI capability.

Platform ABI 1.4 adds safe Sailfish notification replies. The provider exposes
both notification and messaging domains, but the private helper wire carries
only a retained numeric notification ID and reply text. The helper accepts a
reply only once for a current `org.nemomobile.CommHistory` SMS/IM/MMS
notification whose input action resolves to the fixed Sailfish Messages
`sendMessage` API; notification hints cannot select an arbitrary D-Bus call.

## Build

`build.sh` first builds the daemon JVM distribution on the host, then builds
and runs an AArch64 Docker image containing GraalVM Native Image.  GraalVM does
not need to be installed on the host.

### Prerequisites

- Git with submodule support.
- JDK 17 for the host-side Gradle build.  Set `JAVA_HOME` if JDK 17 is not the
  system default.
- An Android SDK.  The Android Gradle plugin needs an SDK while configuring the
  multi-platform source tree even though this build compiles only the JVM
  target.  Set `ANDROID_HOME`, or create `mobileapp/local.properties` with an
  `sdk.dir` entry.  `build.sh` also looks in `$HOME/Android/Sdk` and
  `$HOME/Library/Android/sdk`.
- Docker with permission to use the Docker daemon.
- Roughly 8 GiB of memory available to Docker.  Native Image can fail
  or stall under a substantially lower limit; the build caps compilation at
  four worker threads to stay within this budget.
- AArch64 container support.  The generated executable is AArch64-only because
  Native Image cannot cross-compile it.

On an AArch64 host, Docker can run the builder directly.  On an x86-64 host,
install the `binfmt_misc` AArch64 emulator once:

```sh
docker run --privileged --rm tonistiigi/binfmt --install arm64
```

The command changes the host's binary-format handlers and therefore needs
administrator privileges.  A quick check should print `aarch64`:

```sh
docker run --rm --platform linux/arm64 arm64v8/debian:bookworm uname -m
```

### Build from the pinned libpebble3 checkout

From the Rockpool repository root:

```sh
git submodule update --init libpebble3d/mobileapp
cd libpebble3d
export JAVA_HOME=/path/to/jdk-17
export ANDROID_HOME=/path/to/Android/Sdk
./build.sh
```

Omit either `export` when the corresponding tool is already discoverable as
described above.

To build against another libpebble3 checkout without changing the submodule,
set `MOBILEAPP` to the checkout containing the `libpebble3/` directory:

```sh
cd libpebble3d
MOBILEAPP=/path/to/libpebble3-checkout ./build.sh
```

When using `MOBILEAPP`, an SDK properties file must be at
`/path/to/libpebble3-checkout/local.properties`, not in the pinned
`mobileapp/` directory.  Setting `ANDROID_HOME` avoids that distinction.

`./build.sh --release` is the packaging-grade variant. It snapshots the
Rockpool HEAD commit and its pinned mobileapp commit, validates that snapshot,
and records the complete output-file inventory and SHA-256 digests. Uncommitted
changes are excluded, and later checkout edits or commits do not invalidate the
build. Ordinary `build.sh` output is deliberately marked as a
development build and cannot be reused by `package.sh`.

### Build stages and duration

The script performs five stages:

1. Build the daemon's JVM distribution with the Gradle wrapper.
2. Build the AArch64 Debian/GraalVM builder image.  Docker caches its packages
   and GraalVM download after the first build.
3. Compile the JNI platform-provider loader and install a temporary copy inside
   the builder for tracing.
4. Run the daemon for 45 seconds with the Native Image tracing agent on private
   system and session buses.
5. Compile the AArch64 executable with GraalVM Native Image.

The final stage is CPU- and memory-intensive.  An emulated x86-64 build has
taken roughly 60–90 minutes on the development host; a native AArch64 builder
should be faster.  Long pauses in the Native Image output are normal.

### Outputs

Successful builds write, replacing files from any earlier build:

- `out/libpebble3d` — the AArch64 daemon executable.
- `out/libpebble3d-platform-loader.so` — the AArch64 JNI platform-provider
  loader used by the executable.

These are raw packaging inputs, not an installable RPM.  Rockpool's Sailfish
platform proxy/helper is built and packaged separately.

## RPM packaging

The daemon and Sailfish integration deliberately come from separate builds:

- The libpebble3d build packages the prebuilt Native Image daemon and JNI
  loader.  It does not compile Qt code or package the platform ABI header.
- The Rockpool Sailfish build packages the Silica UI, the platform ABI header,
  and the Sailfish proxy/launcher/host.  It does not require Java or GraalVM.

### Create the Rockpool source archive

Create the RPM source archive from the repository root with:

```sh
rpm/create-source-archive.sh 2.0
```

The command writes `rockpool-2.0.tar.xz` in the current directory.  It first
requires every release-contract artifact to match its owning repository's
committed `HEAD`, verifies that `libpebble3d/mobileapp` matches the committed
submodule gitlink, then archives those two committed trees only.  It never
uses working-tree or untracked content.

### Package the daemon

By default, `package.sh` performs a fresh build before packaging:

```sh
cd libpebble3d
VER=2.0 ./package.sh
```

`VER` defaults to `2.0`. After an immediately preceding, verified
`build.sh --release` run, `--reuse-current-build` explicitly packages that
output without repeating the long Native Image build. Packaging first requires
a fully committed release tree. The reuse option verifies the exact Rockpool
and mobileapp commits, the complete artifact inventory, and every recorded
SHA-256; it rejects development, incomplete, changed, augmented, or older
output. The RPM is built with the exact recorded builder-image ID from a
separately verified read-only artifact snapshot and RPM metadata archived from
the recorded Rockpool commit; it never consumes mutable packaging inputs from
the live worktree.

Packaging reuses the `libpebble3d-builder` AArch64 Docker image and therefore
needs the same Docker and QEMU setup as the binary build.  It writes:

```text
libpebble3d/RPMS/libpebble3d-<VER>-1.aarch64.rpm
```

The RPM contains:

- `/usr/libexec/libpebble3d/libpebble3d` and every generated `out/*.so`, owned
  by `root:root` with mode `0755`;
- the `/usr/bin/libpebble3d` symlink;
- the `libpebble3d.service` user unit and its `user-session.target` enablement;
- the package-owned platform-provider directory; and
- a system `bluetooth.service` drop-in enabling the experimental BlueZ API
  needed for explicit LE connections on BlueZ older than 5.79.

Installing or removing the Bluetooth drop-in reloads systemd and restarts
`bluetooth.service`.  Installing or upgrading the daemon starts or restarts
its user service and stops/disables the retired `rockpoold.service` when it is
present.

The daemon RPM provides these package capabilities:

- `rockpool-dbus-api = 1`
- `libpebble3d-platform-abi = 1`
- `libpebble3d-platform-abi-minor = 4`
- `libpebble3d-platform-launcher-abi = 1`
- `rockwork-dbus-compat = 1` for the temporary UI migration release

The exact Native Image dependency set is intentionally not inferred by RPM;
the executable and its generated shared libraries are packaged together.

### Package the UI and Sailfish provider

The repository-root `rpm/rockpool.spec` is built with the normal Sailfish SDK
or OBS workflow.  For a configured local Sailfish OS 5.2 AArch64 target, a
typical Platform SDK command from the repository root is:

```sh
mb2 -t aarch64 --no-vcs-apply build
```

Target names vary between SDK installations; replace `aarch64` with the name
of the configured Sailfish OS 5.2 AArch64 target.  `--no-vcs-apply` is useful
for a working tree containing the local changes being tested and can be
omitted by a clean source-package workflow.  OBS builds the same spec from the
Rockpool source archive.

The spec cleans each qmake target after regenerating its Makefile.  This is
intentional: the package version is compiled into the UI and into all three
provider processes, while make does not otherwise rebuild objects merely
because a qmake `DEFINES` value changed.  Do not remove those clean steps or an
incremental package can contain mutually incompatible provider build IDs.

That spec produces three packages:

- `rockpool` — the Silica UI.  It requires `rockpool-dbus-api = 1` and, during
  the migration release, `rockwork-dbus-compat = 1`.
- `libpebble3d-platform-devel` — the architecture-independent Apache-2.0 ABI
  header and pkg-config file, currently versioned as platform SDK `1.4`.  It
  has no daemon runtime dependency.
- `libpebble3d-platform-sailfish` — the AArch64 provider package.  It requires
  both platform ABI capabilities supplied by the daemon RPM.

The provider installs these security-sensitive files with exact modes:

```text
root:root       0755  %{_libdir}/libpebble3d/platforms/libpebble3d-platform-sailfish.so
root:privileged 2755  %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-launcher
root:privileged 0750  %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-host
```

Its user-service drop-in replaces the daemon's direct `ExecStart` with the
small setgid launcher.  Installing, upgrading, or removing the provider
reloads the user manager and restarts `libpebble3d.service`.

For a functional Sailfish installation, install the `libpebble3d` daemon RPM,
the matching `libpebble3d-platform-sailfish` RPM, and the `rockpool` UI RPM.
The `libpebble3d-platform-devel` RPM is needed only when compiling a provider.
Set the daemon `VER` and Rockpool package version/release deliberately for the
same release; no tool cross-checks them.  Change the platform SDK version only
when its ABI changes.  The provider build ID is generated from the Rockpool
package version and release.


## Capability registry

Capabilities are stable dotted strings. Clients must check them before
showing an optional workflow; absence means that workflow is not implemented
by this build, not merely disabled. A present capability can still encounter a
temporary runtime failure such as Bluetooth being powered off or its selected
adapter being unavailable; the operation reports that failure normally.

| Capability | Scope | Meaning |
| --- | --- | --- |
| `transport.ble` | Manager | BLE discovery and connection are usable. |
| `transport.classic` | Manager | Classic discovery, pairing, and RFCOMM channel 1 are usable. |
| `watch.concurrent` | Manager | More than one watch can be connected. |
| `discovery.bond-import` | Manager | Existing BlueZ bonds can be imported without unpairing. |
| `account.rebble` | Account | Rebble account-backed sync is configured. |
| `platform.provider` | Platform | A validated platform provider is active. |
| `platform.notifications` | Platform | Notification/action forwarding is healthy. |
| `platform.messaging` | Platform | Replies and canned responses are healthy. |
| `platform.media` | Platform | Media control is healthy. |
| `platform.calls` | Platform | Call control is healthy. |
| `platform.calendar` | Platform | Calendar access is healthy. |
| `platform.contacts` | Platform | Contact lookup is healthy. |
| `platform.location` | Platform | Location updates are healthy. |
| `platform.time` | Platform | Time and timezone updates are healthy. |
| `platform.device-state` | Platform | MCE/device state is healthy. |
| `platform.profiles` | Platform | Sailfish profile integration is healthy. |
| `watch.firmware` | Watch | Firmware and recovery operations are supported. |
| `watch.language` | Watch | Language packs are supported. |
| `watch.apps` | Watch | Application management is supported. |
| `watch.timeline` | Watch | Timeline/calendar sync is supported. |
| `watch.notifications` | Watch | Notification filtering/actions are supported. |
| `watch.messaging` | Watch | Messaging/canned replies are supported. |
| `watch.health` | Watch | Health settings are supported. |
| `watch.screenshots` | Watch | Screenshots are supported. |
| `watch.logs` | Watch | Fixed-directory watch log dumping is supported. |
| `watch.developer-mode` | Watch | Local developer connection is supported. |

Provider domains report `ready`, `degraded`, `failed`, or `missing` in
`Platform1.Health`.  A degraded platform domain must not remove portable watch
capabilities.


## Stable error registry

Methods normally return an `Operation1`; operational failures are represented
by its `Error` property and never expose exception text, credentials, paths, or
Bluetooth keys.  `ErrorDetail` is a short, localized-safe diagnostic category.

| Error | Meaning |
| --- | --- |
| `org.rockpool.Error.Cancelled` | The caller cancelled the operation. |
| `org.rockpool.Error.InvalidArgument` | A typed input record was malformed or inconsistent. |
| `org.rockpool.Error.NotSupported` | The capability is absent for this system or watch. |
| `org.rockpool.Error.NotFound` | The requested watch, operation, or item no longer exists. |
| `org.rockpool.Error.Busy` | A conflicting operation is already active. |
| `org.rockpool.Error.NotConnected` | The operation needs a connected compatible watch. |
| `org.rockpool.Error.PairingFailed` | Bluetooth pairing or authentication failed. |
| `org.rockpool.Error.TransportFailed` | BLE, Classic, or RFCOMM transport failed. |
| `org.rockpool.Error.AuthenticationFailed` | Account authentication was rejected or expired. |
| `org.rockpool.Error.ProviderUnavailable` | The platform provider is missing, failed, or latched. |
| `org.rockpool.Error.ProviderProtocol` | The private provider protocol rejected a message. |
| `org.rockpool.Error.PermissionDenied` | Sailjail or platform policy denied an operation. |
| `org.rockpool.Error.IO` | A constrained local file-descriptor or fixed log output failed. |
| `org.rockpool.Error.Internal` | An unexpected failure was sanitized. |

Invalid D-Bus signatures and unknown object paths remain normal D-Bus dispatch
errors.  They are not a replacement for this registry.


## Public records

The XML fixes container signatures; this document fixes the keys and value
types inside public `a{sv}` records. Unknown keys may be added in a later API
version, so clients must ignore keys they do not understand.

### Bond import result

`Discovery1.ImportBondedWatches` succeeds with an `Operation1.Result` holding
these non-negative aggregate counters. All are `u` (`UInt32`) values.

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `imported` | `u` | Bonds newly imported as watches. |
| `existing` | `u` | Bonds already represented by a known watch. |
| `aliases` | `u` | Duplicate transport addresses merged while planning recognized logical watches. |
| `unsupported` | `u` | Recognized Pebble bonds with no runtime-supported transport. Unrelated bonds are filtered before aggregation. |

### Application records

`Applications1.Applications` is ordered by ascending locker order and scoped to
the watch object. It contains compatible built-in system applications and the
compatible account-locker applications selected for synchronization to that
watch. The account locker may contain additional applications outside the
current per-type synchronization limit or built for another watch platform.

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `uuid` | `s` | Canonical application UUID. |
| `storeId` | `s` | Rebble store ID, or an empty string. |
| `name` | `s` | Display name. |
| `vendor` | `s` | Developer name. |
| `watchface` | `b` | True for a watchface. |
| `version` | `s` | Application version, or an empty string. |
| `hasSettings` | `b` | The application has a configuration page. |
| `icon` | `s` | Preferred platform icon URL, or an empty string. |
| `systemApp` | `b` | Built into PebbleOS and not removable. |

`Applications1.Remove` accepts only the canonical `uuid`. Its successful
`Operation1.Result` contains that same `uuid` (`s`). Removal is global rather
than specific to the watch object on which the method was called: it removes
the application from the shared locker state, updates the remote account
locker when applicable, and schedules deletion from every watch to which the
application was synchronized.

### Screenshot records

`Screenshots1.Screenshots` is the user-global history in
`~/Pictures/Screenshots/Pebble`, exposed on each watch object and ordered by
descending creation time and then descending ID. Only daemon-named regular PNG
files in that exact directory are listed.

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `id` | `s` | Generated PNG basename. |
| `path` | `s` | Absolute fixed-directory path. |
| `mime` | `s` | Always `image/png`. |
| `created` | `t` | Unix timestamp in milliseconds. |

A successful `Screenshots1.Capture` returns the new screenshot record directly
as `Operation1.Result` and then changes the `Screenshots` property.

### Global watch settings

`Health1.Settings`, primary `Messaging1.CannedResponses`, and
`Timeline1.CalendarEnabled` are exposed on every watch object for UI
convenience, but their backing libpebble3 state is account-global. A successful
mutation changes the corresponding property on every exported watch object;
clients must not treat these values as independent per-watch preferences.

Primary and compatibility notification-filter mutations likewise converge on
one canonical collection below `notifications.`. Every successful mutation
stores `configured=true`, including an empty collection. The marker makes an
explicitly empty policy authoritative across restarts and prevents a later
legacy-import retry from restoring retired per-watch filter records.

`Health1.Settings` is an `a{sv}` record with these fixed fields:

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `enabled` | `b` | Health tracking is enabled. |
| `age` | `i` | Age in years. |
| `height` | `i` | Height in centimetres. |
| `gender` | `i` | libpebble3 gender enum ordinal (`0..2`). |
| `weight` | `i` | Weight in kilograms. |
| `moreActive` | `b` | Activity insights are enabled. |
| `sleepMore` | `b` | Sleep insights are enabled. |
| `imperialUnits` | `b` | Imperial display units are enabled. |

Each `Messaging1.CannedResponses` element is an `a{sv}` record containing
`id` (`s`), `name` (`s`), and ordered `values` (`as`). The group records are
canonical below `primary.messaging.canned.`. Every successful replacement also
stores `configured=true`, including an empty collection. Startup and background
retry reconcile that canonical collection into libpebble3's flattened, trimmed,
non-empty, de-duplicated canned-response configuration; a marker-only collection
therefore durably clears the generic replies. Compatibility response groups
remain a separate source-specific store and are not flattened into this primary
list. A primary replacement is rejected before its commit if the complete
projected libpebble3 JSON record, across every later daemon-controlled calendar
and diagnostic variant, would exceed the daemon JVM settings backend's
single-value capacity. An oversized pre-existing canonical collection is
retained for repair and does not replace the last active replies or trigger
periodic retries; a later valid replacement recovers. Compatibility
`setCannedResponses` validates bounded source-keyed data, filters malformed old
groups, and atomically merges only the supplied groups: an explicit empty group
clears that source, while omitted valid sources remain durable. It never changes
the primary list or libpebble3's generic canned-response configuration.

Compatibility health records retain the historical `female`/`male` string
shape. Compatibility writes reject `other` and ordinal `2`, which that record
cannot round-trip; the primary `Health1.Settings` record still supports ordinals
`0..2`.

Historical movement, sleep, and heart-rate rows do not contain a watch
identifier. The compatibility `HealthOverview` dashboard therefore projects
bounded account-global history and labels it as shared across the Rockpool
account. `FetchHealthData` addresses the selected connected watch and waits for
its incremental-sync acknowledgement; every completed database update emits
`HealthDataChanged` on all compatibility watch objects. This compatibility view
does not add historical records to the primary `org.rockpool` API or claim
per-watch provenance.

#### Legacy global-settings migration

The one-time reconciliation waits until v1 per-watch import is complete. Config
freshness applies only to calendar: libpebble3 records its generated-default
origin before saving that generated config, so the origin survives a crash and
restart. Only a generated-default config may adopt a valid unanimous legacy
calendar value; any canonical calendar record or config not generated from
defaults wins, including a stored `true` default. Complete health records and
independent imperial-unit candidates are reconciled only when each is unanimous;
any raw existing health row wins. Health initialization and ordinary daemon
writes commit all four health rows in one awaited Room transaction. Conflicting,
malformed, or failed values are retained and defer or select no global value as
appropriate. Durable canonical calendar and primary canned state are reapplied
in the background after restart, and account-global plus watch-origin health
property changes fan out to every exported watch. Legacy canned groups remain
source-scoped and are recorded as preserved rather than flattened.


## Functional parity matrix

This matrix records the intended replacement for useful Rockwork workflows.
It is deliberately not a list of obsolete endpoints to carry forward.

| Legacy workflow | org.rockpool replacement | Owner |
| --- | --- | --- |
| Watch list and status | ObjectManager + `Watch1` properties | libpebble3d |
| BLE pairing | `Discovery1.Pair` | libpebble3 |
| Classic pairing/RFCOMM | `Discovery1.Pair` with `transport=classic` | libpebble3 + Sailfish socket bridge |
| Existing bonded watch | Explicit `ImportBondedWatches` imports existing BlueZ bonds without unpairing | libpebble3 + Rockpool integration |
| Connect/disconnect/forget | `Watch1` operations; Forget removes all selected-adapter BlueZ aliases before portable state | libpebble3 + Rockpool integration |
| Apps/watchfaces | `Applications1` FD operations | libpebble3 |
| Firmware/recovery/language | `Firmware1` FD operations | libpebble3 |
| Timeline/calendar | Account-global `Timeline1.CalendarEnabled`; internal phone-calendar reconciliation preserves the last complete local projection on unavailable, denied, or failed source reads and applies successful replacements atomically. `watch.timeline` and `platform.calendar` remain absent pending a typed calendar domain | libpebble3d |
| Notifications/actions/replies | `Notifications1`/`Messaging1`; replies are available only for a live, trusted Sailfish SMS/IM/MMS notification with one narrowly validated input route, and are consumed after one attempt. Canonical primary canned groups are account-global and replayed into libpebble3 (including an explicit empty collection), while compatibility groups remain source-scoped and are not reply actions | libpebble3 + provider |
| Calls/media/contacts/location/profiles | Watch domains + provider | provider |
| Health and units | Account-global `Health1` settings projection on every watch; compatibility health strings round-trip only `female`/`male`. The compatibility UI exposes the bounded legacy health dashboard and addressed incremental sync, explicitly labelled as shared account history rather than per-watch provenance | libpebble3d |
| Weather | Compatibility locations receive keyless automatic forecasts for saved coordinates and still accept validated external injection. Migration imports a single physical legacy saved-location collection, or a unanimous collection from eligible legacy watch directories; conflicting legacy collections are preserved without choosing one. The `n/a` current-location slot remains pending `platform.location` | libpebble3 + libpebble3d |
| Screenshots | `Screenshots1` | libpebble3 |
| Developer mode | `Developer1` | libpebble3 |
| Log export | `Logs1.Dump` to `~/Downloads/pebble.log` | libpebble3d |
| Rebble account sync | `Account1`, token write-only | libpebble3 |

Not migrated: WU/TWC keys, manual timeline test insertion, cloud-development
toggles, voice dump callbacks, old timeline blobs, JS caches, datalog state,
PBW caches, and obsolete provider credentials.


## Sailfish platform launcher and host wire protocols

This is a private protocol between the unprivileged Sailfish provider proxy and
its privileged-group host.  A smaller fixed control protocol connects both to
the session launcher.  Neither is a D-Bus or plugin ABI, and neither is ever
available to a watch or application.

### Transport and process rules

The root-owned `2755` launcher is the only setgid executable.  Its user-service
unit names no user: the launcher inherits the active session UID and real GID.
It rejects any parent other than the installed systemd user manager in that
UID's `user@<uid>.service/init.scope`, and requires its own cgroup to be the
installed `libpebble3d.service`.  It temporarily selects the session GID while
reading the manager's procfs identity, then restores its validated saved
`privileged` GID.  Directly executing the launcher, daemon, or host therefore
cannot obtain a platform connection.

The launcher creates an `AF_UNIX`, `SOCK_SEQPACKET`, `SOCK_CLOEXEC` control
pair and starts the fixed root-owned daemon with descriptor 3 after restoring
all of the daemon's group IDs to its real session GID.  It does not start a
host until the daemon JNI bootstrap has set `PR_SET_DUMPABLE` to zero, disabled
core dumps, set `PR_SET_NO_NEW_PRIVS`, validated `SO_PEERCRED`, and sent the
fixed `DaemonReady` record.  The launcher remains the daemon's parent and
retains only the ability to start or stop the fixed package-owned host.

The proxy duplicates the inherited control descriptor; it never forks or
spawns.  A `StartHost` control record makes the launcher create a separate
`SOCK_SEQPACKET` data pair, execute the root-owned `0750` host with the session
UID after normalizing all real/effective/saved GIDs to `privileged`, and return
exactly one daemon endpoint with `SCM_RIGHTS`.  `StopHost` is the only other
command.  Control records are fixed 16-byte native records containing magic,
version, type, status, and PID; all unexpected fields or descriptor counts are
terminal.

The host accepts only its descriptor 3.  Before Qt it verifies that all of its
group IDs and the socket creator's effective GID are `privileged`, its parent
PID matches `SO_PEERCRED`, and its own installed file is exactly
`root:privileged 0750`.  It then clears its environment except the fixed
whitelist, replaces descriptors 0 through 2 with `/dev/null`, closes all
unrelated descriptors, reasserts its group IDs, disables core dumps, and calls
`prctl(PR_SET_NO_NEW_PRIVS, 1)`.  Both proxy and host compile the RPM build ID
and exchange it during `Hello`/`HelloAck`.  The protocol never accepts a path,
shell command, SQL, D-Bus destination, process name, or shared-library name.

### Frame format

Each `SOCK_SEQPACKET` packet is exactly one little-endian frame:

```text
u32 total_length       // header + payload, 24..65536
u16 major              // currently 1
u16 minor              // currently 4
u16 type
u16 flags              // zero unless specified for type
u64 request_id         // zero for handshake, Health, and Event frames
u32 payload_length     // total_length - 24
u8  payload[payload_length]
```

Both peers reject a packet whose length, version, flags, request ID, payload
encoding, or state transition is invalid.  Receivers drain neither a partial
frame nor a second frame after an invalid packet; they close the socket.

Payload records are fixed-width fields followed by explicitly sized UTF-8 or
byte views. Strings are strict UTF-8 without embedded NULs or termination. A
record may not
contain an offset/length outside its payload and no nested length may exceed
the enclosing record. Maximum frame size is 64 KiB. Blob framing is reserved
for later domains; neither peer currently accepts a blob record.

The time domain uses fixed read-only records:

```text
Request TimeGet (request_id != 0, payload size 4)
    u16 operation = 1
    u16 reserved = 0

Complete TimeReply (matching request_id, payload size 24)
    u16 operation = 1
    u16 reserved = 0
    u32 status
    i32 utc_offset_seconds
    i64 unix_ms
    u32 is_24_hour             // exactly 0 or 1

Event TimeChanged (request_id = 0, payload size 20)
    u16 event = 1
    u16 reserved = 0
    i32 utc_offset_seconds
    i64 unix_ms
    u32 is_24_hour             // exactly 0 or 1
```

A non-OK `TimeReply` has zero in every value field. The peers reject unknown
operations, nonzero reserved fields, unknown status values, UTC offsets outside
plus or minus 24 hours, and non-Boolean format values. The helper reads the
single constant Sailfish `/sailfish/i18n/lc_timeformat24h` setting; no setting
key or other selector crosses the protocol.

After `Ready`, the helper sends an initial 24-byte `Health` payload and sends a
new snapshot whenever a notification, volume, or call monitor changes
availability. The three
`u64` values are the ready, degraded, and failed domain masks. They are
disjoint and together contain every domain implemented by this wire minor
(currently notifications, messaging, media, calls, and time). A missing or disconnected monitor
therefore degrades only its domain without hiding or failing the others.

The notification domain sends incoming notifications as events and accepts
only narrow actions against a helper-retained numeric notification ID:

```text
Event NotificationPosted/NotificationClosed (request_id = 0)
    u16 event                   // 2 = posted, 3 = closed
    u16 reserved = 0
    u32 flags                   // bit 0 safe default action; bit 1 safe reply
    i64 timestamp_ms
    u32 close_reason            // 0 unknown, otherwise specification values 1..4
    u32 id_length
    u32 replaces_id_length
    u32 application_id_length
    u32 application_name_length
    u32 title_length
    u32 body_length
    u32 category_length
    u32 icon_name_length
    u8  strings[...]            // concatenated in the order above

Request NotificationCommand (request_id != 0)
    u16 operation = 2
    u16 reserved = 0
    u32 command                 // 1 = dismiss, 2 = reserved open action
    u32 id_length
    u8  id[id_length]

Complete NotificationStatus (matching request_id, payload size 8)
    u16 operation = 2
    u16 reserved = 0
    u32 status
```

IDs are decimal notification-server IDs and at most 64 bytes. Application ID
and name are at most 256 bytes, title 512, body 4096, and category/icon name
128 each. A posted event requires an application ID and non-empty title or
body. A closed event has only ID and close reason; all other fields are zero or
empty. The helper drops transient, hidden, group-summary, and empty
notifications before encoding.

The command contains no D-Bus destination, object path, interface, method,
arguments, file path, or generic payload. Dismissal always targets the fixed
notifications service and is accepted only for an ID observed as active by the
helper. This protocol does not expose an open action: notification-supplied remote
action tuples are untrusted and are neither parsed nor retained. A future open
action must target one fixed Sailfish application-launcher API using a
validated application identity. Dismiss reports success only after a bounded
reply from the fixed notification service; a missing, rejected, or timed-out
service call fails without discarding the retained ID.

Minor 4 adds the messaging domain and a single safe reply request. It carries
only the helper-retained decimal notification ID and bounded UTF-8 reply text:

```text
Request MessageReply (request_id != 0)
    u16 operation = 5
    u16 reserved = 0
    u32 notification_id_length  // decimal ID, 1..64 bytes, no leading zero
    u32 text_length             // strict UTF-8, 1..512 bytes
    u8  notification_id[notification_id_length]
    u8  text[text_length]

Complete MessageReplyStatus (matching request_id, payload size 8)
    u16 operation = 5
    u16 reserved = 0
    u32 status
```

The helper grants reply authority only when the current unique owner of
`org.nemomobile.CommHistory` posted an active notification in exactly one of
the `x-nemo.messaging.sms`, `x-nemo.messaging.im`, or
`x-nemo.messaging.mms` categories. Its action must be an `input` action whose
strictly parsed `x-nemo-remote-action-*` value names exactly the fixed
`org.sailfishos.Messages`, `/`, `org.sailfishos.Messages`, `sendMessage`
route and contains exactly two bounded QString route arguments (account path
and recipient). The private wire never carries those route arguments or any
D-Bus tuple. The helper consumes the retained authority before issuing the
fixed three-QString `sendMessage(account, recipient, replyText)` call, so a
reply is one-shot even on a timeout or error. Owner changes, notification
replacement/removal, and bounded active-ID eviction revoke it.

Hints are input to this narrow parser, not executable authority. In
particular, Telepathy ChannelDispatcher/DRAFT routes and arbitrary hinted
destinations, paths, interfaces, members, or argument lists are never used.

The calls domain reports the one call selected for Pebble phone-control and
accepts only fixed operations against the helper-observed handler ID:

```text
Event CallChanged (request_id = 0)
    u16 event = 4
    u16 reserved = 0
    u32 state                   // 0 ended, 1 ringing, 2 dialing,
                                // 3 active, 4 held
    u32 id_length
    u32 name_length
    u32 number_length
    u8  strings[...]            // ID, name, number in that order

Request CallCommand (request_id != 0)
    u16 operation = 3
    u16 reserved = 0
    u32 command                 // 1 answer, 2 hang up, 3 silence
    u32 id_length
    u8  id[id_length]

Complete CallStatus (matching request_id, payload size 8)
    u16 operation = 3
    u16 reserved = 0
    u32 status
```

Call IDs are 1..128 ASCII bytes from `[A-Za-z0-9_]`, matching only the fixed
`/calls/<id>` object-path component exposed by `org.nemomobile.voicecall`.
Names and numbers are strict UTF-8 and at most 256 bytes each. Ended events
carry the prior non-empty ID and no name or number, so a delayed end cannot
clear a newer call. Answer and hang-up require the exact selected live ID;
silence requires an empty ID and targets only the fixed manager method. No
D-Bus service, path, interface, method, or arbitrary argument crosses the
private protocol.

The media provider owns only Sailfish system-output volume. Generic MPRIS
metadata and transport controls remain in the daemon's portable media backend
and never cross this provider wire:

```text
Event MediaVolumeChanged (request_id = 0, payload size 12)
    u16 event = 5
    u16 reserved = 0
    u32 flags = 1               // bit 0: system output volume
    i32 volume_percent          // inclusive range 0..100

Request MediaCommand (request_id != 0, payload size 8)
    u16 operation = 4
    u16 reserved = 0
    u32 command                 // 4 = volume up, 5 = volume down

Complete MediaStatus (matching request_id, payload size 8)
    u16 operation = 4
    u16 reserved = 0
    u32 status
```

The volume event requires exactly the system-output-volume flag and no other
flags. The provider accepts only volume up and volume down; play/pause, next,
and previous return `NOT_SUPPORTED` without being sent to the helper. Commands
contain no player name, MPRIS object path, D-Bus destination, or arbitrary
argument. A provider-side volume action targets only the fixed Sailfish system
volume API.

### Ordering and backpressure

The only valid start sequence is `Hello`, `HelloAck`, `Ready`.  Every request
receives one `Complete` or `Cancelled` result; events use request ID zero.
`Cancel` is idempotent but does not permit another result after `Complete`.
The proxy permits at most 64 outstanding requests and each direction has a
bounded 64-message event queue.  Queue saturation completes the affected
request with `LP3_PLATFORM_BUSY`; it never blocks a Qt event-loop thread.

The proxy retries a failed host after 1, 5, then 30 seconds by requesting a new
fixed host from the launcher.  An unsuccessful third retry within ten minutes
latches the provider state to `failed`; only an explicit platform restart
clears the latch. The daemon and Bluetooth remain operational. A domain is
advertised only when its request path and bounded typed event dispatcher are
both active. The current provider implements time, notifications, media, and
calls and reports their health independently.
