# Rockpool, a Sailfish port of Rockwork on Ubuntu, which is a port of Pebbled for Sailfish

This is in a very early state, and I'm sharing it in case someone wants to join in, because there's not a lot of v3 support for Sailfish right now. Not much works yet. It currently builds into an rpm which installs the daemon and client application, though please don't distribute that until it's ready.

## What works

* The daemon runs and connects to a paired pebble. It can:
    * Forward notifications to the watch, and invoke the associated action when 'open on phone' is selected on the watch. New notification types in the pebble v3 are used: Hangouts for Hangish, Telegram for Sailorgram and the android client, Whatsapp for the android client. Need more applications to add here.
    * Send music info to the watch, and pass returning track and volume controls back. This now uses mpris-qt5, so should be able to supply track progress information later.
    * Calendar entries are added to the timeline on the watch. This requires some specific Sailfish permission stuff, and won't work when launched from QtCreator, it must be running as a service. Start it manually with 'systemctl --user start rockpoold' from a root prompt.
    * Show incoming call notifications, rejecting, starting and stopping calls. Ending calls currently crashes the daemon.
    * The service is installed, and may be working, but it's more useful to launch it from Qt Creator at this point.
    * App fetching works, but without a UI you have to put them in the proper place yourself. Each one should be unzipped to a subdirectory named  after the UUID of the app (from the filename) under /home/nemo/.local/share/rockpoold/<bluetooth addr>/apps/, which will be created when you connect to your watch. After this you'll see all the apps on the pebble, and launching them will download them from your phone.

## What doesn't

* The app is still the Ubuntu QML and needs porting to Silica, so will just give you a blank screen. This means you can't install or configure apps. Some migration of the C++ UI code is done. I moved the qml out of the binary and into the data directory so they can be edited and patched on device.
* Notifications should be moved to a new class based on the libwatchfish NotificationMonitor, recording the ids of the notifications so they can be dismissed.


## The plan

The plan for this is to get it working, simplify the impact, then merge the Sailfish-specific QML, platform integration and any other tweaks back into the RockWork tree.

