/*jslint sloppy: true */
/*global enyo, preware, $L, console, setTimeout */
//shows a dialog that allows the user to select an IPK for install.

enyo.kind({
    name: "InstallPackageDialog",
    classes: "enyo-popup",
    style: "padding: 15px; width: 90%; height: 90%;",
    kind: "onyx.Popup",
    centered: true,
    modal: true,
    floating: true,
    autoDismiss: false,
    scrim: true,
    scrimWhenModal: false,
    ipkOperation: false,
    //another app started Preware just for this install: after it, Close closes Preware.
    closesApp: false,
    components: [
        {
            kind: "enyo.Panels",
            name: "Panels",
            realtimeFit: true,
            classes: "enyo-fit enyo-fill",
            draggable: false,
            arrangerKind: "CardArranger",
            components: [
                {  //panel to select the IPK.
                    name: "selectPanel",
                    kind: "FittableRows",
                    style: "padding: 15px;",
                    components: [
                        {tag: "div", classes: "webosstyle-groupbox", components: [
                        	{tag: "div", classes: "webosstyle-groupbox-header", content: $L("Package Location")},
                			{tag: "div", classes: "webosstyle-groupbox-body-single", style: "width: 100%", components:[
								{kind: "enyo.FittableColumns", noStretch: true, classes: "settings-item", components: [
									{kind: "onyx.InputDecorator", style: "width: 100%;", components: [
										{name: "ipkEdit", kind: "onyx.Input", classes: "enyo-fill", style: "text-align: left;color: white;", oninput: "validatePackageLocation", onfocus: "checkFocus", onblur: "checkBlur", placeholder: $L("http:// or file:// or ftp://")}
									]},
								]},                			
                			]},
                		]},
                		//package id and version, read from the file name
                		{name: "ipkSummary", allowHtml: true, showing: false, style: "color: white; margin: 0 0 12px 10px; line-height: 22px;"},
                		{ kind: "onyx.Button", content: $L("Browse"), style:"width: 100%;", classes: "onyx-dark", ontap: "browseFiles" },
                        {name: "getInfoButton", kind: "onyx.Button", content: $L("Get Info"), style:"width: 100%; margin-top: 5px;", classes: "onyx-dark", ontap: "getInfo", disabled: true },
                        {name: "installButton", kind: "onyx.Button", content: $L("Install"), style:"width: 100%; margin-top: 5px;", classes: "onyx-affirmative", ontap: "install", disabled: true },
                        {tag: "div", style:"margin-top: 10px; color: white; background-color: #444; border-color: #aaaaaa; border-style: solid; border-width: 1px 1px 1px 1px; padding: 10px; border-radius: 8px;", components:[
                        	{tag: "div", style:"font-weight: bold;", content:$L("Note:")},
                        	{tag: "div", content:$L("If this package needs a luna restart or device restart after installation, you will need to manually perform it when the installation is complete.")},
                        ]},
                        {tag: "div", fit: true},
                        {tag: "div", style:"width: 100%; text-align: center", components: [
        					{kind: "onyx.Button", classes: "onyx-dark", style: "margin:5px; width: 18%; min-width: 100px; font-size: 18px;", content: $L("Close"), ontap: "closePopup"}
        				]},
                    ]
                }, //end of selectPanel.
                {
                    name: "spinnerPanel",
                    kind: "enyo.FittableRows",
                    classes: "enyo-fill enyo-fit",
                    style: "border-radius: 8px;",
                    components: [
                        {tag: "div", classes: "center", style:"width: 40%; margin-top: 20%; min-width: 300px; color: white; background-color: #383838; border-color: #aaaaaa; border-style: solid; border-width: 1px; padding: 10px; border-radius: 8px;", components:[
							{ kind: "onyx.Spinner", style: "display: block; margin: 10% auto;" },
							{
								kind: "enyo.Scroller",
								classes: "enyo-fill center",
								components: [
									{ name: "message", classes: "enyo-fill center", style: "display: block; font-size: 14pt; margin-top: 8px; margin-bottom: 10px; width: 80%; max-width: 500px; max-height: 200px;",
											allowHtml: true, content: "Placeholder." }
								]
							},
                        ]},
                        {tag: "div", style:"width: 100%; text-align: center", components: [
        					{name: "spinnerBackBtn", kind: "onyx.Button", classes: "onyx-dark", style: "margin:5px; width: 18%; min-width: 100px; font-size: 18px;", showing: "false", content: $L("Back"), ontap: "backFromSpinner"}
        				]}
                    ]
                }, //end pf spinnerPanel
                { //infoPanel shows package info
                    name: "infoPanel",
                    kind: "enyo.FittableRows",
                    classes: "enyo-fill enyo-fit",
                    style: "border-radius: 8px;",
                    components: [
                        {kind: "enyo.FittableColumns", style: "width: 100%; background-color: #383838; border-radius: 8px 8px 0 0; padding: 5px;", components: [
                        	{name: "pkgInfoIcon", kind: "Image", style: "width: 48px; height: 48px;", onerror: "infoIconError"},
                        	{name: "pkgInfoTitle", tag: "span", style: "position: relative; top: -10; color: #ffffff; padding-left: 10px; font-size: 28px;", content: "Hello World"},
                        ]},
                        {
                            kind: "enyo.Scroller",
                            fit: true,
                            components: [
                            	{name: "pkgInfoDescriptionContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Description")},
                            		{name: "pkgInfoDescription", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoHomepageContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Homepage")},
                            		{name: "pkgInfoHomepage", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoMaintainerContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Maintainer")},
                            		{name: "pkgInfoMaintainer", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoVersionContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Version")},
                            		{name: "pkgInfoVersion", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoLastUpdatedContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Last Updated")},
                            		{name: "pkgInfoLastUpdated", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoDownloadSizeContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Download Size")},
                            		{name: "pkgInfoDownloadSize", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoInstalledVersionContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Installed Version")},
                            		{name: "pkgInfoInstalledVersion", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoInstalledDateContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Installed")},
                            		{name: "pkgInfoInstalledDate", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoInstalledSizeContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Installed Size (KB)")},
                            		{name: "pkgInfoInstalledSize", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoIDContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("ID")},
                            		{name: "pkgInfoID", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoLicenseContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("License")},
                            		{name: "pkgInfoLicense", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoTypeContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Type")},
                            		{name: "pkgInfoType", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoCategoryContainer", tag: "div", classes:"package-info-container", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Category")},
                            		{name: "pkgInfoCategory", tag: "div", classes:"package-info-content"},
                            	]},
                            	{name: "pkgInfoFeedContainer", tag: "div", classes:"package-info-container", style: "border-bottom: none", components: [
                            		{tag: "div", classes:"package-info-title", content: $L("Feed")},
                            		{name: "pkgInfoFeed", tag: "div", classes:"package-info-content"},
                            	]},
                            ]
                        },
                        {tag: "div", style:"width: 100%; text-align: center", components: [
        					{name: "infoBackButton", kind: "onyx.Button", classes: "onyx-dark", style: "margin:5px; width: 18%; min-width: 100px; font-size: 18px;", content: $L("Back"), ontap: "backFromSpinner"},
        					{name: "infoInstallButton", kind: "onyx.Button", classes: "onyx-affirmative", style: "margin:5px; width: 18%; min-width: 100px; font-size: 18px;", content: $L("Install"), ontap: "install"}
        				]}
                    ]
                },
                {
                    name: "filePickerPanel",
                    classes: "enyo-fill",
                    components: [
                        {
                            kind: "preware.FilePicker",
                            name: "filePicker",
                            showing: false,
                            //fp options
                            type: "file",
                            root: preware.PrefCookie.get().browseFromRoot,
                            folder: "/media/internal",
                            extensions: ["ipk"],
                            onSelect: "handleSelect",
                            pop: false
                        }
                    ]
                }
            ]
        }, //end of panels

        //non ui stuff:
        {
            kind: "Signals",
            onbackbutton: "handleBackGesture",
            onBackendSimpleMessage: "installDone",
            onPackageProgressMessage: "statusMessage"
        }
    ],

    //global variables:
    uri: "", //absolute location of ipk file
    filename: "", //filename of package without path.

    //panel indices:
    selectPanelIndex: 0,
    spinnerPanelIndex: 1,
    infoPanelIndex: 2,
    filePickerPanelIndex: 3,

    create: function (inSender, inEvent) {
        this.inherited(arguments);
    },
    doInstall: function (installUrl) {
        enyo.warn("In InstallPackageDialog: " + installUrl);
        //Opened with a package (e.g. by an app catalog): show it, ready to install,
        //like the original Preware did. No automatic "Get Info": that downloads the
        //whole package just to read its details (a minute for a big game), and Install
        //downloads it again.
        this.$.Panels.setIndex(this.selectPanelIndex);
        this.$.ipkEdit.setValue(installUrl);
        this.validatePackageLocation();
    },
    //show the package id and version, when the file name follows <id>_<version>_<arch>.ipk
    updateSummary: function () {
        var name = decodeURIComponent(this.$.filePicker.getFileName(this.$.ipkEdit.getValue() || "")),
            match = /^([^_\/]+)_([^_\/]+)_[^_\/]+\.ipk$/i.exec(name);
        if (match) {
            this.$.ipkSummary.setContent($L("Package: ") + enyo.dom.escape(match[1]) + "<br>" + $L("Version: ") + enyo.dom.escape(match[2]));
        }
        if (this.$.ipkSummary.showing !== !!match) {
            this.$.ipkSummary.setShowing(!!match);
            this.$.selectPanel.resize(); //keep the Close button in view
        }
    },
    //handlers
    handleBackGesture: function (inSender, inEvent) {
        if (!this.getShowing() || this.$.filePickerPanel.getShowing()) {
            return; //don't process back gesture if not visible.
        }

        var index = this.$.Panels.getIndex();
        if (index === this.infoPanelIndex) {
            this.$.Panels.setIndex(this.selectPanelIndex);
        }
        if (!this.ipkOperation && index === this.spinnerPanelIndex) {
            this.$.Panels.setIndex(this.selectPanelIndex);
        }

        if (!this.ipkOperation && index === this.selectPanelIndex) {
            this.closePopup();
        }

        inEvent.preventDefault();
    },
    backFromSpinner: function (inSender, inEvent) {
        if (!this.ipkOperation) {
            if (inSender === this.$.spinnerBackBtn && this.closesApp) {
                this.closePopup(); //closes Preware too (App.installDialogHidden)
                return;
            }
            this.$.Panels.setIndex(this.selectPanelIndex);
        }
    },
    closePopup: function (inSender, inEvent) {
        if (!this.ipkOperation) {
			this.$.ipkEdit.setValue("");
			this.$.ipkSummary.hide();
			this.$.getInfoButton.setDisabled(true);
			this.$.installButton.setDisabled(true);
            this.hide();
        }
    },
    browseFiles: function (inSender, inEvent) {
        this.$.Panels.setIndex(this.filePickerPanelIndex);
        this.$.filePicker.show();
        this.$.filePicker.folderChanged(); //reload directory.
    },
    getInfo: function (inSender, inEvent) {
        this.uri = this.$.ipkEdit.getValue();
        this.filename = this.$.filePicker.getFileName(this.uri); //this extracts a filename from the uri, i.e. the last part.
                                                                 //ipk gets downloaded to /media/internal/.developer/$filename
        preware.IPKGService.extractControl(this.gotInfo.bind(this), this.filename, this.uri);
        this.$.Panels.setIndex(this.spinnerPanelIndex);
        this.$.message.setContent("Getting info for " + this.filename);
        this.originalMessage = "Getting info for " + this.filename;
        this.ipkOperation = true;
        this.$.spinner.show();
        this.$.spinnerBackBtn.hide();
    },
    install: function (inSender, inEvent) {
        this.uri = this.$.ipkEdit.getValue();
        this.filename = this.$.filePicker.getFileName(this.uri);
        var packageId = this.filename.split("_", 1)[0],
            packageModel = new preware.PackageModel("", {type: 'Package', pkg: packageId, title: packageId, filename: this.filename, location: this.uri});
        console.log("PackageId: " + packageId);

        this.$.Panels.setIndex(this.spinnerPanelIndex);
        this.$.message.setContent("Installing " + packageId);
        this.originalMessage = "Installing " + packageId;
        packageModel.doInstall();
        this.ipkOperation = true;
        this.$.spinner.show();
        this.$.spinnerBackBtn.hide();
    },
    handleSelect: function (inSender, inEvent) {
        if (!this.ipkOperation) {
            this.$.filePicker.hide();
            if (inEvent.success) {
                this.$.ipkEdit.setValue("file://" + inEvent.location);
                this.validatePackageLocation();
            }
            this.$.Panels.setIndex(this.selectPanelIndex);
        }
    },
	validatePackageLocation: function (inSender, inEvent) {
		this.updateSummary();
		if (this.$.ipkEdit.getValue() != "")
		{
			this.$.getInfoButton.setDisabled(false);
			this.$.installButton.setDisabled(false);
		}
		else
		{
			this.$.getInfoButton.setDisabled(true);
			this.$.installButton.setDisabled(true);
		}
	},
	
    //results:
    gotInfo: function (payload) {
        console.log("control extracted: " + JSON.stringify(payload));
        if (payload.stage) {
            if (payload.stage === "completed") {
                var infoObj = this.parsePackage(payload.info);
                infoObj.type = "Package";
                infoObj.filename = this.filename;
                infoObj.location = this.uri;

                this.ipkOperation = false;
                
                if (infoObj) {
                    this.refreshInfoPanel(infoObj);
                    this.$.Panels.setIndex(this.infoPanelIndex);
                    return;
                }
            } else if (payload.stage === "status") {
                this.$.message.setContent(this.originalMessage + "<br />" + payload.status);
            } else if (payload.stage === "failed") {
                this.$.message.setContent("Error: " + payload.errorText);
                this.ipkOperation = false;
                setTimeout(function () { this.$.Panels.setIndex(this.selectPanelIndex);  }.bind(this), 2000);
            }
        }
    },
    
    infoIconError: function (inSender) {
        inSender.hide();
        return true;
    },
    installDone: function (inSender, inEvent) {
        this.$.message.setContent(this.originalMessage + "<br /><bold>Done:</bold> " + inEvent.message);
        this.ipkOperation = false;
        this.$.spinner.hide();
        this.$.spinnerBackBtn.setContent(this.closesApp ? $L("Close") : $L("Back"));
        this.$.spinnerBackBtn.show();
    },
    
    statusMessage: function (inSender, inEvent) {
        this.$.message.setContent(this.originalMessage + "<br />" + inEvent.message);
    },
    
    refreshInfoPanel: function (infoObj) {
        if (infoObj.Description)
        {
        	this.$.pkgInfoTitle.setContent(infoObj.Description);
        }
        else
        {
        	this.$.pkgInfoTitle.setContent("N/A");
        }
        
        this.$.pkgInfoIcon.setSrc(infoObj.Icon || "");
        this.$.pkgInfoIcon.setShowing(!!infoObj.Icon);
        
        if (infoObj.Description)
        {
        	this.$.pkgInfoDescriptionContainer.show();
        	this.$.pkgInfoDescription.setContent(infoObj.Description);
        }
        else
        {
        	this.$.pkgInfoDescriptionContainer.hide();
        }
       
        if (infoObj.Homepage)
        {
        	this.$.pkgInfoHomepageContainer.show();
        	this.$.pkgInfoHomepage.setContent(infoObj.Homepage);
        }
        else
        {
        	this.$.pkgInfoHomepageContainer.hide();
        }

        if (infoObj.Maintainer && infoObj.Maintainer[0].name)
        {
        	this.$.pkgInfoMaintainerContainer.show();
        	this.$.pkgInfoMaintainer.setContent(infoObj.Maintainer[0].name);
        }
        else
        {
        	this.$.pkgInfoMaintainerContainer.hide();
        }

    	if (infoObj.Version)
        {
        	this.$.pkgInfoVersionContainer.show();
        	this.$.pkgInfoVersion.setContent(infoObj.Version);
        }
        else
        {
        	this.$.pkgInfoVersionContainer.hide();
        }

    	if (infoObj.date)
        {
        	this.$.pkgInfoLastUpdatedContainer.show();
        	this.$.pkgInfoLastUpdated.setContent(infoObj.date);
        }
        else
        {
        	this.$.pkgInfoLastUpdatedContainer.hide();
        }        

        if (infoObj.Size)
        {
        	this.$.pkgInfoDownloadSizeContainer.show();
        	this.$.pkgInfoDownloadSize.setContent(infoObj.Size);
        }
        else
        {
        	this.$.pkgInfoDownloadSizeContainer.hide();
        }

		if (infoObj.VersionInstalled)
        {
        	this.$.pkgInfoInstalledVersionContainer.show();
        	this.$.pkgInfoInstalledVersion.setContent(infoObj.VersionInstalled);
        }
        else
        {
        	this.$.pkgInfoInstalledVersionContainer.hide();
        }

        if (infoObj['Installed-Time'])
        {
        	this.$.pkgInfoInstalledDateContainer.show();
        	this.$.pkgInfoInstalledDate.setContent(infoObj['Installed-Time']);
        }
        else
        {
        	this.$.pkgInfoInstalledDateContainer.hide();
        }

        if (infoObj['Installed-Size'])
        {
        	this.$.pkgInfoInstalledSizeContainer.show();
        	this.$.pkgInfoInstalledSize.setContent(infoObj['Installed-Size']);
        }
        else
        {
        	this.$.pkgInfoInstalledSizeContainer.hide();
        }

        if (infoObj.Package)
        {
        	this.$.pkgInfoIDContainer.show();
        	this.$.pkgInfoID.setContent(infoObj.Package);
        }
        else
        {
        	this.$.pkgInfoIDContainer.hide();
        }

        if (infoObj.License)
        {
        	this.$.pkgInfoLicenseContainer.show();
        	this.$.pkgInfoLicense.setContent(infoObj.License);
        }
        else
        {
        	this.$.pkgInfoLicenseContainer.hide();
        }

        if (infoObj.type)
        {
        	this.$.pkgInfoType.setContent(infoObj.type);
        }
        else
        {
           	this.$.pkgInfoType.setContent($L("Package"));
        }

        if (infoObj.Category)
        {
        	this.$.pkgInfoCategory.setContent(infoObj.Category);
        }
        else
        {
           	this.$.pkgInfoCategory.setContent($L("Unsorted"));
        }

        if (infoObj.Feed)
        {
        	this.$.pkgInfoFeed.setContent(infoObj.Feed);
        }
        else
        {
           	this.$.pkgInfoFeed.setContent($L("Unknown"));
        }                
    },
    
    //processing
    parsePackage: function (rawData, url) {
        var test, lineRegExp, curPkg, x, match;
        try {
            if (rawData) {
              	test = rawData;
                lineRegExp = new RegExp(/[\s]*([^:]*):[\s]*(.*)[\s]*$/);
                curPkg = false;

                for (x = 0; x < test.length; x += 1) {
                    match = lineRegExp.exec(test[x]);
                    if (match) {
                        if (match[1] === 'Package' && !curPkg) {
                            curPkg = {
                                Size: 0,
                                Status: '',
                                Architecture: '',
                                Section: '',
                                Package: '',
                                Filename: '',
                                Depends: '',
                                Maintainer: '',
                                Version: '',
                                Description: '',
                                MD5Sum: '',
                                'Installed-Time': 0,
                                'Installed-Size': 0,
                                Source: ''
                            };
                        }
                        if (match[1] && match[2]) {
                            curPkg[match[1]] = match[2];
                        }
                    } else {
						throw $L("Failed to Parse Package");
                    }
                }
                
                // parse maintainer
                var maintainer;
				if (curPkg.Maintainer) {
					maintainer = [];
					splitRes = curPkg.Maintainer.split(',');
					r = new RegExp("^([^<]*)<([^>]*)>?"); // this one is win
					for (i = 0; i < splitRes.length; i += 1) {
						match = splitRes[i].trim().match(r);
						if (match) {
							tmp = {name: match[1].trim(), url: match[2]};
							if (tmp.url.indexOf('@') > -1) {
								// remove stupid default palm address for palm-package'd apps
								if (tmp.url === 'palm@palm.com' ||        // v1.1 style
										tmp.url === 'nobody@example.com') {// v1.2 style
									tmp.url = false;
								} else {
									tmp.url = 'mailto:' + tmp.url + '?subject=' + this.title;
								}
							}
							maintainer.push(tmp);
						} else {
							maintainer.push({name: splitRes[i].trim(), url: false});
						}
					}
					curPkg.Maintainer = maintainer;
				}    
            }
        } catch (e) {
            console.error('error in packagesModel#parsePackages: ' + e);
            this.$.message.setContent("Error: " + e.errorText);
            this.ipkOperation = false;
            setTimeout(function () { this.$.Panels.setIndex(this.selectPanelIndex);  }.bind(this), 2000);
        }
        
        return curPkg;
    },
    
    //utility
    checkFocus: function(source, event) {
		source.applyStyle("color", "black");
	},

	checkBlur: function(source, event) {
		source.applyStyle("color", "white");
	}
});
