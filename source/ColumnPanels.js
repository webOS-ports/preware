/*jslint sloppy: true */
/*global enyo, preware */

// Static column layout used on legacy webOS instead of enyo.Panels.
//
// The first panel (the menu) always stays visible on the left, and the active panel
// opens in a column to its right. When there is room, the panel before the active one
// is shown next to it (e.g. the package list next to the package details).
// Panels are simply shown and hidden: no sliding or scaling transitions, which the
// TouchPad's WebKit renders badly. Too narrow for columns (phones), only the active
// panel is shown.
//
// Implements the part of the enyo.Panels API that AppPanels uses.
enyo.kind({
    name: "preware.ColumnPanels",
    kind: "enyo.Control",
    classes: "preware-column-panels",
    published: {
        index: 0,
        //kept for enyo.Panels compatibility, not used.
        draggable: false,
        arrangerKind: ""
    },
    events: {
        onTransitionFinish: ""
    },
    handlers: {
        onGrabberTap: "grabberTapped"
    },
    //width of the always visible first column.
    menuWidth: 280,
    //width of the column left of the active one, when two are shown.
    listWidth: 320,
    //the right area must be at least this wide to show two columns.
    twoColumnWidth: 700,
    //below this width only one panel is shown at a time.
    columnModeWidth: 700,

    //Overwrite to hide panels that are not part of the current path.
    isPanelSkipped: function (index) {
        return false;
    },

    getPanels: function () {
        var p = this.controlParent || this;
        return p.children;
    },
    getActive: function () {
        return this.getPanels()[this.index];
    },
    //lay out again even if the index stays the same, the skipped panels may have changed.
    setIndex: function (index) {
        if (index === this.index) {
            this.layoutColumns();
        } else {
            this.set("index", index);
        }
        return this;
    },
    indexChanged: function () {
        this.layoutColumns();
        this.doTransitionFinish({fromIndex: this.index, toIndex: this.index});
    },
    rendered: function () {
        this.inherited(arguments);
        this.layoutColumns();
    },
    reflow: function () {
        this.inherited(arguments);
        this.layoutColumns();
    },
    handleResize: function () {
        this.inherited(arguments);
        this.layoutColumns();
    },
    //true if the menu is shown next to the other panels.
    isColumnMode: function () {
        return this.hasNode() && this.node.clientWidth >= this.columnModeWidth;
    },
    //the panel shown left of panel index, or -1.
    previousPanel: function (index) {
        var i;
        for (i = index - 1; i > 0; i -= 1) {
            if (!this.isPanelSkipped(i)) {
                return i;
            }
        }
        return -1;
    },
    //which panels to show, from left to right, when panel index (default: the
    //current one) is active.
    visiblePanels: function (width, index) {
        var prev;
        if (index === undefined) {
            index = this.index;
        }
        if (width < this.columnModeWidth) {
            return [index];
        }
        if (index === 0) {
            return [0];
        }
        prev = this.previousPanel(index);
        if (prev > 0 && width - this.menuWidth >= this.twoColumnWidth) {
            return [0, prev, index];
        }
        return [0, index];
    },
    //true if, with panel index active, the panel left of it is shown next to it.
    showsColumnBefore: function (index) {
        return this.hasNode() && this.visiblePanels(this.node.clientWidth, index).length > 2;
    },
    layoutColumns: function () {
        if (!this.hasNode()) {
            return;
        }
        var panels = this.getPanels(),
            width = this.node.clientWidth,
            visible = this.visiblePanels(width),
            left = 0,
            i,
            p,
            w,
            pos;
        for (i = 0; i < panels.length; i += 1) {
            pos = visible.indexOf(i);
            p = panels[i];
            if (pos < 0) {
                p.setShowing(false);
                continue;
            }
            if (visible.length === 1) {
                w = (i === 0 && width >= this.columnModeWidth) ? this.menuWidth : width;
            } else if (i === 0) {
                w = this.menuWidth;
            } else if (pos === visible.length - 1) {
                w = width - left; //the last column gets the rest.
            } else {
                w = this.listWidth;
            }
            p.addClass("preware-column");
            p.addRemoveClass("preware-column-divider", pos > 0);
            p.applyStyle("left", left + "px");
            p.applyStyle("width", w + "px");
            p.setShowing(true);
            p.resize();
            left += w;
        }
    },
    //a grabber closes the columns right of the one it is in (in column mode),
    //otherwise it goes back one panel.
    grabberTapped: function (inSender, inEvent) {
        var panels = this.getPanels(), c = inEvent.originator, i, prev;
        while (c && c.parent !== this) {
            c = c.parent;
        }
        i = panels.indexOf(c);
        if (this.isColumnMode() && i > 0 && i < this.index) {
            this.setIndex(i);
            return true;
        }
        return false; //let the grabber send the back gesture.
    },

    //enyo.Panels compatibility (drag gestures are not used here).
    dragstartTransition: function () {},
    dragTransition: function () {},
    dragfinishTransition: function () {}
});
