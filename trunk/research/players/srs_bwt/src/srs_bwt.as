package
{	
	import SrsClass.SrsElapsedTimer;

	import flash.display.LoaderInfo;
	import flash.display.Sprite;
	import flash.display.StageAlign;
	import flash.display.StageScaleMode;
	import flash.events.Event;
	import flash.events.NetStatusEvent;
	import flash.events.TimerEvent;
	import flash.external.ExternalInterface;
	import flash.net.NetConnection;
	import flash.net.ObjectEncoding;
	import flash.system.System;
	import flash.ui.ContextMenu;
	import flash.ui.ContextMenuItem;
	import flash.utils.Timer;
	import flash.utils.setTimeout;

	public class srs_bwt extends Sprite
	{		
		private var connection:NetConnection = null;
		
		private var updatePlayProgressTimer:Timer = null;
		private var elapTimer:SrsElapsedTimer = null;
        
        // user set id.
        private var js_id:String = null;
        // play param url.
        private var user_url:String = null;
		
		// server ip get from server
		private var server_ip:String;
		
		// test wheth publish should to stop
		private var stop_pub:Boolean = false;
		
		// js interface
		private var js_on_player_ready:String;
		private var js_update_progress:String;
		private var js_update_status:String;
		
		private var value_progressbar:Number = 0;
		private var max_progressbar:Number = 0;
		
		// set NetConnection ObjectEncoding to AMF0
		NetConnection.defaultObjectEncoding = ObjectEncoding.AMF0;
		
		public function srs_bwt()
		{
            if (!this.stage) {
                this.addEventListener(Event.ADDED_TO_STAGE, this.system_on_add_to_stage);
            } else {
                this.system_on_add_to_stage(null);
            }
        }
        
        /**
         * system event callback, when this control added to stage.
         * the main function.
         */
        private function system_on_add_to_stage(evt:Event):void {
			this.stage.scaleMode = StageScaleMode.NO_SCALE;
			this.stage.align = StageAlign.TOP_LEFT;
			
			var flashvars:Object 	   = this.root.loaderInfo.parameters;
            
            if (!flashvars.hasOwnProperty("id")) {
                throw new Error("must specifies the id");
            }
            
            this.js_id = flashvars.id;
            this.js_on_player_ready = flashvars.on_bandwidth_ready;
			this.js_update_progress    = flashvars.on_update_progress;
			this.js_update_status 	   = flashvars.on_update_status;
						
			// init context menu
			var myMenu:ContextMenu  = new ContextMenu();
			myMenu.hideBuiltInItems();
			myMenu.customItems.push(new ContextMenuItem("SRS 带宽测试工具", true));
			this.contextMenu = myMenu;
            
            flash.utils.setTimeout(this.system_on_js_ready, 0);
        }
        
        /**
         * system callack event, when js ready, register callback for js.
         * the actual main function.
         */
        private function system_on_js_ready():void {
            if (!flash.external.ExternalInterface.available) {
                trace("js not ready, try later.");
                flash.utils.setTimeout(this.system_on_js_ready, 100);
                return;
            }
            
            flash.external.ExternalInterface.addCallback("__check_bandwidth", this.js_call_check_bandwidth);
            flash.external.ExternalInterface.addCallback("__stop", this.js_call_stop);
            
            flash.external.ExternalInterface.call(this.js_on_player_ready, this.js_id);
        }
		
        private function js_call_check_bandwidth(url:String):void {
            js_call_stop();
            
			// init connection
			connection = new NetConnection;
			connection.client = this;
			connection.addEventListener(NetStatusEvent.NET_STATUS, onStatus);
			connection.connect(url);
			//connection.connect("rtmp://192.168.8.234:1935/app?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com");
			
			// for play to update progress bar
			elapTimer  = new SrsElapsedTimer;
			
			// we suppose the check time = 7 S
			updatePlayProgressTimer = new Timer(100);
			updatePlayProgressTimer.addEventListener(TimerEvent.TIMER, onTimerTimeout);
			updatePlayProgressTimer.start();
		}
        private function js_call_stop():void {
            if (connection) {
                connection.close();
                connection = null;
            }
            if (updatePlayProgressTimer) {
                updatePlayProgressTimer.stop();
                updatePlayProgressTimer = null;
            }
            if (elapTimer) {
                elapTimer.restart();
            }
        }
        
        // srs infos
        private var srs_server:String = null;
        private var srs_primary_authors:String = null;
        private var srs_id:String = null;
        private var srs_pid:String = null;
        private function update_context_items():void {
            // for context menu
            var customItems:Array = [new ContextMenuItem("SrsPlayer")];
            if (srs_server != null) {
                customItems.push(new ContextMenuItem("Server: " + srs_server));
            }
            if (srs_primary_authors != null) {
                customItems.push(new ContextMenuItem("PrimaryAuthors: " + srs_primary_authors));
            }
            if (srs_pid != null) {
                customItems.push(new ContextMenuItem("SrsPid: " + srs_pid));
            }
            if (srs_id != null) {
                customItems.push(new ContextMenuItem("SrsId: " + srs_id));
            }
            contextMenu.customItems = customItems;
        }
		
		// get NetConnection NetStatusEvent
		public function onStatus(evt:NetStatusEvent) : void{
			trace(evt.info.code);
            
            if (evt.info.hasOwnProperty("data") && evt.info.data) {
                if (evt.info.data.hasOwnProperty("srs_server")) {
                    srs_server = evt.info.data.srs_server;
                }
                if (evt.info.data.hasOwnProperty("srs_primary_authors")) {
                    srs_primary_authors = evt.info.data.srs_primary_authors;
                }
                if (evt.info.data.hasOwnProperty("srs_id")) {
                    srs_id = evt.info.data.srs_id;
                }
                if (evt.info.data.hasOwnProperty("srs_pid")) {
                    srs_pid = evt.info.data.srs_pid;
                }
                update_context_items();
            }
            
			switch(evt.info.code){
				case "NetConnection.Connect.Failed":
					updateState("连接服务器失败！");
					break;
				case "NetConnection.Connect.Rejected":
					updateState("服务器拒绝连接！");
					break;
				case "NetConnection.Connect.Success":
					server_ip = evt.info.data.srs_server_ip;
					updateState("连接服务器成功!");
					break;
				case "NetConnection.Connect.Closed":
					//updateState("连接已断开!");
					break;
			}
		}
        
        public function onTimerTimeout(evt:TimerEvent):void
        {	
            value_progressbar = elapTimer.elapsed();
            updateProgess(value_progressbar, max_progressbar);
        }
		
		/**
		 * NetConnection callback this function, when recv server call "onSrsBandCheckStartPlayBytes"
		 * then start @updatePlayProgressTimer for updating the progressbar
		 * */
		public function onSrsBandCheckStartPlayBytes(evt:Object):void{			
			var duration_ms:Number = evt.duration_ms;
			var interval_ms:Number = evt.interval_ms;
			
			connection.call("onSrsBandCheckStartingPlayBytes", null);
			updateState("开始测试下行带宽，服务器IP：" + server_ip);
			
			// we suppose play duration_ms = pub duration_ms
			max_progressbar = duration_ms * 2;
		}
		
		public function onSrsBandCheckPlaying(evt:Object):void{
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

			flash.utils.setTimeout(stopPlayTest, 0);
            updateState("下行带宽测试完毕，服务器: " + server_ip + "，" + kbps + "kbps，开始测试上行带宽。");
		}
		
		private function stopPlayTest():void{
			connection.call("onSrsBandCheckStoppedPlayBytes", null);
		}
		
		public function onSrsBandCheckStartPublishBytes(evt:Object):void{			
			var duration_ms:Number = evt.duration_ms;
			var interval_ms:Number = evt.interval_ms;
						
			connection.call("onSrsBandCheckStartingPublishBytes", null);
			
			flash.utils.setTimeout(publisher, 0);
		}
		
		private function publisher():void{
			if (stop_pub) {
				return;
			}
			
			var data:Array = new Array();
			
			var data_size:int = 100;
			for(var i:int; i < data_size; i++){
				data.push("SrS band check data from client's publishing......");
			}
			data_size += 100;
			connection.call("onSrsBandCheckPublishing", null, data);
						
			flash.utils.setTimeout(publisher, 0);			
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
			
			stopPublishTest();
		}
		
		private function stopPublishTest():void{
			if(connection.connected){
				connection.call("onSrsBandCheckStoppedPublishBytes", null);
			}
			stop_pub = true;
			
			value_progressbar = max_progressbar;
			updateProgess(value_progressbar, max_progressbar);
			updatePlayProgressTimer.stop();
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
			
			updateState("检测结束: 服务器: " + server_ip + " 上行: " + publish_kbps + " kbps" + " 下行: " + play_kbps + " kbps"
				+ " 测试时间: " + (end_time-start_time)/1000 + " 秒");
			connection.call("finalClientPacket", null);
		}
		
		// update progressBar's value
		private function updateProgess(value:Number, maxValue:Number):void{
			flash.external.ExternalInterface.call(this.js_update_progress, this.js_id, value * 100 / maxValue);
			trace(value + "-" + maxValue + "-" + value * 100 / maxValue + "%");
		}
		
		// update checking status
		private function updateState(text:String):void{
			flash.external.ExternalInterface.call(this.js_update_status, this.js_id, text);
			trace(text);
		}
        
        public function onBWDone():void{
        }
	}
}