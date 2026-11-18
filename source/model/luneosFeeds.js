/*jslint sloppy: true */
/*global enyo, preware, console */

// The default feeds on LuneOS.
//
// On legacy webOS, legacy/pmPostInstall.script writes the default feeds when
// Preware 2 is installed. LuneOS does not run install scripts of app packages,
// so there Preware 2 adds the same feeds itself, through the package manager
// service, the first time it starts. It shows only these, see
// IPKGService.ownConfigs: the ones it added (preware2-*.conf) and the feeds
// LuneOS ships that are on this list, which it uses instead of adding them
// again.
//
// Keep this list in step with legacy/pmPostInstall.script. What it leaves out:
// - the architecture specific second lines of optware.conf and
//   webos-internals.conf (optware-armv7, ...): those feeds are built for the
//   ARM CPUs of the legacy devices, and a feed added through the service holds
//   one line only.
// - webos-patches and webos-kernels: they are published per legacy webOS
//   version, and have no LuneOS version (the script disables them for any
//   version it does not know).
enyo.singleton({
    name: "preware.LuneOSFeeds",
    feeds: [
        {config: "optware.conf", name: "optware", url: "http://ipkg.preware.net/feeds/optware/all", gzip: true},
        {config: "precentral-weboslives.conf", name: "precentral", url: "http://weboslives.eu/feeds/precentral", gzip: true},
        {config: "precentral-themes.conf", name: "precentral-themes", url: "http://ipkg.preware.net/feeds/precentral-themes", gzip: true},
        {config: "webos-internals.conf", name: "webosinternals", url: "http://ipkg.preware.net/feeds/webos-internals/all", gzip: true},
        {config: "woce.conf", name: "woce", url: "http://ipkg.preware.net/feeds/woce", gzip: true},
        {config: "modernize.conf", name: "modernize", url: "http://stacks.webosarchive.org/feeds/modernize/ipkgs", gzip: true}
    ],

    //the default feed a feed URL belongs to (http or https), or undefined.
    defaultFeedFor: function (url) {
        var key = function (u) {
                return String(u).toLowerCase().replace(/^https?:\/\//, "").replace(/\/+$/, "");
            },
            i;
        for (i = 0; i < this.feeds.length; i += 1) {
            if (key(this.feeds[i].url) === key(url)) {
                return this.feeds[i];
            }
        }
    },

    // Sets the default feeds up, once. Calls back with true when it changed
    // any: they have not been downloaded yet.
    setUp: function (callback) {
        if (preware.Platform.isLegacy || preware.PrefCookie.get().luneosFeedsSetUp) {
            callback(false);
            return;
        }
        preware.IPKGService.list_configs(this.gotConfigs.bind(this, callback));
    },
    gotConfigs: function (callback, payload) {
        var existing = {}, todo = [], i, c;
        if (!payload || !payload.configs) {
            console.error("LuneOSFeeds: cannot list the feeds, trying again at the next start: " + JSON.stringify(payload));
            callback(false);
            return;
        }
        for (i = 0; i < payload.configs.length; i += 1) {
            existing[payload.configs[i].config] = payload.configs[i];
        }
        for (i = 0; i < this.feeds.length; i += 1) {
            c = existing[this.feeds[i].config];
            if (!c) {
                todo.push({feed: this.feeds[i], add: true});
            } else if (!c.enabled && !preware.IPKGService.isOwnConfig(c.config)) {
                // a feed LuneOS ships, which it has turned off: turn it on.
                // Feeds Preware 2 added before (the setting can be lost with the
                // cookies) are left as they are.
                todo.push({feed: this.feeds[i], add: false});
            }
        }
        this.setUpNext(todo, 0, true, callback);
    },
    setUpNext: function (todo, index, allDone, callback) {
        var self = this, step = todo[index], finished = false,
            next = function (done) {
                if (finished) {
                    return;
                }
                finished = true;
                self.setUpNext(todo, index + 1, allDone && done, callback);
            };

        if (!step) {
            if (allDone) {
                preware.PrefCookie.put("luneosFeedsSetUp", true);
            }
            callback(todo.length > 0);
            return;
        }

        if (step.add) {
            console.log("LuneOSFeeds: adding " + step.feed.config);
            preware.IPKGService.addConfig(function (payload) {
                if (!payload || payload.returnValue === false || payload.stage === "failed") {
                    console.error("LuneOSFeeds: could not add " + step.feed.config + ": " + JSON.stringify(payload));
                    next(false);
                } else if (payload.stage === "completed") {
                    next(true);
                }
            }, step.feed.config, step.feed.name, step.feed.url, step.feed.gzip);
        } else {
            console.log("LuneOSFeeds: turning on " + step.feed.config);
            preware.IPKGService.setConfigState(function (payload) {
                next(!!payload && payload.returnValue !== false);
            }, step.feed.config, true);
        }
    }
});
