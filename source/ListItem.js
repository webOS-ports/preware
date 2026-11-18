
//A row of a list: an optional icon, a title and an optional count.
//It is one element whose HTML holds the icon, title and count, with no child
//controls and no bindings: the List of Everything has hundreds of rows, and
//each row with child controls and bindings took about 9 ms to create on a
//TouchPad (this takes well under 1 ms).
enyo.kind({
    name: "ListItem",
    classes: "list-item",
    ontap: "menuItemTapped",
    allowHtml: true,
    published: {
        title: "[list item]",
        icon: false,
        count: -1
    },
    handlers: {
        ondown: "pressed",
        ondragstart: "released",
        onup: "released",
        onleave: "released"
    },
    iconSrc: "",
    create: function() {
        this.inherited(arguments);
        this.addRemoveClass("list-item-has-icon", !!this.icon);
        this.update();
    },
    titleChanged: function() {
        this.update();
    },
    countChanged: function() {
        this.update();
    },
    //show the package icon, or no icon at all if there is none.
    setIcon: function(src) {
        this.iconSrc = src || "";
        this.update();
    },
    //title and icon at once (package lists): builds the row once.
    setTitleAndIcon: function(title, src) {
        this.title = title;
        this.setIcon(src);
    },
    //the title keeps its indent (list-item-has-icon) with or without an icon, so
    //all titles of a package list line up. Many feed icons are hosted on sites
    //that are gone: a broken icon hides itself.
    update: function() {
        var html = "";
        if (this.iconSrc) {
            html += "<img src=\"" + enyo.dom.escape(this.iconSrc) + "\" style=\"display: inline-block; height: 100%; margin-right: 8px;\"" +
                " onerror=\"this.style.display='none'\">";
        }
        html += "<div class=\"list-item-title\" style=\"display: inline-block; position: absolute; margin-top: 6px;\">" +
            enyo.dom.escape(String(this.title)) + "</div>";
        if (this.count >= 0) {
            html += "<div class=\"item-count\">" + enyo.dom.escape(String(this.count)) + "</div>";
        }
        this.setContent(html);
    },
    //a divider above the item (sorted package lists: Today, A...), or none
    //(""). Drawn by CSS (list-item-divided), so a long list needs no more controls.
    setDivider: function(text) {
        this.setAttribute("data-divider", text || null);
        this.addRemoveClass("list-item-divided", !!text);
    },
    pressed: function() {
        this.addClass("onyx-selected");
    },
    released: function() {
        this.removeClass("onyx-selected");
    }
});
