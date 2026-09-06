# libpebble3d

The unprivileged Pebble daemon owns the `io.rebble.libpebble3` session-bus API.
It additionally exports the private `org.rockpool` UI facade on a different
D-Bus connection. The public contract, platform ABI, and provider wire
protocol are under `api/`, `include/`, and `docs/`.

The canonical platform-provider header remains in this source tree so the
Native Image JNI loader and Sailfish provider can compile against it directly.
It is a private in-tree build interface and is not installed by the Rockpool
RPM.

The primary API now exposes addressed application launch/close, live running
state, bounded PKJS configuration URL/result handoff, firmware discovery,
recovery state and update progress, plus FD-backed PBW, firmware and language
installation. The daemon replaces libpebble3's non-FD D-Bus transport at its
own runtime boundary and stages received regular files privately with explicit
size limits. The reference-bus JVM round trip is covered; Native Image and
installed-package verification remain release gates. App ordering remains
pending until desired locker state can be represented per watch rather than
globally.

Platform ABI 1.7 adds an explicit bounded outbound-message command used only
after the daemon authenticates a watch Send Text action against its durable
favorite-contact projection. The primary `Messaging1` API exposes that same
bounded projection and sends only through a current stable method ID; callers
cannot supply an arbitrary destination at dispatch time. Platform ABI 1.6 added bounded read-only Sailfish
contact snapshots, exact phone-number lookup, and change notifications.
Platform ABI 1.5 added bounded read-only calendar snapshots. The isolated
helper reads mkcal on its worker thread, expands occurrences only inside the
requested window, and returns typed, paginated
calendar and event records. Platform ABI 1.4 added safe Sailfish notification
replies: the private helper wire carries only a retained numeric notification
ID and reply text, and the helper accepts a reply only once for a current
`org.nemomobile.CommHistory` SMS/IM/MMS notification whose input action
resolves to the fixed Sailfish Messages `sendMessage` API.

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
development build and cannot be used for a release package.

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

These are raw packaging inputs, not an installable RPM. The repository-root
`build-libpebble3d.sh` command verifies and stages them for a normal Sailfish
SDK `mb2` build.

## RPM packaging

Native Image construction and RPM construction are separate phases, but the
daemon, provider, and UI are distributed together in the single `rockpool`
RPM produced by the repository-root `rpm/rockpool.spec`. The supported
commands are:

```sh
# Normal host:
./build-libpebble3d.sh

# Sailfish Platform SDK:
mb2 -t TARGET --no-vcs-apply build
```

