# Rockpool, a Sailfish port of Rockwork on Ubuntu, which is a port of Pebbled for Sailfish

[![Join the chat at https://gitter.im/abranson/rockpool](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/abranson/rockpool?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

[TMO thread](http://talk.maemo.org/showthread.php?t=96490) [Openrepos](https://openrepos.net/content/abranson/rockpool)

This is in an alpha state, the daemon to communicate with the watch is mostly full featured, but the user interface is in a preliminary state. It's the only way to get support in Sailfish for the Pebble v3 firmware (i.e. Pebble Time, Time Steel, Round and updated older models). For Pebble classic models, notification will work but there's no support for the older app model, so you should continue to use Pebbled. You can use Rockpool to update your watch to the latest firmware if you dare, using DBus.

## What works

* The daemon runs and connects to a paired pebble. It can:
    * Forward notifications to the watch, they can be opened or dismissed from there. New notification types in the pebble v3 are used: Hangouts for Hangish, Telegram for Sailorgram and the android client, Whatsapp for the android client. Need more applications to add here.
    * Send music info to the watch, including track duration and progress so you get the progress bar. It can pass returning track and volume controls back.
    * Calendar entries with reminders are added to the timeline on the watch, automatically refreshed when the phone's calendar is updated. Accessing the calendar on Sailfish requires some specific Sailfish permission stuff, and won't work when launched from QtCreator, it must be running as a service. Start it manually with 'systemctl --user start rockpoold' from a root prompt.
    * Show incoming call notifications, rejecting, starting and stopping calls. Ending calls currently crashes the daemon.
    * App fetching works, but without a UI you have to put them in the proper place yourself. Each one should be unzipped to a subdirectory named  after the UUID of the app (from the filename) under /home/nemo/.local/share/rockpoold/<bluetooth addr>/apps/, which will be created when you connect to your watch. After this you'll see all the apps on the pebble, and launching them will download them from your phone.

## What doesn't

* The UI is still in development. There are a few things you can't do with it, like request screenshots or upgrade the watch firmware. In the meantime you can communicate using DBus like so:

    qdbus org.rockwork /org/rockwork/B0_B4_48_80_B9_87 org.rockwork.Pebble.SetCalendarSyncEnabled true

* Stability is an issue - RockPool may crash on you, though the daemon will restart.

## Building

* The project should build in Qt Creator with no specific changes, but it requires the 'quazip-devel' library to be installed on the build VM because Pkgconfig doesn't work for that.

## The plan

I've started proposing the libpebble changes back into RockWork, When it's ready, we'll merge the Sailfish-specific QML, platform integration and any other tweaks back into the RockWork tree.

## The thanks

* Ruslan N. Marchenko - Sailfish UI
* Javispedro - Contributor to Pebbled, author of Saltoq and libwatchfish.
* Michael Zanetti - Author of RockWork
* Tomasz Sterna - Author of Pebbled
* Brian Douglass - RockWork contributor
* Katharine Berry - Pebble authority
* Robert Meijers, Philipp Andreas - Hints and tips
