Preware 2
=======
Preware 2 is the on-device homebrew installer for LuneOS and legacy webOS
(Palm/HP webOS 2.x/3.x). One package works on both.

Its app id is `com.palm.app.preware2` on both (it was `org.webosports.app.preware`):
legacy webOS only lets apps in the `com.palm` namespace load the files of other apps,
which Preware needs to show the icons of installed apps. The original Mojo Preware
keeps `org.webosinternals.preware`, so either or both can be installed.

The default feeds are enabled at the first start, and other feeds
can be enabled or added by selecting `Manage Feeds` from
the app menu.  Downloaded packages can be installed by
selection `Install Package` from the app menu.

Building/Installation
-------
It runs on LuneOS and legacy webOS devices only
(no mocking is set up to develop in the browser).

To rebuild and install on a LuneOS device attached via USB, run this command in the app directory:
`./build-and-deploy.sh`
Then, in Chrome, surf to `localhost:1122` to debug.


Legacy webOS (Palm/HP webOS 2.x/3.x)
-------
Preware 2 also runs on legacy webOS; it was developed against an HP TouchPad on webOS 3.0.5.
There it uses the package manager service of the original Preware
(`org.webosinternals.ipkgservice`, bundled in `legacy/`) and the original Preware's
default feeds, instead of `org.webosports.service.ipkg`. `preware.Platform`
(`source/model/platform.js`) decides which one to use from the user agent.

To build the package for both, run `./build-legacy.sh`. It produces
`bin/com.palm.app.preware2_<version>_arm.ipk`, which can be installed with `palm-install`, WebOS Quick Install or the original Preware.
The device must be in developer mode.

On install, `legacy/pmPostInstall.script` runs as root and
- installs the package manager service, unless an identical copy is already installed
  (it is the same service the original Preware installs, so the two can be installed side by side),
- writes the original Preware's default feed configuration to `/media/cryptofs/apps/etc/ipkg`,
  keeping feeds that were disabled disabled.

`legacy/pmPreRemove.script` removes the service again, unless the original Preware is still installed.
Removing the original Preware also removes the shared service. The `preware2-service-check` upstart job
installed by Preware 2 puts it back at the next boot (`legacy/bin/install-service.sh`).

At the first launch (and every launch while "Check .ipk association" is on in the preferences)
Preware 2 offers to register itself as the application that opens `.ipk` files
(`source/model/resourceHandler.js`, like the original Preware's resourceHandler.js). It then handles
launches with `{target: <ipk>}` or `{type: "install", file: <ipk>}` by showing the package's info.

On LuneOS, which does not run the install scripts of app packages, Preware 2 adds the same default feeds
itself at its first start (`source/model/luneosFeeds.js`), through `org.webosports.service.ipkg`. It keeps them
in files named `preware2-*.conf`, and shows only its default feeds and the feeds added in Preware 2, not the
other feeds LuneOS ships. A default feed LuneOS already ships is used, and turned on, instead of being added a
second time.

On both, `preware.ColumnPanels` (`source/ColumnPanels.js`) keeps the menu on the left and opens the other panels
in columns to its right (in landscape the list stays next to the package details); on a phone it shows one
panel at a time. There are no sliding or zooming transitions between panels.

Differences from LuneOS:
- 3D acceleration is turned off, the TouchPad's WebKit (534.6) often did not paint the package list panel.
- The legacy service can stop answering feed downloads when two updates run at once, so only one update runs
  at a time, and a feed that is silent for a minute counts as failed and the service is restarted.
- The toolbar grabber is always shown and tapping it goes back one panel, because the TouchPad has no gesture area.
- Up to 6 feeds are downloaded at the same time (service API 18 or later), like the original Preware.

`./build-legacy.sh --debug` additionally bundles `debug/DebugHook.js`: relaunching the app with
`{"pw2eval": "<js>"}` evaluates the JavaScript in the app and writes the result to the system log.
Never ship such a build.

To-do [out of date]
-----

First step is to convert needed stuff from preware/app/model to enyo. The status is as follows:

Working:
- prefs cookie should be working (tested in browser)
- basic IPKGService communictation (version, device id, tested on TouchPad)

Implemented (but untested):
- more complex IPKGService communication
- deviceProfile.js
- feeds.js
- palmProfile.js
- help.js

only Partly-Implemented:
- db8storage (used for just type)
- packageModel.js (renamed from package.js)
- filePicker.js (does some Mojo stuff to display stuff.. need to replace that)

not implemented at all:
- packages.js
- resourceHandler.js
- stayAwake.js

UI TO-DOs
- if a Repeater has no items, display a message in the blank space
