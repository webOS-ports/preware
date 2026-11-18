/*jslint sloppy: true, regexp: true */
/*global enyo, preware */
/*
 * filePicker
 *
 * usage:
 * var f = new filePicker({
 *
 *        type: 'file',                // or folder, it changes the interface and what it returns
 *                                         defaults to file
 *
 *        onSelect: function,            // function that will be called upon completion,
 *                                         it will be passed either a file/folder based on type or false for a cancel
 *                                         this is the only required parameter
 *
 *        root: false,                //    weather or not to allow access to root
 *
 *        folder: '/media/internal/',    // initial folder location, notice the trailing slash!
 *
 *        extensions: ['jpg','png'],    // (file type only) array of extensions to list (lowercase extensions only)
 *                                    // ['ext'] for single extension
 *                                    // [] for all extensions (DEFAULT)
 * });
 *
 *
 */
enyo.kind({
    name: "preware.FilePicker",
    classes: "enyo-fill enyo-fit",
    kind: "FittableRows",
    fit: true,
    published: {
        num: 0,
        type: 'file',
        root: false,
        topLevel: this.root ? '/' : '/media/internal/',
        folder: this.topLevel,
        extensions: []
    },
    currentContent: [],
    folderTree: [],
    events: {
        onListingDone: "", //inEvent will have results: [array of files], success: true / false, directory: string (orignal directory)
        onSelect: ""       //inEvent will have value: selected value, success: true / false
    },
    statics: {
        num: 0,
        folderRegExp: new RegExp(/^\./),
        fileRegExp: new RegExp('^(.+)/([^/]+)$'),
        extensionRegExp: new RegExp(/\.([^\.]+)$/)
    },
    components: [
        {kind: "onyx.Toolbar", style: "box-sizing: border-box; border-top-left-radius: 8px; border-top-right-radius: 8px;", components: [
            {classes: "top-title", content: "Files"}
        ]},
        {
            kind: "Scroller",
            horizontal: "hidden",
            classes: "enyo-fill enyo-fit",
            style: "background-color: #444; position: relative;",
            touch: true,
            fit: true,
            components: [
                {name: "ContentRepeater", kind: "enyo.Repeater", classes: "enyo-fill enyo-fit", fit: true, onSetupItem: "setupContentItem", count: 0, components: [
                    {kind: "ListItem", title: "[file]", icon: true, ontap: "contentTapped"}
                ]}
            ]
        },
        {
            kind: "onyx.Toolbar",
            style: "box-sizing: border-box; width: 100%; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px;",
            components: [
                {name: "CancelButton", classes: "center", kind: "onyx.Button", content: "Cancel", ontap: "cancel"}
            ]
        },
        {
            kind: "enyo.Scrim",
            name: "scrim",
            classes: "enyo-scrim-translucent enyo-scrim enyo-fit",
            style: "text-align: center;",
            floating: true,
            components: [
                { kind: "onyx.Spinner", style: "margin: 20% auto;", name: "spinner" }
            ]
        },
        {
            kind: "Signals",
            onbackbutton: "handleBackGesture"
        }
    ],


    create: function () {
        this.inherited(arguments);

        this.folderChanged();
    },

    //handlers:
    folderChanged: function (oldValue) {
        if (this.folder.charAt(this.folder.length - 1) !== "/") {
            this.folder += "/";
        }

        if (oldValue) {
            this.folderTree.push(oldValue);
        }
        this.getDirectory(this.folder);
    },
    contentTapped: function (inSender, inEvent) {
        var index = inEvent.index,
            item  = this.currentContent[index] || {};
        if (item.type === "directory") {
            this.setFolder(item.location);
            if (item.name === "..") {
                this.folderTree.pop(); //pop newly added child
                this.folderTree.pop(); //pop now active parent
            }
        } else {
            item.success = true;
            this.doSelect(item);
        }
    },
    setupContentItem: function (inSender, inEvent) {
        var index = inEvent.index,
            item = inEvent.item;
        item.$.listItem.set("title", this.currentContent[index].name);
        return true;
    },
    handleBackGesture: function (inSender, inEvent) {
        if (this.showing) { //don't process back handler if not showing
            if (this.folderTree.length > 0) {
                this.setFolder(this.folderTree.pop()); //pop parent
                this.folderTree.pop(); //pop newly added child.
            } else {
                this.cancel();
            }
            return true;
        }
    },

    //auxillary functions:
    getDirectory: function (dir) {
        if (this.hasNode()) {
            this.$.scrim.showAtZIndex(130);
            this.$.spinner.start();
        }
        preware.IPKGService.getDirListing(this.parseDirectory.bind(this, dir), dir);
    },
    parseDirectory: function (dir, payload) {
        var returnArray = [], c;
        if (this.folderTree.length > 0) {
            returnArray.push({name: "..", location: this.folderTree[this.folderTree.length - 1], type: "directory"});
        }
        if (payload.contents && payload.contents.length > 0) {
            for (c = 0; c < payload.contents.length; c += 1) {
                if (!payload.contents[c].name.match(preware.FilePicker.folderRegExp)
                         && ((this.validExtension(payload.contents[c].name) && payload.contents[c].type === 'file') || payload.contents[c].type !== 'file')) {
                    returnArray.push({
                        name: payload.contents[c].name,
                        type: payload.contents[c].type,
                        location: dir + payload.contents[c].name
                    });
                }
            } //end for
        } //end if payload.contents

        //sort array
        if (returnArray.length > 0) {
            returnArray.sort(function (a, b) {
                var strA, strB;
                if (a.type === "directory" && b.type !== "directory") {
                    return -1;
                }
                if (b.type === "directory" && a.type !== "directory") {
                    return 1;
                }
                if (a.name && b.name) {
                    strA = a.name.toLowerCase();
                    strB = b.name.toLowerCase();
                    return ((strA < strB) ? -1 : ((strA > strB) ? 1 : 0));
                }
                return -1;
            });
        }

        this.currentContent = returnArray;
        this.$.ContentRepeater.setCount(this.currentContent.length);
        this.$.scrim.hideAtZIndex(130);
        this.$.spinner.stop();
        this.reflow();

        this.doListingDone({results: returnArray, directory: dir, success: true});
    },
    getDirectories: function (dir) {
        preware.IPKGService.getDirListing(this.parseDirectories.bind(this, dir), dir);
    },
    parseDirectories: function (dir, payload) {
        var returnArray = [], c;
        if (payload.contents.length > 0) {
            for (c = 0; c < payload.contents.length; c += 1) {
                if (!payload.contents[c].name.match(preware.FilePicker.folderRegExp) && payload.contents[c].type === 'directory') {
                    returnArray.push({
                        name: payload.contents[c].name,
                        type: payload.contents[c].type,
                        location: dir + payload.contents[c].name
                    });
                }
            } //end for
        } //end if payload.contents

        //sort array
        if (returnArray.length > 0) {
            returnArray.sort(function (a, b) {
                var strA, strB;
                if (a.name && b.name) {
                    strA = a.name.toLowerCase();
                    strB = b.name.toLowerCase();
                    return ((strA < strB) ? -1 : ((strA > strB) ? 1 : 0));
                }
                return -1;
            });
        }

        this.currentContent = returnArray;
        this.$.ContentRepeater.setCount(returnArray.length);

        this.doListingDone({results: returnArray, directory: dir, success: true});
    },
    ok: function (item) {
        item.success = true;
        this.doSelect(item);
    },
    cancel: function () {
        this.doSelect({success: false});
    },
    validExtension: function (name) {
        var match, i;
        if (this.extensions.length > 0) {
            match = preware.FilePicker.extensionRegExp.exec(name);
            if (match && match.length > 1) {
                for (i = 0; i < this.extensions.length; i += 1) {
                    if (this.extensions[i] === match[1].toLowerCase()) {
                        return true;
                    }
                }
            }
            //no extension did match or no extension at all => false
            return false;
        }
        //have no extensions filter, so everything is accepted.
        return true;
    },
    parseFileString: function (f) {
        return f.replace(/\/media\/internal\//i, 'USB/');
    },
    parseFileStringForId: function (p) {
        return p.toLowerCase().replace(/\//g, '-').replace(/ /g, '-').replace(/\./g, '-');
    },
    getFileName: function (p) {
        var match = preware.FilePicker.fileRegExp.exec(p);
        if (match && match.length > 1) {
            return match[2];
        }
        return p;
    }
});
