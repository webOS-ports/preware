enyo.kind({
    name: "GrabberToolbar",
    kind: "onyx.Toolbar",
    classes: "preware-grabber-toolbar",
    published: {
        showGrabber: true
    },
    components:[
        //shown on every device: on the TouchPad (no gesture area) it is the way back
        {name: "grabberArea", classes: "preware-grabber-area", ontap: "grabberTapped", components: [
            {kind: "onyx.Grabber"}
        ]}
    ],
    create: function () {
        this.inherited(arguments);
        this.showGrabberChanged();
    },
    showGrabberChanged: function () {
        this.$.grabberArea.setShowing(this.showGrabber);
    },
    //tapping the grabber goes back to the previous panel, like the back gesture.
    grabberTapped: function() {
        enyo.Signals.send("onbackbutton", {preventDefault: function () {}, stopPropagation: function () {}});
        return true;
    }
});
