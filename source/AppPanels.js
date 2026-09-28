/*jslint sloppy: true, continue:true */
/*global enyo, window, device, console, preware, $L, setTimeout, UpdateFeeds */

enyo.kind({
    name: "AppPanels",
    //menu always visible on the left, other panels open to its right; on a phone
    //one panel at a time. No sliding or zooming transitions (see ColumnPanels).
    kind: "preware.ColumnPanels",
    peekWidth: 70,   // (600-320)/4
    classes: "app-panels enyo-fill",
    // required ipkgservice
    ipkgServiceVersion: 14,
    // filtered category/package lists
    currentType: "",
    showingTypeAndCategoriesPanels: false,

    menuPanelsIndex: 0,
    typePanelsIndex: 1,
    categoryPanelsIndex: 2,
    packagePanelsIndex: 3,
    packageDisplayPanelsIndex: 4,

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
            onMultiInstallFinished: "updateAllButtonState",
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

        //Types
        {
            name: "TypePanels",
            //legacy webOS: menu always visible on the left, other panels open to its right.
            kind: "Panels",
            arrangerKind: "CardArranger",
            draggable: false,
            style: "width: 33.3%;",
            showing: false,
            components: [
                {kind: "EmptyPanel"},
                {
                    kind: "FittableRows",
                    components: [
                        {kind: "onyx.Toolbar", components: [
                            {style: "display: inline-block; position: absolute;", content: "Types"}
                        ]},
                        {
                            kind: "Scroller",
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "TypeRepeater", kind: "Repeater", onSetupItem: "setupTypeItem", count: 0, components: [
                                    {kind: "ListItem", title: "[type]", ontap: "typeTapped"}
                                ]}
                            ]
                        },
                        {kind: "GrabberToolbar"}
                    ]
                }
            ]
        },

        //Categories
        {
            name: "CategoryPanels",
            //legacy webOS: menu always visible on the left, other panels open to its right.
            kind: "Panels",
            arrangerKind: "CardArranger",
            draggable: false,
            style: "width: 33.3%;",
            showing: false,
            components: [
                {kind: "EmptyPanel"},
                {kind: "FittableRows",
                    components: [
                        {kind: "onyx.Toolbar", components: [
                            {style: "display: inline-block; position: absolute;", content: "Categories"}
                        ]},
                        {kind: "Scroller",
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "CategoryRepeater", kind: "Repeater", onSetupItem: "setupCategoryItem", count: 0, components: [
                                    {kind: "ListItem", title: "[category]", ontap: "categoryTapped"}
                                ]}
                            ]},
                        {kind: "GrabberToolbar"}
                    ]}
            ]
        },

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
                            kind: "Scroller",
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "NoPackages", showing: false, style: "color: white; text-align: center; padding: 24px;", content: $L("No packages")},
                                {name: "PackageRepeater", kind: "Repeater", onSetupItem: "setupPackageItem", count: 0, components: [
                                    {kind: "ListItem", title: "[package]", icon: true, ontap: "packageTapped"}
                                ]}
                            ]
                        },
                        {kind: "GrabberToolbar", components: [
                            //Package Updates list only
                            {name: "UpdateAllButton", kind: "onyx.Button", classes: "onyx-affirmative", showing: false, content: $L("Update All"), ontap: "updateAllTapped"}
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
                            horizontal: "hidden",
                            classes: "enyo-fill",
                            style: "background-image:url('assets/bg.png')",
                            touch: true,
                            fit: true,
                            components: [
                                {name: "NoSearchResults", showing: false, style: "color: white; text-align: center; padding: 24px;", content: $L("No packages found")},
                                {name: "SearchRepeater", kind: "Repeater", onSetupItem: "setupSearchItem", count: 0, components: [
                                    {kind: "ListItem", title: "[package]", icon: true, ontap: "searchResultTapped"}
                                ]}
                            ]
                        },
                        {kind: "GrabberToolbar"}
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
        [this.$.ScrollerPanel, this.$.TypePanels, this.$.CategoryPanels,
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
        if (index === this.typePanelsIndex || index === this.categoryPanelsIndex) {
            return !this.showingTypeAndCategoriesPanels || this.isSearching();
        }
        return false;
    },
    handleBackGesture: function (inSender, inEvent) {
        var index = this.getIndex();
        if (this.isSearching() && index === this.packageDisplayPanelsIndex) { //back to the search results.
            this.setIndex(this.packagePanelsIndex);
        } else if (this.isSearching()) { //leave the search, back to where it started.
            this.endSearch();
        } else if (!this.showingTypeAndCategoriesPanels && index === this.categoryPanelsIndex + 1) { //mind the gap.
            this.setIndex(this.menuPanelsIndex);
        } else {//all panels are showing, that's easy.
            this.setIndex(Math.max(index - 1, 0));
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
        if (count >= 0) {
            this.$.PackageRepeater.setCount(0);
            this.$.PackageRepeater.setCount(count);
            this.$.NoPackages.setShowing(count === 0);
        }
        this.updateAllButtonState();
    },
    //Update All: under the Package Updates list, while it has any.
    updateAllButtonState: function () {
        var menu = this.$.packagesMenu;
        this.$.UpdateAllButton.setShowing(menu.currentPackageFilter === menu.packageFilters.updatable &&
            menu.availablePackages.length > 0);
        this.$.UpdateAllButton.setDisabled(!!preware.PackagesModel.multiPkgs);
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
        this.$.packagesMenu.sortPackageList();
        this.$.PackageRepeater.setCount(0);
        this.$.PackageRepeater.setCount(this.$.packagesMenu.availablePackages.length);
    },
    //react to package selection in packagesMenu.
    packagesMenuSelected: function (inSender, inEvent) {
        //the menu stays visible during a search: picking a list from it leaves the search.
        this.cancelSearch();
        this.showTypeAndCategoriesPanels(inEvent.showTypeAndCategoriesPanels);

        if (inEvent.typesLength >= 0) {
            this.$.TypeRepeater.setCount(0);
            this.$.CategoryRepeater.setCount(0);
            this.$.PackageRepeater.setCount(0);
            this.$.TypeRepeater.setCount(inEvent.typesLength);
            this.$.TypePanels.setIndex(1);
            this.$.CategoryPanels.setIndex(0);
            this.$.PackagePanels.setIndex(0);
            this.setIndex(this.typePanelsIndex);
        }

        if (inEvent.categoriesLength >= 0) {
            this.$.PackageRepeater.setCount(0);
            this.$.CategoryRepeater.setCount(0);
            this.$.CategoryRepeater.setCount(inEvent.categoriesLength);
            this.$.CategoryPanels.setIndex(1);
            this.$.PackagePanels.setIndex(0);
            this.setIndex(this.categoryPanelsIndex);
        }

        if (inEvent.packagesLength >= 0) {
            this.$.PackageRepeater.setCount(0);
            this.$.PackageRepeater.setCount(inEvent.packagesLength);
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
    showTypeAndCategoriesPanels: function (show) {
        //ColumnPanels shows/hides them on setIndex (isPanelSkipped).
        this.showingTypeAndCategoriesPanels = show;
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
    typeTapped: function (inSender, inEvent) {
        this.markSelected("type", inEvent);
        this.currentType = this.$.packagesMenu.availableTypes[inEvent.index].type;
        this.$.packagesMenu.filterCategories(this.currentType);
    },
    categoryTapped: function (inSender, inEvent) {
        this.markSelected("category", inEvent);
        this.$.packagesMenu.filterByCategoryAndType(this.$.packagesMenu.availableCategories[inEvent.index].category, this.currentType);
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
        if (!this.isSearching()) {
            this.indexBeforeSearch = this.getIndex();
            this.packageCardBeforeSearch = this.$.PackagePanels.getIndex();
        }
        this.searchResults = [];
        for (i = 0; i < preware.PackagesModel.packages.length; i += 1) {
            pkg = preware.PackagesModel.packages[i];
            if ((pkg.title && pkg.title.toLowerCase().indexOf(text) >= 0) ||
                    (searchDesc && pkg.description && pkg.description.toLowerCase().indexOf(text) >= 0)) {
                this.searchResults.push(pkg);
            }
        }
        this.searchResults.sort(function (a, b) {
            return (a.title || "").toLowerCase().localeCompare((b.title || "").toLowerCase());
        });
        this.$.SearchRepeater.setCount(this.searchResults.length);
        this.$.NoSearchResults.setShowing(this.searchResults.length === 0);
        this.$.PackagePanels.setIndex(2);
        this.$.SearchScroller.scrollToTop();
        this.setIndex(this.packagePanelsIndex);
    },
    setupSearchItem: function (inSender, inEvent) {
        var pkg = this.searchResults[inEvent.index];
        if (pkg) {
            inEvent.item.$.listItem.$.ItemTitle.setContent(pkg.title);
            inEvent.item.$.listItem.setIcon(pkg.icon);
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
    setupTypeItem: function (inSender, inEvent) {
    	// TODO: refactor the TypeRepeater into its own kind
    	var typeRecord = this.$.packagesMenu.availableTypes[inEvent.index];
        inEvent.item.$.listItem.set("title", typeRecord.type);
        inEvent.item.$.listItem.set("count", typeRecord.count);
        return true;
    },
    setupCategoryItem: function (inSender, inEvent) {
    	// TODO: refactor the CategoryRepeater into its own kind
        var categoryRecord = this.$.packagesMenu.availableCategories[inEvent.index];
        inEvent.item.$.listItem.set("title", categoryRecord.category);
        inEvent.item.$.listItem.set("count", categoryRecord.count);
        return true;
    },
    setupPackageItem: function (inSender, inEvent) {
        var pkg = this.$.packagesMenu.getPackage(inEvent.index);
        if (pkg && pkg.title) {
            inEvent.item.$.listItem.$.ItemTitle.setContent(pkg.title);
            inEvent.item.$.listItem.setIcon(pkg.icon);
        }
        return true;
    }
});
