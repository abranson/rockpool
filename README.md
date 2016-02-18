# Rockpool, a Sailfish port of Rockwork on Ubuntu, which is a port of Pebbled for Sailfish

This is in a very early state, and I'm sharing it in case someone wants to join in, because there's not a lot of v3 support for Sailfish right now. Not much works yet. It currently builds into an rpm which installs the daemon and client application, though please don't distribute that until it's ready.

## What works

* The daemon runs and connects to a paired pebble. It can:
    * Forward notifications to the watch, and invoke the associated action when 'open on phone' is selected on the watch.
    * Send music info to the watch, and pass returning track and volume controls back.
    * Show incoming call notifications, rejecting, starting and stopping calls. Ending calls currently crashes the daemon.
    * The service is installed, and may be working, but it's more useful to launch it from Qt Creator at this point.
    * App fetching works, but without a UI you have to put them in the proper place yourself. Each one should be unzipped to a subdirectory named  after the UUID of the app (from the filename) under /home/nemo/.local/share/rockpoold/<bluetooth addr>/apps/, which will be created when you connect to your watch. After this you'll see all the apps on the pebble, and launching them will download them from your phone.

## What doesn't

* The app is still the Ubuntu QML and needs porting to Silica, so will just give you a blank screen. This means you can't install or configure apps. Some migration of the C++ UI code is done. I moved the qml out of the binary and into the data directory so they can be edited and patched on device.
* The OrganizerAdaptor, which uses the Qt Organizer API to fetch appointments out of your calendar and insert them into the timeline, doesn't find any events. From looking at Fahrplan, it seems that Sailfish doesn't support that API, and we have to use a different mechanism to access the SQLite file directly. I put the discovery code in the initialization, but it doesn't work yet.

## The plan

The plan for this is to get it working, simplify the impact, then merge the Sailfish-specific QML, platform integration and any other tweaks back into the RockWork tree.

