/*jslint sloppy: true */
/*global enyo, onyx, preware, $L, console */
//shows a dialog that allows the user to change feed settings.

enyo.kind({
    name: "ManageFeedsDialog",
    classes: "enyo-popup",
    style: "padding: 15px; width: 90%; height: 90%;",
    kind: "onyx.Popup",
    centered: true,
    modal: true,
    floating: true,
    autoDismiss: false,
    scrim: true,
    scrimWhenModal: false,
    
    components: [
        {name: "ManageFeedsList", kind: "AroundList", fit: true, count: 0, style:"width: 100%;", touch: true, enableSwipe: true, percentageDraggedThreshold: 0.01, persistSwipeableItem: true, onSetupItem: "setupItem", onSetupSwipeItem: "setupSwipeItem",
        components: [
			{name: "feedWrapper", tag: "div", classes: "webosstyle-list-groupbox-item", components: [
				{name: "feed", kind: "preware.FeedItem", classes: "managefeedslist-feed-item webosstyle-list-groupbox-item-content", onToggle: "feedEnabledToggled"}
			]}
		],
		swipeableComponents: [
			{name: "swipableFeed", style: "height: 100%; margin-left: 5px; margin-right: 5px; background-color: darkgrey; text-align:center", components: [
				{name: "swipeableDeleteButton", kind: "onyx.Button", style: "margin-top: 10px; margin-right: 10px;", classes:"onyx-negative", ontap: "deleteButtonTapped", content: $L("Delete")},
				{name: "swipeableCancelButton", kind: "onyx.Button", style: "margin-left: 10px;", ontap: "cancelButtonTapped", content: $L("Cancel")}
			]}
		],
		aboveComponents: [
			{tag: "div", classes: "webosstyle-groupbox", components: [
                {tag: "div", classes: "webosstyle-groupbox-header", content: $L("New Feed")},
                {tag: "div", classes: "webosstyle-groupbox-body", style: "width: 100%", components:[
					{kind: "enyo.FittableColumns", noStretch: true, classes: "settings-item", components: [
						{kind: "onyx.InputDecorator", classes: "flex-row", style: "width: 100%;", components: [
							{name: "newFeedName", kind: "onyx.Input", classes: "flex-auto", style: "color: white", onfocus: "checkFocus", onblur: "checkBlur" },
							{name: "newFeedNameLabel", content: $L("Name"), classes: "flex-none", style: "padding-left: 5px; color: SkyBlue;"}
						]},
					]},
					{kind: "enyo.FittableColumns", noStretch: true, classes: "settings-item", components: [
						{kind: "onyx.InputDecorator", classes: "flex-row", style: "width: 100%;", components: [
							{name: "newFeedURL", kind: "onyx.Input", classes: "flex-auto", style: "width: 100%;  color: white", value: "https://", onfocus: "checkFocus", onblur: "checkBlur" },
							{name: "newFeedURLLabel", content: $L("URL"), classes: "flex-none", style: "padding-left: 5px; color: SkyBlue;"}
						]},
					]},
					{kind: "enyo.FittableColumns", noStretch: true, classes: "settings-item", components: [
						{tag: "span", classes: "settings-title-toggle", content: $L("Compressed"), fit: true},
						{kind: "onyx.ToggleButton", name: "newFeedCompressedToggle", onContent: $L("Yes"), offContent: $L("No") }
					]},
					{kind: "enyo.FittableColumns", noStretch: true, classes: "settings-item", style:"text-align: center", components: [
						{name: "addNewFeedButton", kind: "onyx.Button", classes: "onyx-affirmative", content: $L("Add Feed"), ontap: "testNewFeed"}
					]}
                ]}
            ]},
            {name: "installedFeedsGroupboxHeader", tag: "div", classes: "webosstyle-list-groupbox-top webosstyle-list-groupbox-top-collapsed", components: [
            	{tag: "div", classes: "webosstyle-list-groupbox-header", content: $L("Installed")},
            ]},
		]},
        {tag: "div", style:"width: 100%; text-align: center", components: [
        	{kind: "onyx.Button", classes: "onyx-affirmative", style: "margin:5px; width: 18%; min-width: 100px; font-size: 18px;", content: $L("Close"), ontap: "closePopup"}
        ]},
        {name: "alertDialog", kind: Preware.AlertDialog, onDismiss: "closeDialog"},
        {name: "warningDialog", kind: Preware.ChoiceDialog, title: $L("Third-party Feed"),
        	body: $L("<p>By adding a third-party feed, you're trusting both the package developers and feed maintainer, and that their sites haven't been hacked.</p><p>You take full responsibility for any and all potential outcomes that may occur as a result of doing so, including (but not limited to): loss of warranty, loss of all data, loss of all privacy, security vulnerabilities and device damage.</p>"),
        	onDismiss: "closeDialog"},
    ],

	//setup
    create: enyo.inherit(function (sup) {
		return function () {
			this.warningOkd = false;
			sup.apply(this, arguments);
		};
	}),
	
	show: function() {
		//This is required to reset the shim so that it displays correctly when we are displaying a dialog on top of a dialog.
		this.$.alertDialog.show();
		this.$.alertDialog.hide();
		
		this.$.warningDialog.show();
		this.$.warningDialog.hide();

		this.feeds = [];
		preware.IPKGService.list_configs(this.onFeeds.bind(this));
		
		this.inherited(arguments);
	},
	
	setupItem: function(inSender, inEvent) {
		var i = inEvent.index;
		var item = this.feeds[i];
		
		this.$.feed.setFeed(item);
		
		this.$.feedWrapper.addRemoveClass("webosstyle-list-groupbox-item-last", i == (this.feeds.length - 1));
		this.$.feed.addRemoveClass("webosstyle-list-groupbox-item-content-first", i == 0);
		this.$.feed.addRemoveClass("webosstyle-list-groupbox-item-content-last", i == (this.feeds.length - 1));
				
		return true;
	},

	setupSwipeItem: function(inSender, inEvent) {
        // because setting it on the list itself fails:
        this.$.ManageFeedsList.setPersistSwipeableItem(true);
        this.activeItem = inEvent.index;
        
   		this.$.swipableFeed.addRemoveClass("webosstyle-list-swipable-groupbox-item-first", inEvent.index == 0);
		this.$.swipableFeed.addRemoveClass("webosstyle-list-swipable-groupbox-item-last", inEvent.index == (this.feeds.length - 1));
		
		//This chunk is required because the height is applied to the row as a style, so we have to override the style
		//Can't just use a class.
		if (inEvent.index == (this.feeds.length - 1))
		{ 
			if (!this.rowHeight)
			{
				var currentRowHeight = this.$.swipableFeed.getComputedStyleValue("height");
				this.rowHeight = currentRowHeight.substring(0, currentRowHeight.length - 2);
			}
			this.$.swipableFeed.applyStyle("height", (this.rowHeight - 5) + "px");
		}
		
        this.swiping = true;
    },

    //handlers
    onFeeds: function(payload)
	{
		try 
		{
			if (!payload) 
			{
				// i dont know if this will ever happen, but hey, it might
				this.$.alertDialog.show('Preware', $L("Cannot access the service. First try restarting Preware, or reboot your device and try again."));
				this.doneLoading();
			}
			else if (payload.errorCode != undefined) 
			{
				// we probably dont need to check this stuff here,
				// it would have already been checked and errored out of this process
				if (preware.IPKGService.isNotRunning(payload))
				{
					this.$.alertDialog.show('Preware', $L("The service is not running. First try restarting Preware, or reboot your device and try again."));
					this.doneLoading();
				}
				else
				{
					this.$.alertDialog.show('Preware', payload.errorText);
					this.doneLoading();
				}
			}
			else 
			{
				// clear feeds array
				this.feeds = [];
			
				// load feeds
				for (var x = 0; x < payload.configs.length; x++)
				{
					var feedObj = {
						config: payload.configs[x].config,
						name: payload.configs[x].config.replace(/.conf/, ''),
						url: "",
						data: [],
						enabled: payload.configs[x].enabled
					};
				
					if (payload.configs[x].contents) {
						var tmpSplit1 = payload.configs[x].contents.split('<br>');
						for (var c = 0; c < tmpSplit1.length; c++)
						{
							if (tmpSplit1[c]) 
							{
								var tmpSplit2 = tmpSplit1[c].split(' ');
								feedObj.url = tmpSplit2[2];
								feedObj.data.push(tmpSplit2)
							}
						}
					}

					this.feeds.push(feedObj);
				}
			
				// sort them
				this.feeds.sort(function(a, b)
				{
					if (a.name && b.name)
					{
						return ((a.name < b.name) ? -1 : ((a.name > b.name) ? 1 : 0));
					}
					else
					{
						return -1;
					}
				});
			
				this.doneLoading();
			}
		}
		catch (e)
		{
			console.log(e, 'configs#onFeeds');
			this.$.alertDialog.show('onFeeds Error', e);
		}
	},
	
	doneLoading: function()
	{
		try 
		{
			if (this.feeds.length > 0) 
			{
				this.$.installedFeedsGroupboxHeader.removeClass("webosstyle-list-groupbox-top-collapsed");
			}
			else
			{
				this.$.installedFeedsGroupboxHeader.addClass("webosstyle-list-groupbox-top-collapsed");
			}
			
			this.$.ManageFeedsList.setCount(this.feeds.length);
			this.$.ManageFeedsList.refresh();
		}
		catch (e)
		{
			console.log(e, 'configs#doneLoading');
			this.$.alertDialog.show('doneLoading Error', e);
		}
	},

    handleResize: function(){
    	//Calculate scroller height - if we don't explicitly set the scroller height, it will overflow the dialog
    	var dialogHeight = this.getComputedStyleValue("height").replace('px', '');
    	var dialogPadding = this.getComputedStyleValue("padding-top").replace('px', '');

		var scrollerHeight = dialogHeight - (dialogPadding * 2);

    	this.$.ManageFeedsList.applyStyle("height", scrollerHeight + "px");

		//Calculate Window Width
		var windowWidth = Math.max(document.documentElement.clientWidth, window.innerWidth || 0);
    	var dialogWidth = this.getComputedStyleValue("width").replace('px', '');
    	
    	var groupboxWidth = dialogWidth - (dialogPadding * 2);
		this.$.newFeedName.applyStyle("width", (groupboxWidth - 90) + "px");
		this.$.newFeedURL.applyStyle("width", (groupboxWidth - 90) + "px");
		this.$.addNewFeedButton.applyStyle("width", groupboxWidth + "px");
		
		this.inherited(arguments);
	},

	feedEnabledToggled: function (inSender, inEvent)
	{
		if (this.swiping)
		{
			return true;
		}

		var i = inEvent.index;
		if (this.feeds[i].enabled || this.warningOkd || this.isTrustedFeed(this.feeds[i].url)) {
			this.toggleFeed(i);
		} else {   // enabling feed or warning not previously accepted
			this.$.warningDialog.set("onAction", "okFeedToggle");
			this.$.warningDialog.show(i);
		}
	},
	//feeds that are set up by default don't need a warning
	//(the same ones on LuneOS, see preware.LuneOSFeeds).
	isTrustedFeed: function (url) {
		return /^https?:\/\/([a-z0-9\-]+\.)*(preware\.net|webosarchive\.org|weboslives\.eu)(:|\/)/.test(url);
	},
	okFeedToggle: function (inSender, inEvent) {
    	this.$.warningDialog.hide();
    	this.warningOkd = true;
    	
    	this.toggleFeed(inEvent.data);
	},
	toggleFeed: function (i) {
		var item = this.feeds[i];
		item.enabled = !item.enabled;
		
		this.$.ManageFeedsList.renderRow(i);
		preware.IPKGService.setConfigState(function(){preware.PackagesModel.dirtyFeeds = true;}, item.config, item.enabled);
	},

    closePopup: function (inSender, inEvent) {
		this.clearNewFeed();
		this.$.ManageFeedsList.setCount(0);
		this.$.installedFeedsGroupboxHeader.addClass("webosstyle-list-groupbox-top-collapsed");
        this.hide();
    },

    closeDialog: function (inSender, inEvent) {
        this.$.warningDialog.hide();
        this.$.alertDialog.hide();
    },

	//add feeds
    testNewFeed: function (inSender, inEvent) {
    	var newUrl = this.$.newFeedURL.getValue();
		if (newUrl.indexOf("http://ipkg.preware.org/alpha") == 0 || newUrl.indexOf("http://ipkg.preware.net/alpha") == 0)
		{
			this.$.alertDialog.show($L("Custom Feed"), $L("You may not add alpha testing feeds here. See http://testing.preware.net/"));
		}
		else if (newUrl.indexOf("http://ipkg.preware.org/beta") == 0 || newUrl.indexOf("http://ipkg.preware.net/beta") == 0)
		{
			this.$.alertDialog.show($L("Custom Feed"), $L("You may not add beta testing feeds here. See http://testing.preware.net/"));
		}
		else if (((newUrl.indexOf("http://ipkg.preware.org/feeds") == 0) || (newUrl.indexOf("http://ipkg.preware.net/feeds") == 0)) &&
				 (newUrl.indexOf("/testing/") > 0))
		{
			this.$.alertDialog.show($L("Custom Feed"), $L("The instructions you are following are obsolete. See http://testing.preware.net/"));
		}
		else if (newUrl.indexOf("http://preware.is.awesome.com") == 0)
		{
			this.$.alertDialog.show($L("Custom Feed"), $L("The instructions you are following are obsolete. See http://testing.preware.net/"));
		}
		else if (this.$.newFeedName.getValue() != '' &&
				 newUrl != '' && newUrl != 'http://')
		{
			if (!this.warningOkd)
			{
				this.$.warningDialog.set("onAction", "okWarning");
				this.$.warningDialog.show();
			}
			else
			{
				this.addNewFeed();
			}
		}
		else
		{
			this.$.alertDialog.show($L("Custom Feed"), $L("You need to fill in all fields for a new feed."));
		}
    },
    
    okWarning: function (inSender, inEvent) {
    	this.$.warningDialog.hide();
    	this.warningOkd = true;
    	this.addNewFeed();
    },
    
    addNewFeed: function (inSender, inEvent) {
        preware.IPKGService.addConfig(this.addNewFeedResponse.bind(this), this.$.newFeedName.getValue() + ".conf", this.$.newFeedName.getValue(), this.$.newFeedURL.getValue(), this.$.newFeedCompressedToggle.getValue());
    },
    
	addNewFeedResponse: function (payload) {
		if (payload.stage == 'completed')
		{
			// tell packages the feeds are "dirty"
			preware.PackagesModel.dirtyFeeds = true;
		
			this.clearNewFeed();

			// init feed loading
			preware.IPKGService.list_configs(this.onFeeds.bind(this));
		}
    },

	clearNewFeed: function() {
		this.$.newFeedName.setValue("");
        this.$.newFeedURL.setValue("https://");
        this.$.newFeedCompressedToggle.setValue(false);
	},

	//delete feeds
	completeSwipeItem: function() {
        this.$.ManageFeedsList.completeSwipe();
        this.swiping = false;
    },

    deleteButtonTapped: function(inSender, inEvent) {
    	var item = this.feeds[this.activeItem];
        this.$.ManageFeedsList.setPersistSwipeableItem(false);
    	preware.IPKGService.deleteConfig(this.deleteFeedResponse.bind(this), item.config, item.name);
		this.completeSwipeItem();
    },

	deleteFeedResponse: function (payload) {
		if (payload.stage == 'completed')
		{
			// tell packages the feeds are "dirty"
			preware.PackagesModel.dirtyFeeds = true;

			// init feed loading
			preware.IPKGService.list_configs(this.onFeeds.bind(this));
		}
    },

    cancelButtonTapped: function(inSender, inEvent) {
        this.$.ManageFeedsList.setPersistSwipeableItem(false);
		this.completeSwipeItem();
    },
	
		
	//utility
    checkFocus: function(source, event) {
		source.applyStyle("color", "black");
	},

	checkBlur: function(source, event) {
		source.applyStyle("color", "white");
	}
});

