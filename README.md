# Rockpool, a Sailfish port of Rockwork on Ubuntu, which is a port of Pebbled for Sailfish

[![Join the chat at https://gitter.im/abranson/rockpool](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/abranson/rockpool?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

[TMO thread](http://talk.maemo.org/showthread.php?t=96490) [Openrepos](https://openrepos.net/content/abranson/rockpool)

This application supports the Pebble v3 firmware in Sailfish (i.e. Pebble Time, Time Steel, Round and updated older models). It is in a beta state. The app and daemon are mostly full featured, but the Javascript watch app API is incomplete so not all apps work. For Pebble classic models, notification will work but there's no support for the older app model, so you should continue to use Pebbled. You can use Rockpool to update your watch to the latest firmware, and you should because it's a great update.

## Features

* Forwards notifications to the watch, they can be opened or dismissed from there. New notification types in the pebble v3 are used: Hangouts for Hangish, Telegram for Sailorgram and the android client, Whatsapp for the android client.
* Send music info to the watch, including track duration and progress so you get the progress bar. You can pause, play, skip tracks and change the volume from your pebble.
* Calendar entries with reminders are added to the timeline on the watch, automatically refreshed when the phone's calendar is updated.
* Show incoming call notifications, rejecting, starting and stopping calls. Ending calls currently crashes the daemon.
* You can manage watch apps, and browse the pebble store for new ones. If you previously added apps manually, you should remove them and add them from the store so you get the nice icon etc.

## Known issues

* Stability - RockPool may crash on you, though the daemon will restart.
* Some app configuration pages don't display well because QtWebKit isn't very good.
* Some other apps don't work because the JSKit API is incomplete. Please tell me about these!

## Building

* The project should build in Qt Creator with no specific changes, but it requires the 'quazip-devel' library to be installed on the build VM because Pkgconfig doesn't work for that.

* To build with Gecko engine it's required to install qtmozembed-qt5-devel on MerSDK. That requires manual intervention as well, because 
	* The repository containing the package is missing in default MerSDK VM
	* The prerequisite packages (dependencies) are hooked to the different versions, hence it raises a conflict which would need to be manually resolved

	So you would need to perform following steps:

	* SSH to the MerSDK as mersdk user
	* enter the scratchbox as root for package installation (sb2 -t SailfishOS-armv7hl -R -m sdk-install)
	* add mer-core repository (zypper ar -f http://repo.merproject.org/obs/mer-core:/armv7hl:/devel/Core_armv7hl/ mer-core)
	* install required package and explicit conflicting down-chain (zypper install xulrunner-qt5 qtmozembed-qt5 qtmozembed-qt5-devel)
	* When zypper complains about conflicts - chose option 3 (proceed with broken dependencies)

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
