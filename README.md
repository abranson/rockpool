# Rockpool, Pebble support for Sailfish

[TMO thread](http://talk.maemo.org/showthread.php?t=96490) [Openrepos](https://openrepos.net/content/abranson/rockpool)

Rockpool provides the Sailfish UI and platform integration for Pebble watches.
Since version 2.0, the watch daemon is the separately packaged `libpebble3d`
service; Rockpool communicates with it through the `org.rockpool` D-Bus API.

## Features

* Forwards notifications to the watch, they can be opened or dismissed from there. New notification types in the pebble v3 are used: Hangouts for Hangish, Telegram for Sailorgram and the android client, Whatsapp for the android client. Different notification types can be silenced either completely or only when the phone is unlocked.
* Calendar entries are added to the timeline on the watch, automatically refreshed when the phone's calendar is updated. If you have a reminder set on the entry before the event starts, you'll get one on your watch too.
* Send music info to the watch, including track duration and progress so you get the progress bar. You can pause, play, skip tracks and change the volume from your pebble.
* Show incoming call notifications, rejecting, starting and stopping calls. Ending calls currently crashes the daemon.
* You can manage watch apps, and browse the pebble store for new ones. If you previously added apps manually, you should remove them and add them from the store so you get the nice icon.
* Lots more: Profile switching when the watch is connected (e.g. silent). Take, manage and share watch screenshots. Update the time on your watch whenever the time or timezone changes on your phone.

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
