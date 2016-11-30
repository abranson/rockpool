# Rockpool, a Sailfish port of Rockwork on Ubuntu, which is a port of Pebbled for Sailfish

[![Join the chat at https://gitter.im/abranson/rockpool](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/abranson/rockpool?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

[TMO thread](http://talk.maemo.org/showthread.php?t=96490) [Openrepos](https://openrepos.net/content/abranson/rockpool)

This application supports the Pebble v3 firmware in Sailfish (i.e. Pebble Time, Time Steel, Round and updated older models). It is in a beta state. The app and daemon are mostly full featured, but the JavaScript watch app API is incomplete so not all apps work. For Pebble classic models, notification will work but there's no support for the older app model, so you should continue to use Pebbled. You can use Rockpool to update your watch to the latest firmware, and you should because it's a great update.

## Features

* Forwards notifications to the watch, they can be opened or dismissed from there. New notification types in the pebble v3 are used: Hangouts for Hangish, Telegram for Sailorgram and the android client, Whatsapp for the android client. Different notification types can be silenced either completely or only when the phone is unlocked.
* Calendar entries are added to the timeline on the watch, automatically refreshed when the phone's calendar is updated. If you have a reminder set on the entry before the event starts, you'll get one on your watch too.
* Send music info to the watch, including track duration and progress so you get the progress bar. You can pause, play, skip tracks and change the volume from your pebble.
* Show incoming call notifications, rejecting, starting and stopping calls. Ending calls currently crashes the daemon.
* You can manage watch apps, and browse the pebble store for new ones. If you previously added apps manually, you should remove them and add them from the store so you get the nice icon.
* Lots more: Profile switching when the watch is connected (e.g. silent). Take, manage and share watch screenshots. Update the time on your watch whenever the time or timezone changes on your phone.

## Known issues

* Stability - RockPool may crash on you, though the daemon will restart if it does.
* Some other apps don't work because the JSKit API is incomplete. Please tell me about these!

## Building

Rockpool requires some extra packages to be installed on the MerSDK to be able to build. Mainly this is to add missing QtWebSockets Qt plugin which is missing in Qt 5.2 in general and in Sailfish in particular.

You would need to perform following steps:

  * SSH to the MerSDK as mersdk user
  * Enter the scratchbox as root for package installation 
    * Phone: sb2 -t SailfishOS-armv7hl -R -m sdk-install
    * Tablet: sb2 -t SailfishOS-i486 -R -m sdk-install
  * Add my repo for QtWebSockets
    * zypper ar -f http://sailfish.openrepos.net/abranson/personal/main openrepos-abranson

After that, the package will build pulling necessary dependencies from official and newly added repositories. QtCreator will warn you that Qt websockets can't be found locally, but you can fix that by syncing the target in the 'SailfishOS' pane. To install your package on your phone or tablet, you'll need to add my openrepo. The easiest way to do this is with warehouse, though you'll have to do it manually if you're using the emulator.

## The thanks

* Ruslan N. Marchenko - Sailfish UI, Developer mode and much more
* Javispedro - Contributor to Pebbled, author of Saltoq and libwatchfish.
* Michael Zanetti - Author of RockWork
* Tomasz Sterna - Author of Pebbled
* Brian Douglass - RockWork contributor
* Katharine Berry - Pebble authority
* Robert Meijers, Philipp Andreas - Hints and tips
