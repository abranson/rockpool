# Rockpool, Pebble support for Sailfish

[TMO thread](http://talk.maemo.org/showthread.php?t=96490) [Openrepos](https://openrepos.net/content/abranson/rockpool)

Rockpool provides the Sailfish UI and platform integration for Pebble watches.
Since version 2.0, the watch daemon is the separately packaged `libpebble3d`
service; Rockpool communicates with it through the `org.rockpool` D-Bus API.

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

Version 2.0 is built as separate daemon and Sailfish integration packages:

* `libpebble3d` supplies the Native Image daemon, JNI loader, and
  `libpebble3d.service`.
* This repository's RPM spec supplies the Rockpool Silica UI, the platform ABI
  development package, and the Sailfish platform provider.

Build the Sailfish UI and provider with the normal Sailfish SDK or OBS workflow;
for a configured AArch64 target, for example:

```
mb2 -t aarch64 --no-vcs-apply build
```

Create a matching `libpebble3d` daemon RPM separately, then install both it
and `libpebble3d-platform-sailfish` with the `rockpool` UI RPM. See
[`libpebble3d/README.md`](libpebble3d/README.md) for the daemon release and
packaging instructions.

## The thanks

* Ruslan N. Marchenko - Sailfish UI, Developer mode and much more
* Javispedro - Contributor to Pebbled, author of Saltoq and libwatchfish.
* Michael Zanetti - Author of RockWork
* Tomasz Sterna - Author of Pebbled
* Brian Douglass - RockWork contributor
* Katharine Berry - Pebble authority
* Robert Meijers, Philipp Andreas - Hints and tips
* Christopher Frost, Kristjan Räts, István Hovai, Allan Nordhøy, Omar Anwar Aglan, J Li, Nathan Follens, Olexandr Nesterenko, ssantos, Jakob Jespersen - Translators
