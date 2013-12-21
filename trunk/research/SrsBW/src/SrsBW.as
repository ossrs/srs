package
{	
	import SrsClass.SrsElapsedTimer;
	import SrsClass.SrsSettings;
	
	import fl.controls.Button;
	import fl.controls.Label;
	import fl.controls.ProgressBar;
	import fl.controls.ProgressBarDirection;
	import fl.controls.ProgressBarMode;
	import fl.controls.TextArea;
	
	import flash.display.Loader;
	import flash.display.LoaderInfo;
	import flash.display.MovieClip;
	import flash.display.Sprite;
	import flash.display.StageAlign;
	import flash.display.StageScaleMode;
	import flash.events.Event;
	import flash.events.IOErrorEvent;
	import flash.events.MouseEvent;
	import flash.events.NetStatusEvent;
	import flash.events.TimerEvent;
	import flash.external.ExternalInterface;
	import flash.media.SoundTransform;
	import flash.media.Video;
	import flash.net.NetConnection;
	import flash.net.NetStream;
	import flash.net.ObjectEncoding;
	import flash.net.URLLoader;
	import flash.net.URLRequest;
	import flash.system.System;
	import flash.text.TextField;
	import flash.text.TextFormat;
	import flash.ui.*;
	import flash.utils.Timer;
	import flash.utils.clearTimeout;
	import flash.utils.setTimeout;
	
	public class SrsBW extends Sprite
	{
		private var addressInput : TextArea;
		private var okButton : Button;
		private var logCenter : TextArea;
		private var progressBar : ProgressBar;
		
		private var connection:NetConnection;
		
		private var publishTimer:Timer;
		
		private var stopPublishMarker:Boolean = false;
		
		private var progressTextLabel:Label;
		
		
		private var loader:Loader;
		
		private var elapTimer:SrsElapsedTimer;
		
		private var server_ip:String;
		private var play_kbps:Number;
		private var pub_kbps:Number;
		
		private var settings:SrsSettings = new SrsSettings;
		
		// set NetConnection ObjectEncoding to AMF0
		NetConnection.defaultObjectEncoding = ObjectEncoding.AMF0;
		
		public function SrsBW()
		{
			this.stage.scaleMode = StageScaleMode.NO_SCALE;
			this.stage.align = StageAlign.TOP_LEFT;
			
			this.graphics.beginFill(0x464645, 0.8);
			this.graphics.drawRect(0, 0, 700, 400);
			this.graphics.endFill();
			
			var request:URLRequest = new URLRequest("ui.swf"); 
			loader = new Loader() 
			loader.contentLoaderInfo.addEventListener(Event.COMPLETE, __loadedSWF); 
			loader.load(request);
			loader.x = 0;
			loader.y = 0;
			
			// NetConnection 
			connection = new NetConnection;
			connection.client = this;
			connection.addEventListener(NetStatusEvent.NET_STATUS, onStatus);
			
			var myMenu:ContextMenu  = new ContextMenu();
			myMenu.hideBuiltInItems();
			myMenu.customItems.push(new ContextMenuItem("Srs 带宽测试工具 0.1", true));
			this.contextMenu = myMenu;
			
			publishTimer = new Timer(50);
			publishTimer.addEventListener(TimerEvent.TIMER, onTimerEventHandle);
			addChild(loader);	
		}
		
		public function __loadedSWF(e:Event):void {
			var contents:MovieClip=e.target.content as MovieClip;
			
			progressBar = contents.progressBar;
			progressBar.setProgress(50, 100);
			progressBar.indeterminate = false;
			progressBar.mode = ProgressBarMode.MANUAL;
			progressBar.maximum = 100;
			progressBar.value = 0;
			
			var startButton:Button = contents.startButton;
			startButton.height = 30;
			startButton.addEventListener(MouseEvent.CLICK, onOKButtonClicked);
			
			progressTextLabel = contents.stateLabel;
			addressInput = contents.addressLineEdit;
			
			var txt:TextFormat = new TextFormat();
			txt.size = 12;
			addressInput.setStyle("textFormat", txt);
			progressTextLabel.setStyle("textFormat", txt);
			startButton.setStyle("textFormat", txt);
			contents.addressLabel.setStyle("textFormat", txt);
			addressInput.setStyle("textFormat", txt);
			
			// restore text
			addressInput.text = settings.addressText();
		}
		
		public function onOKButtonClicked(evt:Event) : void
		{	
			if(connection.connected){
				return;
			}
			
			connection.connect(addressInput.text);
			
			stopPublishMarker = false;
			
			// store the address into cookie
			settings.addAddressText(addressInput.text);
		}
		
		public function onStatus(evt:NetStatusEvent) : void{
			trace(evt.info.code);			
			switch(evt.info.code){
				case "NetConnection.Connect.Failed":
					updateState("连接服务器失败！");
					break;
				case "NetConnection.Connect.Rejected":
					updateState("服务器拒绝连接！");
					break;
				case "NetConnection.Connect.Success":
					server_ip = evt.info.data.srs_server_ip;
					updateState("连接服务器成功: " + server_ip);
					break;
			}
		}
		
		public function close():void{
			trace("close");
		}
		
		public function onSrsBandCheckStartPlayBytes(evt:Object):void{			
			var duration_ms:Number = evt.duration_ms;
			var interval_ms:Number = evt.interval_ms;
			
			connection.call("onSrsBandCheckStartingPlayBytes", null);
			updateState("测试下行带宽(" + server_ip + ")");

			elapTimer = new SrsElapsedTimer;
			progressBar.maximum = evt.duration_ms;
			progressBar.value = 0;
			
			publishTimer.start();
		}
		
		public function onSrsBandCheckPlaying(evt:Object):void{
			//progressBar.value = elapTimer.elapsed();
		}
		
		public function onTimerEventHandle(evt:TimerEvent):void
		{
			progressBar.value += 50;
		}
		
		public function onSrsBandCheckStopPlayBytes(evt:Object):void{			
			var duration_ms:Number = evt.duration_ms;
			var interval_ms:Number = evt.interval_ms;
			var duration_delta:Number = evt.duration_delta;
			var bytes_delta:Number = evt.bytes_delta;
			
			var kbps:Number = 0;
			if(duration_delta > 0){
				kbps = bytes_delta * 8.0 / duration_delta; // b/ms == kbps
			}
			kbps = (int(kbps * 10))/10.0;
			play_kbps = kbps;
			
			flash.utils.setTimeout(stopPlayTest, 0);
			progressBar.value = elapTimer.elapsed();
			progressBar.value = progressBar.maximum;
		}
		
		private function stopPlayTest():void{
			connection.call("onSrsBandCheckStoppedPlayBytes", null);
			publishTimer.stop();
		}
		
		private function publisher():void{
			
			if(stopPublishMarker) {
				return;
			}
			
			var data:Array = new Array();
			for(var i:int; i < 2000; i++){
				data.push("SrS band check data from client's publishing......");
			}
			connection.call("onSrsBandCheckPublishing", null, data);
			
			flash.utils.setTimeout(publisher, 0);
			
			progressBar.value = elapTimer.elapsed();
		}
		
		public function onSrsBandCheckStartPublishBytes(evt:Object):void{			
			var duration_ms:Number = evt.duration_ms;
			var interval_ms:Number = evt.interval_ms;
						
			connection.call("onSrsBandCheckStartingPublishBytes", null);
			updateState("测试上行带宽(" + server_ip + ")");
			
			flash.utils.setTimeout(publisher, 0);
			
			elapTimer.restart();
			progressBar.maximum = duration_ms;
			progressBar.value = 0;
		}
		
		public function onSrsBandCheckStopPublishBytes(evt:Object):void{
			var duration_ms:Number = evt.duration_ms;
			var interval_ms:Number = evt.interval_ms;
			var duration_delta:Number = evt.duration_delta;
			var bytes_delta:Number = evt.bytes_delta;
			
			var kbps:Number = 0;
			if(duration_delta > 0){
				kbps = bytes_delta * 8.0 / duration_delta; // b/ms == kbps
			}
			kbps = (int(kbps * 10))/10.0;
			pub_kbps = kbps;
			progressBar.value = progressBar.maximum;
			
			stopPublishMarker = true;
			stopPublishTest();
		}
		
		private function stopPublishTest():void{
			if(connection.connected){
				connection.call("onSrsBandCheckStoppedPublishBytes", null);
			}
		}
		
		public function onSrsBandCheckFinished(evt:Object):void{			
			var code:Number = evt.code;
			var start_time:Number = evt.start_time;
			var end_time:Number = evt.end_time;
			var play_kbps:Number = evt.play_kbps;
			var publish_kbps:Number = evt.publish_kbps;
			var play_bytes:Number = evt.play_bytes;
			var play_time:Number = evt.play_time;
			var publish_bytes:Number = evt.publish_bytes;
			var publish_time:Number = evt.publish_time;
			
			updateState("检测结束: 服务器: " + server_ip + " 上行: " + publish_kbps + " kbps" + " 下行: " + play_kbps + " kbps");
			
			connection.call("finalClientPacket", null);
		}
		
		public function onBWDone():void{
			// do nothing
		}
		
		private function updateState(text:String):void{
			progressTextLabel.text = text;
		}
	}
}