Use `./build-libpebble3d.sh --release` for committed, provenance-checked native
artifacts; the subsequent `mb2` command is unchanged. The root
[`README.md`](../README.md#building) documents every host and SDK prerequisite,
test, output, development step, and release step.
There is no standalone daemon or development RPM flow.

The RPM provides `libpebble3-dbus-api = 1` for the generic daemon API and
`rockpool-ui-dbus-api = 1` for the private UI facade. The C provider header
remains an internal build interface and is not installed as a development
package.

The Native Image executable and its generated shared libraries are treated as
one sealed set, so their exact ELF dependencies are deliberately not inferred
individually by RPM.

The provider installs these security-sensitive files with exact modes:

```text
root:root       0755  %{_libdir}/libpebble3d/platforms/libpebble3d-platform-sailfish.so
root:privileged 2755  %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-launcher
root:privileged 0750  %{_libexecdir}/libpebble3d/libpebble3d-platform-sailfish-host
```

Its user-service drop-in replaces the daemon's direct `ExecStart` with the
small setgid launcher.  Installing, upgrading, or removing the provider
reloads the user manager and restarts `libpebble3d.service`.

Installing the `rockpool` RPM installs the complete runtime atomically. The
provider build ID is generated from the Rockpool package version and release.


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
| `io.rebble.libpebble3.Error.Cancelled` | The caller cancelled the operation. |
| `io.rebble.libpebble3.Error.InvalidArgument` | A typed input record was malformed or inconsistent. |
| `io.rebble.libpebble3.Error.NotSupported` | The capability is absent for this system or watch. |
| `io.rebble.libpebble3.Error.NotFound` | The requested watch, operation, or item no longer exists. |
| `io.rebble.libpebble3.Error.Busy` | A conflicting operation is already active. |
| `io.rebble.libpebble3.Error.NotConnected` | The operation needs a connected compatible watch. |
| `io.rebble.libpebble3.Error.PairingFailed` | Bluetooth pairing or authentication failed. |
| `io.rebble.libpebble3.Error.TransportFailed` | BLE, Classic, or RFCOMM transport failed. |
| `io.rebble.libpebble3.Error.AuthenticationFailed` | Account authentication was rejected or expired. |
| `io.rebble.libpebble3.Error.ProviderUnavailable` | The platform provider is missing, failed, or latched. |
| `io.rebble.libpebble3.Error.ProviderProtocol` | The private provider protocol rejected a message. |
| `io.rebble.libpebble3.Error.PermissionDenied` | Sailjail or platform policy denied an operation. |
| `io.rebble.libpebble3.Error.IO` | A constrained local file-descriptor or fixed log output failed. |
| `io.rebble.libpebble3.Error.Internal` | An unexpected failure was sanitized. |

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

### Firmware state

`Firmware1` exposes the addressed watch's current firmware state without
exposing the candidate download URL:

| Property | D-Bus type | Meaning |
| --- | --- | --- |
| `FirmwareVersion` | `s` | Installed firmware version, or empty when unknown. |
| `LanguageVersion` | `s` | Installed language-pack version, or empty when unknown. |
| `Recovery` | `b` | The watch is connected in recovery mode. |
| `CheckingForUpdate` | `b` | A firmware catalogue request is active. |
| `UpdateAvailable` | `b` | A bounded candidate is available. |
| `CandidateVersion` | `s` | Candidate version, or empty. |
| `ReleaseNotes` | `s` | Bounded candidate release notes, or empty. |
| `UpdateState` | `s` | Stable state listed below. |
| `UpdateProgress` | `d` | Installation progress clamped to `0.0..1.0`. |

`UpdateState` is one of `disconnected`, `idle`, `checking`, `available`,
`up-to-date`, `check-failed`, `waiting-to-start`, `installing`,
`waiting-for-reboot`, or `failed`. Nested updater progress changes invalidate
`UpdateProgress`; connection/update transitions invalidate the complete
firmware property set.

`Firmware1.CheckForUpdate(force)` joins an already running check or subscribes
before triggering a new one so a fast checking/terminal transition cannot be
missed. Success returns `updateAvailable`, `candidateVersion`, `releaseNotes`,
and `recovery`. Provider failures are sanitized as
`io.rebble.libpebble3.Error.ProviderUnavailable`. Firmware and language installation
remain `NotSupported` until the FD transport requirement is met.

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
| `running` | `b` | This is the application currently running on the addressed watch. |

`Applications1.Remove` accepts only the canonical `uuid`. Its successful
`Operation1.Result` contains that same `uuid` (`s`). Removal is global rather
than specific to the watch object on which the method was called: it removes
the application from the shared locker state, updates the remote account
locker when applicable, and schedules deletion from every watch to which the
application was synchronized.

`Applications1.Launch` and `Close` address only the watch object on which they
are called. Both accept a canonical installed application UUID. Launch waits
for the watch to confirm the running application and returns `uuid` (`s`) plus
`alreadyRunning` (`b`); Close requires that exact application to be running,
waits for the stop state, and returns `uuid`. The `Applications` property is
invalidated when the watch reports a run-state change, including changes made
outside this API.

For configurable applications, `Applications1.RequestConfiguration` reuses a
current matching PKJS session or launches the application, then returns `uuid`
and the bounded opaque `url` (`s`) emitted by the app. The daemon serializes
requests within the session and does not open the URL itself.
`SubmitConfiguration` returns a bounded opaque result of at most 64 KiB to the
same still-active application session. It never launches a replacement session
or sends the result to a different application. Empty results are allowed for
cancelled configuration pages; embedded NULs are rejected.

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

`Health1.Settings`, primary `Messaging1.CannedResponses`/`Favorites`, and
`Timeline1.CalendarEnabled` are exposed on every watch object for UI
convenience, but their backing libpebble3 state is account-global. A successful
mutation changes the corresponding property on every exported watch object;
clients must not treat these values as independent per-watch preferences.

A successful `Timeline1.Sync` returns a bounded summary in `Operation1.Result`:

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `calendarCount` | `i` | Calendars in the complete platform snapshot. |
| `eventCount` | `i` | Event occurrences projected into Timeline pins. |
| `reminderCount` | `i` | Event reminders represented in the projection. |
| `calendarEnabled` | `b` | Whether account-global calendar pins were enabled for this reconciliation. |

Success means the durable local projection was replaced atomically. It does
not claim that an offline watch has acknowledged its BlobDB records; those are
delivered by the normal per-watch connection flow. An unavailable or failed
platform source fails with `io.rebble.libpebble3.Error.ProviderUnavailable` and leaves
the last complete projection intact.

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
the primary list or libpebble3's generic canned-response configuration. An
explicit empty `com.pebble.sendText` group clears the Send Text response record.
Compatibility favorite contacts are a complete replacement and are projected
with that group into the watch Contacts, CannedResponses, and AppConfigs
databases. The projection admits at most ten methods and requires every
displayed recipient to map to exactly one bounded Telepathy account/recipient
route; invalid new data is rejected before persistence, while malformed
persisted data leaves the last complete Room projection intact.

Each `Messaging1.Favorites` element represents one configured Send Text route:

| Key | D-Bus type | Meaning |
| --- | --- | --- |
| `contactId` | `s` | Stable UUID derived from the contact name. |
| `methodId` | `s` | Stable UUID authorizing this exact account/recipient route. |
| `name` | `s` | Contact display name. |
| `account` | `s` | Bounded Telepathy account object path. |
| `recipient` | `s` | Bounded provider recipient identifier. |
| `displayRecipient` | `s` | Recipient text projected to the watch; currently equal to `recipient`. |

`Messaging1.SetFavorites` replaces the complete account-global collection.
Its input requires `name`, `account`, and `recipient`; a client may round-trip
the other fields, but any supplied IDs or display recipient must match the
daemon-derived values. At most ten method records are accepted, and every
displayed recipient and account/recipient route must be unique. An empty array
durably clears the collection.

`Messaging1.SendText` accepts only a canonical `methodId` from the current
durable `Favorites` collection and a non-empty UTF-8 message of at most 512
bytes. It resolves the provider account and recipient inside the daemon and
never accepts an arbitrary destination at dispatch time. The operation crosses
its commit boundary immediately before the externally visible send. Its result
contains the sent `methodId`; stale or unknown methods fail with
`io.rebble.libpebble3.Error.NotFound`, and an unavailable platform provider fails with
`io.rebble.libpebble3.Error.ProviderUnavailable`.

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
does not add historical records to the primary `io.rebble.libpebble3` API or claim
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

This matrix records the intended replacement for useful Rockpool workflows.
It is deliberately not a list of obsolete endpoints to carry forward.

| Legacy workflow | io.rebble.libpebble3 replacement | Owner |
| --- | --- | --- |
| Watch list and status | ObjectManager + `Watch1` properties | libpebble3d |
| BLE pairing | `Discovery1.Pair` | libpebble3 |
| Classic pairing/RFCOMM | `Discovery1.Pair` with `transport=classic` | libpebble3 + Sailfish socket bridge |
| Existing bonded watch | Explicit `ImportBondedWatches` imports existing BlueZ bonds without unpairing | libpebble3 + Rockpool integration |
| Connect/disconnect/forget | `Watch1` operations; Forget removes all selected-adapter BlueZ aliases before portable state | libpebble3 + Rockpool integration |
| Apps/watchfaces | `Applications1` exposes the compatible per-watch projection, validated shared-locker removal, addressed launch/close with live running state, bounded request-scoped PKJS configuration URL/result handoff, and size-bounded FD-based PBW installation for a sole known watch. Per-watch desired ordering still needs a non-global locker model | libpebble3 |
| Firmware/recovery/language | `Firmware1` exposes installed versions, recovery mode, bounded update discovery metadata, stable update state, live progress, a race-safe explicit update check, and size-bounded FD-based firmware/language installation | libpebble3 |
| Timeline/calendar | Account-global `Timeline1.CalendarEnabled`; a bounded read-only Sailfish `platform.calendar` domain enumerates mkcal notebooks and expanded event occurrences through the isolated helper. The internal phone-calendar reconciliation preserves the last complete local projection on unavailable or failed reads and applies successful replacements atomically. `Timeline1.Sync` runs that reconciliation explicitly and returns calendar/event/reminder counts. Operation completion covers the local projection, while normal BlobDB delivery to disconnected watches remains asynchronous | libpebble3 + provider |
| Notifications/actions/replies | `Notifications1`/`Messaging1`; replies are available only for a live, trusted Sailfish SMS/IM/MMS notification with one narrowly validated input route, and are consumed after one attempt. A `default` action on the same authenticated target separately permits only the fixed `org.sailfishos.Messages.startConversation(ss)` conversation open; notification-provided open D-Bus tuples are never executed, and both actions require the current CommHistory owner. Canonical primary canned groups are account-global and replayed into libpebble3 (including an explicit empty collection), while compatibility groups remain source-scoped and are not reply actions | libpebble3 + provider |
| Calls/media/location/profile switching | Calls, media, and bounded one-shot/watch location use the independently healthy Sailfish provider; existing connection-driven profile switching remains daemon-owned | libpebble3 + provider |
| Contacts/outgoing Send Text | A bounded read-only Sailfish `platform.contacts` domain synchronizes QtContacts names/IDs into libpebble3 and resolves caller names by exact phone lookup. Primary `Messaging1.Favorites` exposes the same account-global durable favorite projection, `SetFavorites` replaces it, and `SendText` accepts only a current stable method ID rather than an arbitrary destination. Compatibility favorites and `com.pebble.sendText` canned replies are projected transactionally into Contacts, CannedResponses, and AppConfigs BlobDB records. The fixed watch Send Text action likewise requires a unique current projected recipient; both paths resolve the Telepathy route inside the daemon and send through the fixed Sailfish Messages command. Native-Linux notifications still do not supply participant lookup keys, so contact-specific notification policy remains pending | libpebble3 + provider |
| Health and units | Account-global `Health1` settings projection on every watch; compatibility health strings round-trip only `female`/`male`. The compatibility UI exposes the bounded legacy health dashboard and addressed incremental sync, explicitly labelled as shared account history rather than per-watch provenance | libpebble3d |
| Weather | Compatibility locations receive keyless automatic forecasts for saved coordinates and still accept validated external injection. Migration imports a single physical legacy saved-location collection, or a unanimous collection from eligible legacy watch directories; conflicting legacy collections are preserved without choosing one. The canonical `n/a` slot resolves through the bounded Sailfish Location provider without persisting coordinates | libpebble3 + libpebble3d + provider |
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
u16 minor              // currently 8
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
new snapshot whenever a notification, volume, call, calendar, contacts, or location monitor changes
availability. The three
`u64` values are the ready, degraded, and failed domain masks. They are
disjoint and together contain every domain implemented by this wire minor
(currently notifications, messaging, media, calls, calendar, contacts, location, and time). A missing or disconnected monitor
therefore degrades only its domain without hiding or failing the others.

Minor 5 adds a bounded, one-shot location query. It has no watch/update mode:
each accepted request produces exactly one `Complete`, or is cancelled. The
proxy validates the requested accuracy and timeout before it reaches the
helper; the helper must independently apply the same bound and stop any
underlying acquisition when it receives `Cancel`.

The Sailfish helper implements this operation with one shared Qt Positioning
source backed by the system GeoClue plugin. Coarse-only requests prefer
non-satellite positioning; any fine request permits all available positioning
methods so a useful fix can fall back when GPS is unavailable. Requests retain
independent monotonic deadlines, and a valid fix completes only the requests
that are still pending. Source errors retire the current Location generation
and degrade only the Location health domain.

```text
Request LocationQuery (request_id != 0, payload size 12)
    u16 operation = 6
    u16 reserved = 0
    u32 accuracy                  // 1 coarse, 2 fine
    u32 timeout_ms                // 1..30000

Complete LocationReply (matching request_id, payload size 28)
    u16 operation = 6
    u16 reserved = 0
    u32 status
    i32 latitude_e7               // -900000000..900000000 on success
    i32 longitude_e7              // -1800000000..1800000000 on success
    i32 accuracy_m                // non-negative on success
    i64 timestamp_ms              // positive on success
```

On a non-OK status every location value is zero. The helper has no authority
to select a provider, D-Bus destination, or arbitrary options from this
record. The proxy maps a valid terminal reply to the public
`LP3_PLATFORM_EVENT_LOCATION`, preserving the original request ID and status.
After cancellation, a late `Complete` is consumed against its bounded
tombstone, or discarded as an unknown retired ID, and is never published.
Helper disconnect, a Location health loss, or a
provider-generation reset retires outstanding request authority before a
replacement helper can report a result.

Minor 6 adds a read-only Calendar domain backed by mkcal and KCalendarCore in
the isolated helper. Requests contain no database path, SQL, D-Bus endpoint,
or arbitrary selector. Calendar and event results are paginated to 64 records,
at most 512 records may exist in one source snapshot, the event window is at
most 370 days, and the complete frame remains below 64 KiB. The helper expands
recurrence only inside the requested half-open window and applies the same
notebook visibility, exclusion, name, and colour settings as the Sailfish
calendar application.

```text
Request CalendarQuery (request_id != 0)
    u16 operation = 7
    u16 reserved = 0
    u32 kind                      // 1 calendars, 2 event occurrences
    u32 max_records               // 1..64
    u32 offset                    // 0..512
    i64 start_ms                  // zero for calendars
    i64 end_ms                    // zero for calendars; exclusive for events
    u32 calendar_id_length        // empty for calendars, 1..256 for events
    u8  calendar_id[calendar_id_length]

Complete CalendarReply (matching request_id)
    u16 operation = 7
    u16 reserved = 0
    u32 status
    u32 kind
    u32 next_offset               // zero when complete
    u32 record_count              // 0..64
    u8  bounded_typed_records[...] // calendars or expanded event occurrences

Event CalendarChanged (request_id = 0, payload size 4)
    u16 event = 6
    u16 reserved = 0
```

Calendar records carry bounded IDs, display and owner fields, colour, and
visible/enabled/sync flags. Event records carry occurrence and base IDs,
calendar ID, title, description, location, start/end milliseconds, all-day and
recurrence flags, availability/status, up to 16 attendees, and up to eight
minutes-before reminders. Non-OK replies contain no records or next offset.
Cancellation, Calendar health loss, helper replacement, malformed records,
oversized results, and non-advancing pagination all retire the query without
replacing libpebble3's last complete calendar projection. Storage changes emit
only a coalesced invalidation edge; libpebble3 then rereads a complete snapshot.

Minor 7 adds a read-only Contacts domain backed by QtContacts on its own helper
thread. Empty-query list reads are paginated to 64 records and capped at 4096
contacts. Exact phone lookups accept one bounded phone number and return at most
one record. The operation accepts no contacts-manager name, storage path, or
arbitrary filter expression.

Minor 8 adds a bounded outbound Send Text request. The daemon accepts this
operation only after the fixed watch action UUID and exact projected favorite
recipient resolve to one unique durable account route. The private wire carries
that resolved route, never a D-Bus destination, object path, interface, member,
signature, or arbitrary argument list.

```text
Request MessageSend (request_id != 0)
    u16 operation = 9
    u16 reserved = 0
    u32 account_id_length       // strict UTF-8 Telepathy path, 1..512 bytes
    u32 recipient_length        // strict UTF-8, 1..512 bytes
    u32 text_length             // strict UTF-8, 1..512 bytes
    u8  account_id[account_id_length]
    u8  recipient[recipient_length]
    u8  text[text_length]

Complete MessageSendStatus (matching request_id, payload size 8)
    u16 operation = 9
    u16 reserved = 0
    u32 status
```

Immediately before dispatch, the helper requires the monitored
`org.nemomobile.CommHistory` owner to remain current. It then issues only the
fixed empty-reply `org.sailfishos.Messages` `/`
`org.sailfishos.Messages.sendMessage(sss)` call with the three bounded values.
Timeouts and owner or connection generation changes fail without retrying the
externally visible send.

Minor 9 adds one idempotent Pebble-bond removal request. It carries only a
numeric BlueZ adapter index and six address octets; no D-Bus destination,
object path, interface, member, or caller-supplied device name crosses the
private protocol.

```text
Request PebbleBondRemove (request_id != 0, payload size 16)
    u16 operation = 10
    u16 reserved = 0
    u32 adapter_index
    u8  address[6]
    u16 reserved = 0

Complete PebbleBondRemoveStatus (matching request_id, payload size 8)
    u16 operation = 10
    u16 reserved = 0
    u32 status
```

The privileged helper derives the fixed BlueZ adapter and device paths. Before
calling `Adapter1.RemoveDevice`, it reads that exact `Device1` object and
requires the strict supported Pebble name form plus either a recognized Pebble
service UUID, the existing Classic-device class evidence, or the legacy
`Pebble Time Le XXXX` identity. Missing or already-unbonded objects succeed
without mutation. A non-Pebble object is rejected with
`LP3_PLATFORM_INVALID_ARGUMENT`.

```text
Request ContactQuery (request_id != 0)
    u16 operation = 8
    u16 reserved = 0
    u32 kind                      // 1 list, 2 exact phone lookup
    u32 max_records               // 1..64; exactly 1 for phone lookup
    u32 offset                    // 0..4096; zero for phone lookup
    u32 query_length              // zero for list, 1..256 for phone lookup
    u8  query[query_length]

Complete ContactReply (matching request_id)
    u16 operation = 8
    u16 reserved = 0
    u32 status
    u32 kind
    u32 next_offset               // zero when complete
    u32 record_count              // 0..64; at most 1 for phone lookup
    u8  bounded_contact_records[...]

Event ContactChanged (request_id = 0, payload size 4)
    u16 event = 7
    u16 reserved = 0
```

Contact records contain a stable QtContacts ID, display name, optional first
phone number, and a bounded avatar byte view. The Sailfish implementation does
not currently export avatar bytes. A complete list is staged before the Room
projection is reconciled; unavailable, failed, cancelled, malformed, or
source-invalidated reads preserve the last complete projection. Exact phone
lookup is also used to enrich call events whose voice-call source provides
only a line ID.

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
    u32 command                 // 1 = dismiss, 2 = open authenticated conversation
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
helper. Open is available only for an active, authenticated messaging
notification for which the helper separately retained conversation authority.
That authority requires both the narrowly validated reply target described
below and the literal `default` key in the notification action list. The
default action is only an intent gate: its label is ignored, while its dynamic
remote-action hint is not admitted, retained, decoded, or executed.

Open always issues the fixed empty-reply D-Bus call
`org.sailfishos.Messages` `/` `org.sailfishos.Messages`
`startConversation(accountPath, recipient)` with signature `ss`. Its two
arguments come only from the already authenticated, bounded reply target; no
notification-provided open destination, path, interface, member, signature,
or argument list is executable. Immediately before dispatch, the helper
resolves the current unique owner of `org.nemomobile.CommHistory` and requires
it to equal both the monitored owner and the target's source owner.

Conversation authority is retained separately from one-shot reply authority:
reply consumption does not consume Open, and opening does not consume Reply.
Notification replacement/removal, active-ID eviction, notification-service
generation loss, CommHistory owner change, and bus disconnect/reconnect revoke
both authorities. Open remains gated by both Notifications and Messaging at
backend admission, controller dispatch, native-loader dispatch, and proxy
queueing, so either domain loss prevents a dequeued command from crossing into
a replacement generation. Dismiss reports success only after a bounded reply
from the fixed notification service; Open likewise requires a bounded empty
method reply from the fixed Messages service. A missing, rejected, or timed-out
call fails safely.

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