enyo.kind({
	name: "preware.FeedItem",
	
	events: {
		onToggle: ""
	},
	
	//the toggle at the right, the name and URL in the width that is left
	//(a long URL breaks there instead of pushing the toggle out of the row)
	components: [
		{classes: "managefeedslist-feed-item-columns", components: [
			{name: "feedEnabledToggle", kind: "onyx.ToggleButton", classes: "managefeedslist-feed-item-toggle", ontap: "toggleTap"},
			{classes: "managefeedslist-feed-item-text", components: [
				{name: "feedName"},
				{name: "feedURL", classes: "managefeedslist-feed-item-url"}
			]}
		]}
	],
	
	setFeed: function(inFeed) {
		this.$.feedName.setContent(inFeed.name);
		this.$.feedURL.setContent(inFeed.url);
		if(inFeed.enabled)
		{
			this.$.feedEnabledToggle.setContent($L("On"));
		}
		else
		{
			this.$.feedEnabledToggle.setContent($L("Off"));
		}
		//This behaviour is necessary to get the togglebutton to render properly
		this.$.feedEnabledToggle.setValue(!inFeed.enabled);
		this.$.feedEnabledToggle.setValue(inFeed.enabled);
	},
	
	toggleTap: function(inSender, inEvent) {
		this.doToggle(inEvent);
		return true;
	}
});