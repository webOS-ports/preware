/*jslint sloppy: true, continue:true */
/*global enyo, window, device, console, preware, $L, setTimeout, UpdateFeeds, prewareGroupPanel */

//a column of Available Packages' group lists (level 1-3). The toggle in its
//bottom bar picks what it lists (Feeds / Types, Categories / Feeds).
function prewareGroupPanel(level) {
    return {
        name: "GroupPanels" + level,
        kind: "Panels",
        arrangerKind: "CardArranger",
        draggable: false,
        style: "width: 33.3%;",
        showing: false,
        components: [
            {kind: "EmptyPanel"},
            {kind: "FittableRows", components: [
                {kind: "onyx.Toolbar", components: [
                    {name: "GroupTitle" + level, style: "display: inline-block; position: absolute;", content: ""}
                ]},
                {name: "GroupScroller" + level, kind: "Scroller",
                    horizontal: "hidden",
                    classes: "enyo-fill",
                    style: "background-image:url('assets/bg.png')",
                    touch: true,
                    fit: true,
                    components: [
                        {name: "GroupRepeater" + level, kind: "Repeater", groupLevel: level, onSetupItem: "setupGroupItem", count: 0, components: [
                            {kind: "ListItem", title: "[group]", groupLevel: level, ontap: "groupTapped"}
                        ]}
                    ]},
                {kind: "GrabberToolbar", components: [
                    {name: "GroupToggle" + level, kind: "onyx.RadioGroup", classes: "preware-sort-toggle", groupLevel: level, onActivate: "groupToggleTapped", components: [
                        {content: "", value: ""},
                        {content: "", value: ""}
                    ]}
                ]}
            ]}
        ]
    };
}

