// Preware App kind and main window.
/*jslint sloppy: true */
/*global enyo, onyx, preware, $L, device, PalmServiceBridge */

//to reload changes on device: luna-send -n 1 palm://com.palm.applicationManager/rescan {}

//webos-lib's AppMenu sets its height for 30px items; the items are taller here
//(App.css), so let it take the height of its items (up to maxHeight).
enyo.kind({
    name: "preware.AppMenu",
    kind: "enyo.AppMenu",
    show: function () {
        this.inherited(arguments);
        this.applyStyle("height", null);
        this.applyStyle("max-height", this.maxHeight + "px");
    }
});

enyo.kind({
    name: "App",
    kind: "FittableRows",
    classes: "enyo-fit",
    components: [
        {
            kind: "Signals",
            onbackbutton: "handleBackGesture",
            onCoreNaviDragStart: "handleCoreNaviDragStart",
            onCoreNaviDrag: "handleCoreNaviDrag",
            onCoreNaviDragFinish: "handleCoreNaviDragFinish",
            onLaunchedWithInstallRequest: "handleLaunchInstallRequest",
            onPackageActionRequired: "handlePackageActionRequired",
            onUpdateFeedsFinished: "checkResourceHandler",
            onAskUpdateFeeds: "askUpdateFeeds",
            onrelaunch: "handleRelaunch",
            onkeypress: "typeToSearch"
        },
        {
            name: "searchHeader",
            kind: "preware.SearchHeader",
            disabled: true,
            onSearch: "handleSearch",
            title: "Preware 2",
            taglines: [
                "I live... again...",
                "Miss me?",
                "Installing packages, with a penguin!",
                "How many Ports could a webOS Ports Port?",
                "Not just for Apps anymore.",
                "Serving apps for the last 1.67x10^8 seconds",
                "Now with 100% more Enyo2!"
            ]
        },
        {name: "AppPanels", kind: "AppPanels", fit: true},
        {kind: "CoreNavi", fingerTracking: true},
        {name: "SettingsDialog", kind: "SettingsDialog"},
        {name: "ManageFeedsDialog", kind: "ManageFeedsDialog"},
        {name: "LunaManagerDialog", kind: "LunaManagerDialog"},
        {name: "InstallPackageDialog", kind: "InstallPackageDialog", onHide: "installDialogHidden"},
        {name: "RestartDialog", kind: "Preware.ChoiceDialog", title: $L("Restart Required"), onAction: "restartAccepted", onDismiss: "restartDeclined"},
        {name: "UpdateFeedsDialog", kind: "Preware.ChoiceDialog", title: $L("Update Feeds"), okLabel: $L("Update"), okClasses: "onyx-affirmative", cancelLabel: $L("Not Now"),
            body: $L("Check the feeds for new and updated packages now?"), onAction: "updateFeedsYes", onDismiss: "updateFeedsNo"},
                {name: "ResourceHandlerDialog", kind: "Preware.ChoiceDialog", title: $L("FileType Association"), onAction: "resourceHandlerAccepted", onDismiss: "resourceHandlerDeclined"},
        {
            kind: "preware.AppMenu", //onSelect: "appMenuItemSelected",
            style: "overflow: hidden;",
            components: [
                //matches the original Preware's swipe-down menu, minus Help.
                { kind: "enyo.AppMenuItem", content: $L("Preferences"), ontap: "showSettingsDialog" },
                { kind: "enyo.AppMenuItem", content: $L("Update Feeds"), ontap: "reloadPackageList"},
                { kind: "enyo.AppMenuItem", content: $L("Manage Feeds"), ontap: "showManageFeedsDialog" },
                { kind: "enyo.AppMenuItem", content: $L("Install Package"), ontap: "showInstallPackageDialog"},
                { kind: "enyo.AppMenuItem", content: $L("Saved Package List"), ontap: "showSavedPackageList"},
                { kind: "enyo.AppMenuItem", content: $L("Luna Manager"), ontap: "showLunaManagerDialog"}
            ]
        }
    ],
    create: function () {
        this.inherited(arguments);
        this.$.AppPanels.searchHeader = this.$.searchHeader;
    },
    //Handlers
    handleSearch: function (inSender, inEvent) {
        return this.$.AppPanels.searchChanged(inSender, inEvent);
    },
    handleBackGesture: function (inSender, inEvent) {
        if (this.$.ManageFeedsDialog.get('showing')) {
            this.$.AppPanels.doReloadList();
        }
        //hide possible open dialogs on back gesture?
        this.$.SettingsDialog.hide();
        this.$.ManageFeedsDialog.hide();
        this.$.LunaManagerDialog.hide();
        inEvent.preventDefault();
    },
    handleCoreNaviDragStart: function (inSender, inEvent) {
        this.$.AppPanels.dragstartTransition(this.$.AppPanels.draggable === false ? this.reverseDrag(inEvent) : inEvent);
    },
    handleCoreNaviDrag: function (inSender, inEvent) {
        this.$.AppPanels.dragTransition(this.$.AppPanels.draggable === false ? this.reverseDrag(inEvent) : inEvent);
    },
    handleCoreNaviDragFinish: function (inSender, inEvent) {
        this.$.AppPanels.dragfinishTransition(this.$.AppPanels.draggable === false ? this.reverseDrag(inEvent) : inEvent);
    },
    reloadPackageList: function (inSender, inEvent) {
        this.$.AppPanels.doReloadList();
    },
    handleLaunchInstallRequest: function (inSender, inEvent) {
        //enyo.info("Handling launch with install request on: " + this.name + " for " + inEvent.params);
        this.$.InstallPackageDialog.closesApp = !!this.$.AppPanels.launchedForInstall;
        this.showInstallPackageDialog();
        this.$.InstallPackageDialog.doInstall(inEvent.params);
    },
    installDialogHidden: function (inSender, inEvent) {
        if (inEvent.originator !== inSender) {
            return; //a popup inside the dialog
        }
        if (this.$.AppPanels.launchedForInstall) {
            //another app started Preware just to install a package: closing the dialog closes Preware.
            window.close();
        }
    },
    //the app was already running and got launched again, e.g. by opening an ipk from another app.
    handleRelaunch: function (inSender, inEvent) {
        var file = preware.ResourceHandler.launchFile(inEvent);
        if (file) {
            this.handleLaunchInstallRequest(this, {params: file});
        }
    },
    //A key typed on a physical keyboard (legacy webOS phones, keyboards on LuneOS
    //devices) while no text field has the focus goes to the search field.
    typeToSearch: function (inSender, inEvent) {
        var target = inEvent.target, code = inEvent.charCode || inEvent.keyCode;
        if (target && (/^(INPUT|TEXTAREA|SELECT)$/.test(target.tagName) || target.isContentEditable)) {
            return; //typing into a text field already
        }
        if (inEvent.ctrlKey || inEvent.altKey || inEvent.metaKey || !code || code < 32 || code === 127) {
            return; //not a character
        }
        if (this.dialogShowing()) {
            return;
        }
        if (this.$.searchHeader.startTyping(String.fromCharCode(code))) {
            inEvent.preventDefault();
            return true;
        }
    },
    dialogShowing: function () {
        var names = ["SettingsDialog", "ManageFeedsDialog", "LunaManagerDialog", "InstallPackageDialog", "RestartDialog",
                     "ResourceHandlerDialog", "UpdateFeedsDialog", "appMenu"], i;
        for (i = 0; i < names.length; i += 1) {
            if (this.$[names[i]] && this.$[names[i]].showing) {
                return true;
            }
        }
        return false;
    },
    //"Update Feeds: Ask At Launch" preference
    askUpdateFeeds: function (inSender, inEvent) {
        this.updateFeedsCallback = inEvent.callback;
        this.$.UpdateFeedsDialog.show();
    },
    updateFeedsYes: function () {
        this.updateFeedsAnswered(true);
        return true;
    },
    updateFeedsNo: function () {
        this.updateFeedsAnswered(false);
        return true;
    },
    updateFeedsAnswered: function (update) {
        var callback = this.updateFeedsCallback;
        this.$.UpdateFeedsDialog.hide();
        this.updateFeedsCallback = null;
        if (callback) {
            callback(update);
        }
    },
    //offer to make Preware the app that opens .ipk files (once per launch).
    checkResourceHandler: function () {
        if (this.resourceHandlerChecked || !preware.PrefCookie.get().resourceHandlerCheck) {
            return;
        }
        this.resourceHandlerChecked = true;
        preware.ResourceHandler.check(function (result) {
            if (!result) {
                return;
            }
            if (result.action === "add") {
                this.$.ResourceHandlerDialog.set("body", $L("Preware 2 is not set up to open application packages (.ipk files) from the browser, email or app catalogs.<br><br><b>Would you like Preware 2 to open .ipk files?</b>"));
            } else {
                this.$.ResourceHandlerDialog.set("body", $L("Preware 2 is not the default application for .ipk files.<br>Current default: ") + result.active +
                    $L("<br><br><b>Would you like to make Preware 2 the default application?</b>"));
            }
            this.$.ResourceHandlerDialog.show();
        }.bind(this));
    },
    resourceHandlerAccepted: function () {
        this.$.ResourceHandlerDialog.hide();
        preware.ResourceHandler.fix(function (ok) {
            if (!ok) {
                enyo.error("Could not register Preware 2 as handler for .ipk files.");
            }
        });
        return true;
    },
    //don't ask again, can be turned back on with "Check .ipk association" in the preferences.
    resourceHandlerDeclined: function () {
        this.$.ResourceHandlerDialog.hide();
        preware.PrefCookie.put("resourceHandlerCheck", false);
        return true;
    },
    //a package needs luna/java/device restart after install/update/removal.
    handlePackageActionRequired: function (inSender, inEvent) {
        this.pendingAction = inEvent.callback;
        this.$.RestartDialog.set("body", inEvent.message);
        this.$.RestartDialog.show();
    },
    restartAccepted: function () {
        this.$.RestartDialog.hide();
        if (this.pendingAction) {
            this.pendingAction("ok");
        }
        this.pendingAction = null;
        return true;
    },
    restartDeclined: function () {
        this.$.RestartDialog.hide();
        if (this.pendingAction) {
            this.pendingAction("skip");
        }
        this.pendingAction = null;
        return true;
    },
    showSettingsDialog: function (inSender, inEvent) {
        this.$.SettingsDialog.updateValues();
        this.$.SettingsDialog.show();
    },
    showManageFeedsDialog: function (inSender, inEvent) {
        this.$.ManageFeedsDialog.show();
    },
    showLunaManagerDialog: function (inSender, inEvent) {
        this.$.LunaManagerDialog.show();
    },
    showSavedPackageList: function (inSender, inEvent) {
        this.$.AppPanels.$.packagesMenu.showSavedPackages();
    },
    showInstallPackageDialog: function (inSender, inEvent) {
        this.$.InstallPackageDialog.show();
    },
    //Utility Functions
    reverseDrag: function (inEvent) {
        inEvent.dx = -inEvent.dx;
        inEvent.ddx = -inEvent.ddx;
        inEvent.xDirection = -inEvent.xDirection;
        return inEvent;
    }
});
