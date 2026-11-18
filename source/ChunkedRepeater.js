/*jslint sloppy: true */
/*global enyo */

//A Repeater for long lists (package lists, search results) that builds its
//rows as they are needed, like the original Preware's lists (Mojo's List
//widget draws only the rows near the screen): the first rows at once, more
//when the list is scrolled near the end of the rows built (moreIfNear, called
//from the scroller's onScroll). Building all rows of the List of Everything
//(hundreds) took over 3 seconds on a TouchPad, with the app frozen.
//
//Until all rows are built, getClientControls() returns fewer controls than
//count.
enyo.kind({
    name: "preware.ChunkedRepeater",
    kind: "enyo.Repeater",
    //rows built at first: more than a TouchPad screen holds
    firstRows: 30,
    //rows added at a time
    chunkRows: 30,
    built: 0,
    build: function () {
        this.buildFirst(this.firstRows);
    },
    buildFirst: function (rows) {
        this.destroyClientControls();
        this.built = 0;
        this.buildRows(Math.max(rows, this.firstRows));
        this.render();
    },
    //count rows again, with as many built as before: an open list keeps its
    //place (after an install or removal).
    rebuild: function (count) {
        var rows = this.built;
        this.count = count;
        this.buildFirst(rows);
    },
    buildRows: function (n) {
        var end = Math.min(this.count, this.built + n), c;
        while (this.built < end) {
            c = this.createComponent({kind: "enyo.OwnerProxy", index: this.built});
            c.createComponents(this.itemComponents);
            this.doSetupItem({index: this.built, item: c});
            this.built += 1;
        }
    },
    //adds rows when scroller shows less than three screens before the end of
    //the rows built.
    moreIfNear: function (scroller) {
        var b;
        if (this.built >= this.count || !this.hasNode()) {
            return;
        }
        b = scroller.getScrollBounds();
        if (b.top + 3 * b.clientHeight >= b.height) {
            this.addRows(this.chunkRows);
        }
    },
    //the new rows go into the page as one piece of HTML: rendered one at a
    //time, each took longer as the list grew (seconds per few dozen rows on a
    //TouchPad).
    addRows: function (n) {
        var start = this.built, node = this.hasNode(), items, html = "", i,
            delegate = enyo.Control.renderDelegate;
        this.buildRows(n);
        if (node) {
            items = this.getClientControls();
            for (i = start; i < this.built; i += 1) {
                html += delegate.generateHtml(items[i]);
            }
            node.insertAdjacentHTML("beforeend", html);
            for (i = start; i < this.built; i += 1) {
                items[i].rendered();
            }
        }
    }
});