enyo.kind({
    name: "AppPanels",
    //menu always visible on the left, other panels open to its right; on a phone
    //one panel at a time. No sliding or zooming transitions (see ColumnPanels).
    kind: "preware.ColumnPanels",
    peekWidth: 70,   // (600-320)/4
    classes: "app-panels enyo-fill",
    // required ipkgservice
    ipkgServiceVersion: 14,
    //Available Packages: the group lists shown, like the original Preware's
    //(see startGroups), or null.
    groups: null,

    menuPanelsIndex: 0,
    groupPanelsIndex: 1, //the first of the three group columns
    groupPanelsCount: 3,
    packagePanelsIndex: 4,
    packageDisplayPanelsIndex: 5,

    components: [
        {
            kind: "Signals",
            onbackbutton: "handleBackGesture",
            onPackagesStatusUpdate: "processStatusUpdate",
            onUpdateFeedsFinished: "doneLoading",
            onListSortChanged: "listSortChanged",
            onReloadPackages: "reloadPackages",
            onListInstalledChanged: "listInstalledChanged",
            onPackageRefresh: "handlePackageRefresh",
            onUpdateAllPackage: "showUpdateAllPackage",
            onMultiInstallFinished: "multiInstallFinished",
            onShowConnectedPackages: "showConnectedPackages",
            ondeviceready: "handleDeviceReady"
        },

        //Menu
        {
            name: "MenuPanel",
            layoutKind: "FittableRowsLayout",
            style: "width: 33.3%",
            components: [
                {
                    name: "ScrollerPanel",
                    //legacy webOS: menu always visible on the left, other panels open to its right.
                    kind: "Panels",
                    arrangerKind: "CardArranger",
                    fit: true,
                    draggable: false,
                    components: [
                        { //boot-messages:
                            style: "width: 100%; height: 100%; background-image: url('assets/bg.png');",
                            components: [
                                {
                                    classes: "onyx-toolbar preware-status-box",
                                    components: [
                                        {kind: "onyx.Spinner"},
                                        {
                                            name: "SpinnerText",
                                            classes: "preware-status-text",
                                            allowHtml: true
                                        }
                                    ]
                                }
                            ]
                        },
                        //bubble doSettings and doManageFeeds events to parent.
                        {kind: "preware.PackagesMenu", name: "packagesMenu", onSelected: "packagesMenuSelected"}
                    ]
                },
                {kind: "onyx.Toolbar"}
            ]
        },

        //Available Packages: the group lists (types, feeds, categories), one
        //column each, see startGroups.
        prewareGroupPanel(1),
        prewareGroupPanel(2),
        prewareGroupPanel(3),

        //Packages
        {
            name: "PackagePanels",
            //legacy webOS: menu always visible on the left, other panels open to its right.
            kind: "Panels",
            arrangerKind: "CardArranger",
            draggable: false,
            style: "width: 33.3%;",
            components: [
                {kind: "EmptyPanel"},
                {
                    kind: "FittableRows",
                    components: [
                        {kind: "onyx.Toolbar", components: [
                            {style: "display: inline-block; position: absolute;", content: "Packages"}
                        ]},
                        {
                            name: "PackageScroller",
                            kind: "Scroller",
                            onScroll: "packageListScrolled",
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "NoPackages", showing: false, style: "color: white; text-align: center; padding: 24px;", content: $L("No packages")},
                                {name: "PackageRepeater", kind: "preware.ChunkedRepeater", onSetupItem: "setupPackageItem", count: 0, components: [
                                    {kind: "ListItem", title: "[package]", icon: true, ontap: "packageTapped"}
                                ]}
                            ]
                        },
                        {kind: "GrabberToolbar", components: [
                            //Package Updates list only
                            {name: "UpdateAllButton", kind: "onyx.Button", classes: "onyx-affirmative", showing: false, content: $L("Update All"), ontap: "updateAllTapped"},
                            {name: "ListSort", kind: "onyx.RadioGroup", classes: "preware-sort-toggle", onActivate: "listSortTapped", components: [
                                {content: $L("A-Z"), value: "alpha"},
                                {content: $L("Date"), value: "date"}
                            ]}
                        ]}
                    ]
                },
                //search results: in this column, so the menu stays visible next to them.
                {
                    kind: "FittableRows",
                    components: [
                        {kind: "onyx.Toolbar", components: [
                            {style: "display: inline-block; position: absolute;", content: $L("Search Results")}
                        ]},
                        {
                            name: "SearchScroller",
                            kind: "Scroller",
                            onScroll: "searchListScrolled",
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "NoSearchResults", showing: false, style: "color: white; text-align: center; padding: 24px;", content: $L("No packages found")},
                                {name: "SearchRepeater", kind: "preware.ChunkedRepeater", onSetupItem: "setupSearchItem", count: 0, components: [
                                    {kind: "ListItem", title: "[package]", icon: true, ontap: "searchResultTapped"}
                                ]}
                            ]
                        },
                        {kind: "GrabberToolbar", components: [
                            {name: "SearchSort", kind: "onyx.RadioGroup", classes: "preware-sort-toggle", onActivate: "searchSortTapped", components: [
                                {content: $L("A-Z"), value: "alpha"},
                                {content: $L("Date"), value: "date"}
                            ]}
                        ]}
                    ]
                },
                //"View Them" from the question about a package's dependencies (or
                //the packages that depend on it): the package and those packages,
                //like the original Preware's Connected Packages scene.
                {
                    kind: "FittableRows",
                    components: [
                        {kind: "onyx.Toolbar", components: [
                            {name: "ConnectedTitle", classes: "preware-toolbar-title", content: ""}
                        ]},
                        {
                            name: "ConnectedScroller",
                            kind: "Scroller",
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "ConnectedRepeater", kind: "Repeater", onSetupItem: "setupConnectedItem", count: 0, components: [
                                    {kind: "ListItem", title: "[package]", icon: true, ontap: "connectedTapped"}
                                ]}
                            ]
                        },
                        {kind: "GrabberToolbar", components: [
                            {name: "ConnectedInstallButton", kind: "onyx.Button", classes: "onyx-affirmative", content: $L("Install / Update All"), ontap: "connectedInstallTapped"}
                        ]}
                    ]
                }
            ]
        },

        //Package Display
        {
            name: "PackageDisplayPanels",
            //legacy webOS: menu always visible on the left, other panels open to its right.
            kind: "Panels",
            arrangerKind: "CardArranger",
            draggable: false,
            style: "width: 33.3%;",
            components: [
                {kind: "EmptyPanel"},
                {kind: "preware.PackageDisplay", name: "packageDisplay"} //also contains simple message and porgress message for now.
            ]
        }
    ],


    //Handlers
    create: function (inSender, inEvent) {
        this.inherited(arguments);
        //switch the cards inside the columns directly: an animation still running
        //while ColumnPanels resizes a column left the old card visible next to the new one.
        [this.$.ScrollerPanel, this.$.GroupPanels1, this.$.GroupPanels2, this.$.GroupPanels3,
            this.$.PackagePanels, this.$.PackageDisplayPanels].forEach(function (p) {
            p.setAnimate(false);
        });
        setTimeout(this.handleDeviceReady.bind(this), 500);
        this.fired = false;
    },
    handleDeviceReady: function (inSender, inEvent) {
        if (!this.fired) {
            this.fired = true;

            //This appears to be our first opportunity to evaluate launch parameters, but they must be handled on the owner
            var launchParams = null;
            try {
                launchParams = JSON.parse(PalmSystem.launchParams || "{}");
            } catch (e) {
                this.log("Could not parse launch params: " + PalmSystem.launchParams);
            }
            var file = preware.ResourceHandler.launchFile(launchParams);
            if (file) {
                //launched just to install a package (e.g. from an app catalog): only do that,
                //no feed update. Closing the install dialog closes the app (App.installDialogHidden).
                enyo.warn("Preware was launched with a request to install an app: " + file);
                this.launchedForInstall = true;
                this.$.spinner.hide();
                this.$.SpinnerText.setContent("");
                enyo.Signals.send("onLaunchedWithInstallRequest", { params: file });
            } else {
                UpdateFeeds.startUpdateFeeds();
            }
        }
    },
    indexChanged: function () {
        this.inherited(arguments);
        if (this.index === this.menuPanelsIndex) {
            this.$.packagesMenu.clearSelection();
        }
    },
    //ColumnPanels: hide the panels that are not on the way to the current one.
    isPanelSkipped: function (index) {
        var level = index - this.groupPanelsIndex;
        if (level >= 0 && level < this.groupPanelsCount) {
            return !this.groups || level >= this.groups.length || this.isSearching();
        }
        return false;
    },
    handleBackGesture: function (inSender, inEvent) {
        var index = this.getIndex();
        if (this.isShowingConnected()) {
            if (index === this.packageDisplayPanelsIndex && !this.showsColumnBefore(index)) {
                this.setIndex(this.packagePanelsIndex); //phone: back to the list.
            } else {
                this.closeConnected();
            }
        } else if (this.isSearching() && index === this.packageDisplayPanelsIndex) { //back to the search results.
            this.setIndex(this.packagePanelsIndex);
        } else if (this.isSearching()) { //leave the search, back to where it started.
            this.endSearch();
        } else { //the panel before, past the group columns not in use.
            index = this.previousPanel(index);
            this.setIndex(index > 0 ? index : this.menuPanelsIndex);
        }
        inEvent.preventDefault();
    },
    doReloadList: function () {
        this.launchedForInstall = false;
        //the progress shows in the first panel: go there (on a phone the other
        //panels cover it).
        this.setIndex(this.menuPanelsIndex);
        if (UpdateFeeds.isUpdating()) {
            return; //already loading.
        }
        this.searchHeader.set("disabled", true);
        this.cancelSearch();
        this.$.spinner.show();
        UpdateFeeds.startUpdateFeeds(true);
        this.$.ScrollerPanel.setIndex(0);
    },
    //load the downloaded package lists again, without downloading (a preference
    //that filters packages while they are loaded changed).
    reloadPackages: function () {
        if (UpdateFeeds.isUpdating()) {
            return;
        }
        this.setIndex(this.menuPanelsIndex);
        this.searchHeader.set("disabled", true);
        this.cancelSearch();
        this.$.spinner.show();
        this.$.ScrollerPanel.setIndex(0);
        UpdateFeeds.startUpdateFeeds(false, true);
    },
    //"Installed is available" changed: the counts in the menu change with it.
    listInstalledChanged: function () {
        this.$.packagesMenu.listOfEverythingChanged(null, preware.PackagesModel.packages);
    },
    //an install/update/remove completed: the menu counts are stale, and so is any
    //package list already open (it was filtered when the user opened it). Bring
    //both back in sync without changing what panel is showing.
    handlePackageRefresh: function () {
        var count = this.$.packagesMenu.refreshLists();
        this.refreshGroups();
        if (count >= 0) {
            this.$.PackageRepeater.rebuild(count);
            this.$.NoPackages.setShowing(count === 0);
        }
        if (this.isShowingConnected()) {
            this.refreshConnected(false);
        }
        this.updateAllButtonState();
    },
    multiInstallFinished: function () {
        if (this.isShowingConnected()) {
            this.refreshConnected(true);
        }
        this.updateAllButtonState();
    },
    //Update All: under the Package Updates list, while it has any.
    updateAllButtonState: function () {
        var menu = this.$.packagesMenu;
        this.$.UpdateAllButton.setShowing(menu.currentPackageFilter === menu.packageFilters.updatable &&
            menu.availablePackages.length > 0);
        this.$.UpdateAllButton.setDisabled(!!preware.PackagesModel.multiPkgs);
        this.$.ConnectedInstallButton.setDisabled(!!preware.PackagesModel.multiPkgs);
    },
    updateAllTapped: function () {
        if (preware.PackagesModel.startUpdateAll(this.$.packagesMenu.availablePackages.slice())) {
            this.$.UpdateAllButton.setDisabled(true);
        }
    },
    //the package Update All is on: its details show its progress.
    showUpdateAllPackage: function (inSender, inEvent) {
        if (this.$.packageDisplay.currentPackage === inEvent.pkg) {
            this.$.packageDisplay.refreshPackageDisplay();
        } else {
            this.$.packageDisplay.setCurrentPackage(inEvent.pkg);
        }
        this.$.PackageDisplayPanels.setIndex(1);
        if (!this.isSearching() && this.getIndex() === this.packagePanelsIndex) {
            this.setIndex(this.packageDisplayPanelsIndex);
        }
    },
    //the sort order preference changed: sort the list shown again.
    listSortChanged: function (inSender, inEvent) {
        this.setListSort(this.$.packagesMenu.defaultSort(this.listName));
        if (this.isSearching()) {
            this.setSearchSort(this.$.packagesMenu.defaultSort("search"));
        }
    },
    //the sort toggles under the package list and the search results.
    listSortTapped: function (inSender, inEvent) {
        if (!this.settingSort && inEvent.originator.getActive()) {
            this.setListSort(inEvent.originator.value);
        }
        return true;
    },
    searchSortTapped: function (inSender, inEvent) {
        if (!this.settingSort && inEvent.originator.getActive()) {
            this.setSearchSort(inEvent.originator.value);
        }
        return true;
    },
    //shows sort as chosen in a sort toggle.
    showSort: function (toggle, sort) {
        this.settingSort = true;
        toggle.getControls().forEach(function (c) {
            c.setActive(c.value === sort);
        });
        this.settingSort = false;
    },
    setListSort: function (sort) {
        var menu = this.$.packagesMenu;
        menu.currentSort = sort;
        menu.sortPackageList();
        this.showSort(this.$.ListSort, sort);
        this.refillRepeater(this.$.PackageRepeater, menu.isShowingPackageList() ? menu.availablePackages.length : 0);
        this.$.PackageScroller.scrollToTop();
    },
    setSearchSort: function (sort) {
        this.searchSort = sort;
        this.showSort(this.$.SearchSort, sort);
        if (this.searchResults) {
            this.$.packagesMenu.sortPackages(this.searchResults, sort);
            this.refillRepeater(this.$.SearchRepeater, this.searchResults.length);
            this.$.SearchScroller.scrollToTop();
        }
    },
    //shows count items in repeater again. With the same count (the list was
    //sorted again) the rows it has built are set up again, which is quicker
    //than building them anew (a ChunkedRepeater may have built only some).
    refillRepeater: function (repeater, count) {
        var items = repeater.getClientControls(), i;
        if (count > 0 && repeater.count === count && items.length > 0 && items.length <= count) {
            //the highlighted item now shows another package
            if (this.selectedItems && this.selectedItems.list && !this.selectedItems.list.destroyed) {
                this.selectedItems.list.removeClass("list-item-active");
            }
            for (i = 0; i < items.length; i += 1) {
                repeater.doSetupItem({index: i, item: items[i]});
            }
        } else {
            repeater.setCount(0);
            repeater.setCount(count);
        }
    },
    //package lists and search results build their rows as they are scrolled to
    //(preware.ChunkedRepeater).
    packageListScrolled: function () {
        this.$.PackageRepeater.moreIfNear(this.$.PackageScroller);
    },
    searchListScrolled: function () {
        this.$.SearchRepeater.moreIfNear(this.$.SearchScroller);
    },
    //sets up the divider above item index of list, sorted by sort.
    setupDivider: function (item, list, index, sort, letters) {
        var menu = this.$.packagesMenu,
            now = Math.round(new Date().getTime() / 1000),
            text = menu.sortDivider(list[index], sort, letters, now),
            prev = index > 0 ? menu.sortDivider(list[index - 1], sort, letters, now) : null;
        item.$.listItem.setDivider(text !== prev ? text : "");
    },
    //react to package selection in packagesMenu.
    packagesMenuSelected: function (inSender, inEvent) {
        //the menu stays visible during a search: picking a list from it leaves the search.
        this.dropConnected();
        this.cancelSearch();
        this.listName = inEvent.name;
        this.showSort(this.$.ListSort, this.$.packagesMenu.currentSort);
        if (inEvent.name === "available") {
            this.startGroups();
        } else if (inEvent.name !== "filtered") {
            this.groups = null;
        }

        if (inEvent.packagesLength >= 0) {
            this.$.PackageRepeater.setCount(0);
            this.$.PackageRepeater.setCount(inEvent.packagesLength);
            this.$.PackageScroller.scrollToTop();
            this.$.NoPackages.setShowing(inEvent.packagesLength === 0);
            this.$.PackagePanels.setIndex(1);
            this.setIndex(this.packagePanelsIndex);
        }
        this.updateAllButtonState();
    },

    //Action Functions
    log: function (text) {
        //this.inherited(arguments);
        console.log.apply(console, arguments);
        this.$.SpinnerText.setContent(text);
    },
    //highlight the tapped list item (one per group), like the menu item.
    markSelected: function (group, inEvent) {
        var item = inEvent && inEvent.originator, old;
        while (item && item.kindName !== "ListItem") {
            item = item.parent;
        }
        this.selectedItems = this.selectedItems || {};
        old = this.selectedItems[group];
        if (old && old !== item && !old.destroyed) {
            old.removeClass("list-item-active");
        }
        if (item && item.addClass) {
            item.addClass("list-item-active");
        }
        this.selectedItems[group] = item;
    },
    //Available Packages' group lists, like the original Preware (its pkg-groups
    //scene): first the types, or with the toggle the feeds. Each choice narrows
    //the packages down, until a type and a category are chosen: then their
    //packages show. After a type: its categories, or its feeds; after a feed:
    //its types; and so on (see nextGroupLevel). A level is {options: what its
    //toggle offers, kind: what it lists, items, chosen: the name tapped}.
    groupFields: {types: "type", feeds: "feed", categories: "category"},
    startGroups: function () {
        this.groups = [];
        this.showGroupLevel(0, {options: ["types", "feeds"], kind: "types"});
    },
    //what the levels before level have chosen.
    groupFilter: function (level) {
        var filter = {}, i;
        for (i = 0; i < level && i < this.groups.length; i += 1) {
            if (this.groups[i].chosenAll) {
                filter.allCategories = true;
            } else {
                filter[this.groupFields[this.groups[i].kind]] = this.groups[i].chosen;
            }
        }
        return filter;
    },
    //shows group (a level) as level number level (0-2), drops the ones after it.
    showGroupLevel: function (level, group) {
        var n = level + 1, names = {types: $L("Types"), feeds: $L("Feeds"), categories: $L("Categories")},
            toggle = this.$["GroupToggle" + n], buttons = toggle.getControls(), options = group.options.slice().sort(), i;
        this.groups.length = level;
        this.groups.push(group);
        group.items = this.$.packagesMenu.groupCounts(group.kind, this.groupFilter(level));
        this.$["GroupTitle" + n].setContent(names[group.kind]);
        //the toggle, when there's a choice (in the original's order: by name)
        toggle.setShowing(options.length > 1);
        for (i = 0; i < buttons.length; i += 1) {
            buttons[i].setShowing(i < options.length);
            buttons[i].value = options[i] || "";
            buttons[i].setContent(names[options[i]] || "");
        }
        this.showSort(toggle, group.kind);
        if (this.selectedItems && this.selectedItems["group" + n] && !this.selectedItems["group" + n].destroyed) {
            this.selectedItems["group" + n].removeClass("list-item-active");
        }
        this.$["GroupRepeater" + n].setCount(0);
        this.$["GroupRepeater" + n].setCount(group.items.length);
        this.$["GroupScroller" + n].scrollToTop();
        this.$["GroupPanels" + n].setIndex(1);
        this.$.PackagePanels.setIndex(0);
        this.setIndex(this.groupPanelsIndex + level);
    },
    //what follows tapping an item of level: the next group list, or null when
    //a type and a category are chosen. Like the original: the other choices of
    //its toggle, and the categories first once a type is chosen.
    nextGroupLevel: function (level) {
        var group = this.groups[level], filter = this.groupFilter(level + 1),
            rest = group.options.filter(function (k) {
                return k !== group.kind;
            });
        if (filter.type && (filter.category || filter.allCategories)) {
            return null;
        }
        if (filter.type && rest.indexOf("categories") === -1) {
            rest.unshift("categories");
        }
        return rest.length ? {options: rest, kind: rest[0]} : null;
    },
    groupLevelOf: function (inEvent) {
        var c = inEvent.originator;
        while (c && !c.groupLevel) {
            c = c.parent;
        }
        return c ? c.groupLevel - 1 : -1;
    },
    groupTapped: function (inSender, inEvent) {
        var level = this.groupLevelOf(inEvent), group = this.groups && this.groups[level], next;
        if (!group || !group.items[inEvent.index]) {
            return;
        }
        this.markSelected("group" + (level + 1), inEvent);
        group.chosen = group.items[inEvent.index].name;
        group.chosenAll = !!group.items[inEvent.index].all;
        next = this.nextGroupLevel(level);
        if (next) {
            this.showGroupLevel(level + 1, next);
        } else {
            this.groups.length = level + 1;
            this.$.packagesMenu.filterPackages(this.groupFilter(level + 1));
        }
    },
    //the toggle of a group list: list the other kind of groups instead.
    groupToggleTapped: function (inSender, inEvent) {
        var level = this.groupLevelOf(inEvent), group = this.groups && this.groups[level];
        if (this.settingSort || !inEvent.originator.getActive() || !group || group.kind === inEvent.originator.value) {
            return true;
        }
        this.showGroupLevel(level, {options: group.options, kind: inEvent.originator.value});
        return true;
    },
    //an install/remove changed the counts: count the group lists shown again.
    refreshGroups: function () {
        var i, n;
        for (i = 0; this.groups && i < this.groups.length; i += 1) {
            n = i + 1;
            this.groups[i].items = this.$.packagesMenu.groupCounts(this.groups[i].kind, this.groupFilter(i));
            this.refillRepeater(this.$["GroupRepeater" + n], this.groups[i].items.length);
        }
    },
    //search in package titles (and descriptions, if enabled in the preferences).
    searchChanged: function (inSender, inEvent) {
        enyo.job("preware-search", this.doSearch.bind(this, inEvent.value || ""), 300);
        return true;
    },
    //search results are shown (in the package list column, card 2).
    isSearching: function () {
        return this.$.PackagePanels.getIndex() === 2;
    },
    //leave the search without moving to another panel (the caller does that).
    cancelSearch: function () {
        var card = this.packageCardBeforeSearch || 0;
        this.searchHeader.clear();
        if (this.isSearching()) {
            this.$.PackagePanels.setIndex(card);
        }
        this.searchOpenedPackage = false;
        this.indexBeforeSearch = null;
        return card;
    },
    //clear the search and go back to the panel shown when it started.
    endSearch: function () {
        var index = this.indexBeforeSearch || this.menuPanelsIndex,
            openedPackage = this.searchOpenedPackage,
            card = this.cancelSearch();
        this.searchHeader.blur();
        if (openedPackage && index === this.packageDisplayPanelsIndex) {
            //the package details now show a search result: go to the list before them.
            index = card === 1 ? this.packagePanelsIndex : this.menuPanelsIndex;
        }
        this.setIndex(index);
    },
    //Emptying the field does nothing: the results stay until a new search (Enter)
    //or back. Ending the search here re-laid out the columns, which took the focus
    //(and the virtual keyboard) away from the field while the user was typing.
    doSearch: function (text) {
        var i, pkg, searchDesc = preware.PrefCookie.get().searchDesc;
        if (this.$.ScrollerPanel.getIndex() === 0) {
            return; //still loading.
        }
        text = text.toLowerCase().trim();
        if (!text) {
            return;
        }
        this.dropConnected();
        if (!this.isSearching()) {
            this.indexBeforeSearch = this.getIndex();
            this.packageCardBeforeSearch = this.$.PackagePanels.getIndex();
            this.searchSort = this.$.packagesMenu.defaultSort("search");
        }
        this.searchResults = [];
        for (i = 0; i < preware.PackagesModel.packages.length; i += 1) {
            pkg = preware.PackagesModel.packages[i];
            if ((pkg.title && pkg.title.toLowerCase().indexOf(text) >= 0) ||
                    (searchDesc && pkg.description && pkg.description.toLowerCase().indexOf(text) >= 0)) {
                this.searchResults.push(pkg);
            }
        }
        this.$.packagesMenu.sortPackages(this.searchResults, this.searchSort);
        this.showSort(this.$.SearchSort, this.searchSort);
        this.$.SearchRepeater.setCount(0);
        this.$.SearchRepeater.setCount(this.searchResults.length);
        this.$.NoSearchResults.setShowing(this.searchResults.length === 0);
        this.$.PackagePanels.setIndex(2);
        this.$.SearchScroller.scrollToTop();
        this.setIndex(this.packagePanelsIndex);
    },
    setupSearchItem: function (inSender, inEvent) {
        var pkg = this.searchResults[inEvent.index];
        if (pkg) {
            this.setupDivider(inEvent.item, this.searchResults, inEvent.index, this.searchSort, true);
            inEvent.item.$.listItem.setTitleAndIcon(pkg.title, pkg.icon);
        }
        return true;
    },
    searchResultTapped: function (inSender, inEvent) {
        this.searchOpenedPackage = true;
        this.markSelected("list", inEvent);
        this.$.packageDisplay.setCurrentPackage(this.searchResults[inEvent.index]);
        this.$.PackageDisplayPanels.setIndex(1);
        this.setIndex(this.packageDisplayPanelsIndex);
    },
    packageTapped: function (inSender, inEvent) {
        this.markSelected("list", inEvent);
        this.$.packageDisplay.setCurrentPackage(this.$.packagesMenu.getPackage(inEvent.index));

        this.$.PackageDisplayPanels.setIndex(1);
        this.setIndex(this.packageDisplayPanelsIndex);
    },
    //"View Them": the packages a package needs (type "install") or the installed
    //packages that need it ("remove"), in the package list column. The details
    //of the package stay next to them.
    showConnectedPackages: function (inSender, inEvent) {
        if (!this.isShowingConnected()) {
            this.cardBeforeConnected = this.$.PackagePanels.getIndex();
            this.indexBeforeConnected = this.getIndex();
        }
        this.connected = {type: inEvent.type, pkg: inEvent.pkg};
        this.$.ConnectedTitle.setContent(inEvent.type === "install" ? $L("Packages To Be Installed / Updated") : $L("Packages That Depend On This"));
        this.$.ConnectedInstallButton.setShowing(inEvent.type === "install");
        this.refreshConnected(false);
        this.$.PackagePanels.setIndex(3);
        this.$.ConnectedScroller.scrollToTop();
        if (this.$.packageDisplay.currentPackage !== inEvent.pkg) {
            this.$.packageDisplay.setCurrentPackage(inEvent.pkg);
        }
        this.$.PackageDisplayPanels.setIndex(1);
        this.setIndex(this.packageDisplayPanelsIndex);
        if (!this.showsColumnBefore(this.packageDisplayPanelsIndex)) {
            this.setIndex(this.packagePanelsIndex); //phone: the list.
        }
        this.updateAllButtonState();
    },
    isShowingConnected: function () {
        return !!this.connected && this.$.PackagePanels.getIndex() === 3;
    },
    //the package first, then the packages connected to it (what's still needed,
    //like the original). Nothing left (all installed): back to where it opened from.
    refreshConnected: function (closeIfDone) {
        var c = this.connected, nums, pkgs = this.$.packagesMenu;
        if (!c) {
            return;
        }
        nums = c.type === "install" ? c.pkg.getDependenciesRecursive(true) : c.pkg.getDependent(true);
        if (closeIfDone && nums.length === 0 && (c.type !== "install" || (c.pkg.isInstalled && !c.pkg.hasUpdate))) {
            this.closeConnected();
            return;
        }
        c.list = [c.pkg].concat(nums.map(function (n) {
            return preware.PackagesModel.packages[n];
        }));
        this.$.ConnectedRepeater.setCount(0);
        this.$.ConnectedRepeater.setCount(c.list.length);
    },
    //leave the list, back to where it opened from: the details of the package
    //it was opened for (like the original, back to that package).
    closeConnected: function () {
        var index = this.indexBeforeConnected, pkg = this.connected && this.connected.pkg;
        if (this.dropConnected()) {
            if (index === this.packageDisplayPanelsIndex && pkg && this.$.packageDisplay.currentPackage !== pkg) {
                this.$.packageDisplay.setCurrentPackage(pkg);
            }
            this.setIndex(index);
        }
    },
    //leave the list without moving to another panel (the caller does that).
    dropConnected: function () {
        var showing = this.isShowingConnected();
        if (showing) {
            this.$.PackagePanels.setIndex(this.cardBeforeConnected || 0);
        }
        this.connected = null;
        return showing;
    },
    setupConnectedItem: function (inSender, inEvent) {
        var c = this.connected, pkg = c && c.list[inEvent.index], item = inEvent.item;
        if (pkg) {
            item.$.listItem.setTitleAndIcon(pkg.title, pkg.icon);
            //the package itself, then the others
            item.$.listItem.setDivider(inEvent.index === 0 ? $L("Package") :
                    (inEvent.index > 1 ? "" : (c.type === "install" ? $L("Needs") : $L("Needed By"))));
        }
        return true;
    },
    connectedTapped: function (inSender, inEvent) {
        this.markSelected("list", inEvent);
        this.$.packageDisplay.setCurrentPackage(this.connected.list[inEvent.index]);
        this.$.PackageDisplayPanels.setIndex(1);
        this.setIndex(this.packageDisplayPanelsIndex);
    },
    connectedInstallTapped: function () {
        var c = this.connected;
        if (c && preware.PackagesModel.startMultiInstall(c.pkg, c.pkg.getDependenciesRecursive(true))) {
            this.updateAllButtonState();
            //the details of the package show the progress and the result: when
            //the list covers them (one column: phone, portrait tablet), go there.
            //Back returns to the list.
            if (this.getIndex() !== this.packageDisplayPanelsIndex) {
                if (this.$.packageDisplay.currentPackage !== c.pkg) {
                    this.$.packageDisplay.setCurrentPackage(c.pkg);
                }
                this.$.PackageDisplayPanels.setIndex(1);
                this.setIndex(this.packageDisplayPanelsIndex);
            }
        }
    },
    processStatusUpdate: function (inSender, inEvent) {
        this.log(inEvent.message);
        this.$.SpinnerText.setContent(inEvent.message);
        this.$.spinner.setShowing(!inEvent.error);
    },
    doneLoading: function (inSender, inEvent) {
        this.searchHeader.set("disabled", false);
        this.log("Done loading, num Packages: " + preware.PackagesModel.packages.length);
        this.$.packagesMenu.set("listOfEverything", preware.PackagesModel.packages);
        // so if we're inactive we know to push a scene when we return
        //this.isLoading = false;

        // show that we're done (while the pushed scene is going)
        this.processStatusUpdate(this, {message: $L("<strong>Done!</strong>")});
        //this.hideProgress();

        // we're done loading so let the device sleep if it needs to
        // TODO: convert stayAwake.js to enyo, implement stayAwake.start() etc
        //this.stayAwake.end();

        //alert(packages.packages.length);

        if ((!this.isActive || !this.isVisible)) {
            // if we're not the active scene, let them know via banner:
            if (this.onlyLoad) {
                //TODO: show banner notification.
                console.log("Preware: Done Loading Feeds.");
                //navigator.notification.showBanner($L("Preware: Done Loading Feeds"), {source: 'updateNotification'}, 'miniicon.png');
            } else {
                console.log("Preware: Done Updating Feeds.");
                //TODO: show banner notification.
                //navigator.notification.showBanner($L("Preware: Done Updating Feeds"), {source: 'updateNotification'}, 'miniicon.png');
            }
        }

        // show the menu
        var storedThis = this;
        setTimeout(function () {
            storedThis.$.ScrollerPanel.setIndex(1);
        }, 500);
    },
    setupGroupItem: function (inSender, inEvent) {
        var group = this.groups && this.groups[inSender.groupLevel - 1],
            item = group && group.items[inEvent.index];
        if (item) {
            inEvent.item.$.listItem.set("title", item.name);
            inEvent.item.$.listItem.set("count", item.count);
        }
        return true;
    },
    setupPackageItem: function (inSender, inEvent) {
        var pkg = this.$.packagesMenu.getPackage(inEvent.index);
        if (pkg && pkg.title) {
            this.setupDivider(inEvent.item, this.$.packagesMenu.availablePackages, inEvent.index,
                this.$.packagesMenu.currentSort, this.listName === "all");
            inEvent.item.$.listItem.setTitleAndIcon(pkg.title, pkg.icon);
        }
        return true;
    }
});
