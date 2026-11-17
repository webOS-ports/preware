/*global enyo, preware, $L */
/*jslint sloppy: true */

//TODO: extract the package filtering logic into a "PackageFilter" singleton.
// => best would be to use bindings to fill the repeaters
// => connect this to the cookie-pref about installed <=> available apps, too.

enyo.kind({
    name: "preware.PackagesMenu",
    kind: "Scroller",
    horizontal: "hidden",
    classes: "enyo-fill",
    style: "background-image:url('assets/bg.png')",
    touch: true,
    fit: true,
    events: {
        onSelected: ""
    },

    packageFilters: {//filter for all = 0, available (i.e. not installed) = 1, only installed = 2, only updatable = 3, saved list = 4
        all: 0,
        available: 1,
        installed: 2,
        updatable: 3,
        saved: 4
    },
    currentPackageFilter: -1,
    //set only while showing the type+category filtered list under "Available Packages".
    currentFilterType: null,
    currentFilterCategory: null,
    currentFilterFeed: null,
    //"alpha" or "date": how the package list shown is sorted (the toggle under it).
    currentSort: "date",

    //public components
    published: {
        availablePackages: [],
        listOfEverything: []
    },

    handlers: {
        ontap: "itemTapped"
    },
    components: [
        {name: "updatesItem", kind: "ListItem", classes: "preware-menu-first", title: $L("Package Updates"), ontap: "showUpdatablePackages" },
        {name: "availableItem", kind: "ListItem", title: $L("Available Packages"), ontap: "showAvailableTypeList" },
        {name: "installedItem", kind: "ListItem", title: $L("Installed Packages"), ontap: "showInstalledPackages" },
        {name: "listOfEverythingItem", kind: "ListItem", title: $L("List of Everything"), ontap: "showListOfEverything" }
    ],
    
    listOfEverythingChanged: function (oldList, newList) {
    	// TODO: refactor from item tap handlers to here
    	this.$.listOfEverythingItem.set("count", newList.length);
    	
    	this.$.installedItem.set("count", newList.reduce(function (accumulatedValue, pkg) {
    		return accumulatedValue + (pkg.isInstalled ? 1 : 0);
    	}, 0));
    	
    	if (preware.PrefCookie.get().listInstalled) { //include installed packages.
    		this.$.availableItem.set("count", newList.length);
    	} else {
    		this.$.availableItem.set("count", newList.length - this.$.installedItem.get("count"));    		
    	}
    	
    	this.$.updatesItem.set("count", newList.reduce(function (accumulatedValue, pkg) {
    		return accumulatedValue + (pkg.hasUpdate ? 1 : 0);
    	}, 0));
    },

    //handlers:
    //keep the item whose list is open highlighted.
    itemTapped: function (inSender, inEvent) {
        var c = inEvent.originator;
        while (c && c.owner !== this) {
            c = c.owner;
        }
        if (c && c.kindName === "ListItem") {
            this.setSelectedItem(c);
        }
    },
    setSelectedItem: function (item) {
        var names = ["updatesItem", "availableItem", "installedItem", "listOfEverythingItem"], i;
        for (i = 0; i < names.length; i += 1) {
            this.$[names[i]].addRemoveClass("list-item-active", this.$[names[i]] === item);
        }
    },
    clearSelection: function () {
        this.setSelectedItem(null);
    },
    showUpdatablePackages: function () {
        this.currentPackageFilter = this.packageFilters.updatable;
        this.currentFilterType = null;
        this.currentFilterCategory = null;
        this.currentFilterFeed = null;
        this.currentSort = this.defaultSort("updatable");
        this.recomputePackageList();

        this.doSelected({name: "updatable", packagesLength: this.availablePackages.length});
    },
    //Available Packages: AppPanels shows the group lists (Types / Feeds /
    //Categories, see groupCounts), like the original Preware.
    showAvailableTypeList: function () {
        this.currentPackageFilter = this.packageFilters.available;
        this.currentFilterType = null;
        this.currentFilterCategory = null;
        this.currentFilterFeed = null;
        this.doSelected({name: "available"});
    },
    showInstalledPackages: function () {
        this.currentPackageFilter = this.packageFilters.installed;
        this.currentFilterType = null;
        this.currentFilterCategory = null;
        this.currentFilterFeed = null;
        this.currentSort = this.defaultSort("installed");
        this.recomputePackageList();

        this.doSelected({name: "installed", packagesLength: this.availablePackages.length});
    },
    showListOfEverything: function () {
        this.currentPackageFilter = this.packageFilters.all;
        this.currentFilterType = null;
        this.currentFilterCategory = null;
        this.currentFilterFeed = null;
        this.currentSort = this.defaultSort("all");
        this.recomputePackageList();

        this.doSelected({name: "all", packagesLength: this.availablePackages.length});
    },
    //opened from the swipe-down menu, not from a ListItem here: see App.js/AppPanels.js.
    showSavedPackages: function () {
        this.currentPackageFilter = this.packageFilters.saved;
        this.currentFilterType = null;
        this.currentFilterCategory = null;
        this.currentFilterFeed = null;
        this.currentSort = this.defaultSort("saved");
        this.recomputePackageList();

        this.doSelected({name: "saved", packagesLength: this.availablePackages.length});
    },


    //public functions:
    //the packages of Available Packages chosen in the group lists: filter has
    //type, category (none for the "All" category) and maybe feed. Like the
    //original, newest first for "All" or a feed's packages, else by title.
    filterPackages: function (filter) {
        this.currentFilterType = filter.type || null;
        this.currentFilterCategory = filter.category || null;
        this.currentFilterFeed = filter.feed || null;
        this.currentSort = this.defaultSort((filter.allCategories || filter.feed) ? "all" : "filtered");
        this.recomputePackageList();
        this.doSelected({name: "filtered", packagesLength: this.availablePackages.length});
    },
    //a group list of Available Packages: the types, feeds or categories (kind)
    //of the packages that match filter ({type, feed, category}, each optional),
    //with how many there are, by name. A package can be in several feeds.
    groupCounts: function (kind, filter) {
        var counts = {}, out = [], name, field = {types: "type", feeds: "feed", categories: "category"}[kind];
        preware.PackagesModel.packages.forEach(function (pkg) {
            if (!this.checkPackageStatus(pkg) ||
                    (field !== "type" && filter.type && pkg.type !== filter.type) ||
                    (field !== "category" && filter.category && pkg.category !== filter.category) ||
                    (field !== "feed" && filter.feed && !pkg.inFeed(filter.feed))) {
                return;
            }
            (field === "feed" ? pkg.feeds : [pkg[field]]).forEach(function (n) {
                counts[n] = (counts[n] || 0) + 1;
            });
        }, this);
        for (name in counts) {
            if (counts.hasOwnProperty(name)) {
                out.push({name: name, count: counts[name]});
            }
        }
        out.sort(function (a, b) {
            return a.name.localeCompare(b.name);
        });
        //the categories start with "All", like the original's: all of them.
        if (field === "category" && out.length) {
            out.unshift({name: $L("All"), all: true, count: out.reduce(function (sum, c) {
                return sum + c.count;
            }, 0)});
        }
        return out;
    },
    checkPackageStatus: function (pkg) {
        if (!pkg) {
            return false;
        }
        if (this.currentPackageFilter === this.packageFilters.updatable) {
            return pkg.hasUpdate;
        }
        if (this.currentPackageFilter === this.packageFilters.installed) {
            return pkg.isInstalled;
        }
        if (this.currentPackageFilter === this.packageFilters.saved) {
            return pkg.isInSavedList;
        }

        if (this.currentPackageFilter === this.packageFilters.available) {
            if (preware.PrefCookie.get().listInstalled) { //include installed packages.
                return true;
            } else {
                return !pkg.isInstalled; //return only not installed packages.
            }
        }

        return true; //everything is fine.
    },
    //rebuilds availablePackages from the current filter (and, under "Available
    //Packages", the drilled-down type/category) without navigating anywhere.
    //Shared by the show*/filter* methods above and by refreshLists below, so a
    //package install/remove can bring an already-open list back in sync.
    recomputePackageList: function () {
        var i, pkg;
        this.availablePackages = [];
        for (i = 0; i < preware.PackagesModel.packages.length; i += 1) {
            pkg = preware.PackagesModel.packages[i];
            if (!this.checkPackageStatus(pkg)) {
                continue;
            }
            if (this.currentFilterType !== null && pkg.type !== this.currentFilterType) {
                continue;
            }
            if (this.currentFilterCategory !== null && pkg.category !== this.currentFilterCategory) {
                continue;
            }
            if (this.currentFilterFeed !== null && !pkg.inFeed(this.currentFilterFeed)) {
                continue;
            }
            if (this.availablePackages.indexOf(pkg) === -1) {
                this.availablePackages.push(pkg);
            }
        }
        this.sortPackageList();
        return this.availablePackages.length;
    },
    //true while availablePackages holds a package list (rather than the Types or
    //Categories drill-down, which don't show packages directly).
    isShowingPackageList: function () {
        return this.currentPackageFilter !== this.packageFilters.available ||
            (this.currentFilterType !== null && this.currentFilterCategory !== null);
    },
    //called after a package install/update/remove completes: the menu item counts
    //are always stale (they're only set from a full listOfEverything reload), and
    //the currently open package list, if any, was filtered at the time it opened.
    //Returns the refreshed length of the currently shown package list, or -1 if
    //none is currently shown.
    refreshLists: function () {
        this.listOfEverythingChanged(null, preware.PackagesModel.packages);
        if (this.currentPackageFilter === -1 || !this.isShowingPackageList()) {
            return -1;
        }
        return this.recomputePackageList();
    },
    getPackage: function (index) {
        if (index >= 0) {
            return this.availablePackages[index];
        } else {
            return undefined;
        }
    },


    //auxillary package handling stuff:
    //how a list is sorted when it opens: the "Default sort" preference, or with
    //"Category Default" what the original Preware does: by title for the installed
    //and saved packages and a category's packages, newest first for the others
    //(Package Updates, List of Everything, search results).
    defaultSort: function (listName) {
        var pref = preware.PrefCookie.get().listSort;
        if (pref === "alpha" || pref === "date") {
            return pref;
        }
        return (listName === "installed" || listName === "saved" || listName === "filtered") ? "alpha" : "date";
    },
    sortPackageList: function () {
        this.sortPackages(this.availablePackages, this.currentSort);
    },
    //sorts packages by title ("alpha") or newest first ("date", undated ones
    //last). The comparison has to be consistent, or Array.sort mixes the list up
    //(a comparison that said "first" for every undated package reversed it).
    sortPackages: function (packages, sort) {
        var title = this.sortTitle,
            byTitle = function (a, b) {
                var x = title(a), y = title(b);
                return x < y ? -1 : (x > y ? 1 : 0);
            },
            date = this.packageDate,
            compare = byTitle;

        if (sort === "date") {
            compare = function (a, b) {
                var x = date(a), y = date(b);
                if (x !== y) {
                    if (x === undefined) {
                        return 1;
                    }
                    if (y === undefined) {
                        return -1;
                    }
                    return y - x;
                }
                return byTitle(a, b);
            };
        }
        packages.sort(compare);
        return packages;
    },
    //some feeds have titles starting with a space (modernize)
    sortTitle: function (p) {
        return String(p.title || p.pkg || "").replace(/^\s+/, "").toLowerCase();
    },
    //last updated, in seconds, or undefined (0 means unknown too).
    packageDate: function (p) {
        var n = parseFloat(String(p.date).replace(/[^0-9.]/g, ""));
        return (isNaN(n) || !n) ? undefined : n;
    },
    //the divider above a package in a list sorted by sort (the original Preware's):
    //how long ago it was updated, or with letters the first letter of its title.
    //now: the time in seconds.
    sortDivider: function (p, sort, letters, now) {
        var d, age, c;
        if (sort === "date") {
            d = this.packageDate(p);
            if (d === undefined) {
                return $L("Unknown");
            }
            age = now - d;
            if (age <= 86400) {
                return $L("Today");
            }
            if (age <= 172800) {
                return $L("Yesterday");
            }
            if (age <= 604800) {
                return $L("This Week");
            }
            if (age <= 1209600) {
                return $L("Last Week");
            }
            if (age <= 2629744) {
                return $L("This Month");
            }
            if (age <= 5259488) {
                return $L("Last Month");
            }
            return $L("Older");
        }
        if (letters) {
            c = this.sortTitle(p).charAt(0).toUpperCase();
            return (c >= "A" && c <= "Z") ? c : "#";
        }
        return "";
    }
});
