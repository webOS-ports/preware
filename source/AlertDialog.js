enyo.kind({
	name: "Preware.AlertDialog",
	kind: "onyx.Popup",
	modal: false,
	autoDismiss: false,
	floating: true,
	centered: true,
	scrim: true,
	
	style: "padding: 15px; width: 80%; max-width: 300px",
	
	events: {
		onDismiss: "",		
	},
	
	components: [
		{name: "dialogTitle", style: "font-weight: bold"},
		{tag: "hr"},
		{name: "dialogContent"},
		{components: [
			{kind: "onyx.Button", style: "margin-top: 10px; margin-left: 5%; width: 90%", classes: "onyx-blue", content: $L("OK"), ontap: "chooseOk"},
		]}
	],
	
	show: function(title, content) {
		this.$.dialogTitle.setContent(title)
		this.$.dialogContent.setContent(content)
		
		this.inherited(arguments);
	},
	
	chooseOk: function(){
		this.doDismiss();
	},
})