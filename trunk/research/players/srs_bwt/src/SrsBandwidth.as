//
// Copyright (c) 2013-2021 Winlin
//
// SPDX-License-Identifier: MIT
//
package 
{
    import flash.events.NetStatusEvent;
    import flash.external.ExternalInterface;
    import flash.net.NetConnection;
    import flash.net.NetStream;
    import flash.net.ObjectEncoding;
    import flash.utils.clearTimeout;
    import flash.utils.setTimeout;

    /**
    * SRS bandwidth check/test library,
    * user can copy this file and use it directly,
    * this library will export as callback functions, and js callback functions.
    * 
    * Usage:
    *       var bandwidth:SrsBandwidth = new SrsBandwidth();
    *       bandwidth.initialize(......); // required
    *       bandwidth.check_bandwidth(......); // required
    *       bandwidth.stop(); // optional
    * 
    * @remark we donot use event, but use callback functions set by initialize.
    */
	public class SrsBandwidth
	{
        /**
        * server notice client to do the downloading/play bandwidth test.
        */
        public static const StatusSrsBwtcPlayStart:String = "srs.bwtc.play.start";
        /**
         * server notice client to complete the downloading/play bandwidth test.
         */
        public static const StatusSrsBwtcPlayStop:String = "srs.bwtc.play.stop";
        /**
         * server notice client to do the uploading/publish bandwidth test.
         */
        public static const StatusSrsBwtcPublishStart:String = "srs.bwtc.publish.start";
        /**
         * server notice client to complete the uploading/publish bandwidth test.
         */
        public static const StatusSrsBwtcPublishStop:String = "srs.bwtc.publish.stop";
        
        /**
        * constructor, do nothing
        */
        public function SrsBandwidth()
        {
        }
        
        /**
        * initialize the bandwidth test tool, the callbacks. null to ignore.
        * 
        * the as callbacks.
        * @param as_on_ready, function():void, callback when bandwidth tool is ready to run.
        * @param as_on_status_change, function(code:String, data:String):void, where:
        *       code can be:
        *           "NetConnection.Connect.Failed", see NetStatusEvent(evt.info.code).
        *           "NetConnection.Connect.Rejected", see NetStatusEvent(evt.info.code).
        *           "NetConnection.Connect.Success", see NetStatusEvent(evt.info.code).
        *           "NetConnection.Connect.Closed", see NetStatusEvent(evt.info.code).
        *           SrsBandwidth.StatusSrsBwtcPlayStart, "srs.bwtc.play.start", when srs start test play bandwidth.
        *           SrsBandwidth.StatusSrsBwtcPlayStop, "srs.bwtc.play.stop", when srs complete test play bandwidth.
        *           SrsBandwidth.StatusSrsBwtcPublishStart, "srs.bwtc.publish.start", when srs start test publish bandwidth.
        *           SrsBandwidth.StatusSrsBwtcPublishStop, "srs.bwtc.publish.stop", when srs complete test publish bandwidth.
        *       data is extra parameter:
        *           kbps, for code is SrsBandwidth.StatusSrsBwtcPlayStop or SrsBandwidth.StatusSrsBwtcPublishStop.
        *           "", otherwise empty string.
        * @param as_on_progress_change, function(percent:Number):void, where:
        *       percent, the progress percent, 0 means 0%, 100 means 100%.
        * @param as_on_srs_info, function(srs_server:String, srs_primary:String, srs_authors:String, srs_id:String, srs_pid:String, srs_server_ip:String):void, where:
        *       srs_server: the srs server info.
        *       srs_primary: the srs primary authors info.
        *       srs_authors: the srs authors info.
        *       srs_id: the tracable log id, to direclty grep the log.
        *       srs_pid: the srs process id, to direclty grep the log.
        *       srs_server_ip: the srs server ip, where client connected at.
        * @param as_on_complete, function(start_time:Number, end_time:Number, play_kbps:Number, publish_kbps:Number, play_bytes:Number, publish_bytes:Number, play_time:Number, publish_time:Number):void, where
        *       start_time, the start timestamp, in ms.
        *       end_time, the finish timestamp, in ms.
        *       play_kbps, the play/downloading kbps.
        *       publish_kbps, the publish/uploading kbps.
        *       play_bytes, the bytes play/download from server, in bytes.
        *       publish_bytes, the bytes publish/upload to server, in bytes.
        *       play_time, the play/download duration time, in ms.
        *       publish_time, the publish/upload duration time, in ms.
        * 
        * the js callback id.
        * @param js_id, specifies the id of swfobject, used to identify the bandwidth object.
        *       for all js callback, the first param always be the js_id, to identify the callback object.
        * 
        * the js callbacks.
        * @param js_on_ready, function(js_id:String):void, callback when bandwidth tool is ready to run.
        * @param js_on_status_change, function(js_id:String, code:String, data:String):void
        * @param as_on_progress_change, function(js_id:String, percent:Number):void
        * @param as_on_srs_info, function(js_id:String, srs_server:String, srs_primary:String, srs_authors:String, srs_id:String, srs_pid:String, srs_server_ip:String):void
        * @param as_on_complete, function(js_id:String, start_time:Number, end_time:Number, play_kbps:Number, publish_kbps:Number, play_bytes:Number, publish_bytes:Number, play_time:Number, publish_time:Number):void
        * 
        * the js export functions.
        * @param js_export_check_bandwidth, function(url:String):void, for js to start bandwidth check, @see: check_bandwidth(url:String):void
        * @param js_export_stop, function():void, for js to stop bandwidth check, @see: stop():void
        * 
        * @remark, all parameters can be null.
        * @remark, as and js callback use same parameter, except that the js calblack first parameter is js_id:String.
        */
        public function initialize(
            as_on_ready:Function, as_on_status_change:Function, as_on_progress_change:Function, as_on_srs_info:Function, as_on_complete:Function,
            js_id:String, js_on_ready:String, js_on_status_change:String, js_on_progress_change:String, js_on_srs_info:String, js_on_complete:String,
            js_export_check_bandwidth:String, js_export_stop:String
        ):void {
            this.as_on_ready = as_on_ready;
            this.as_on_srs_info = as_on_srs_info;
            this.as_on_status_change = as_on_status_change;
            this.as_on_progress_change = as_on_progress_change;
            this.as_on_complete = as_on_complete;
            
            this.js_id = js_id;
            this.js_on_srs_info = js_on_srs_info;
            this.js_on_ready = js_on_ready;
            this.js_on_status_change = js_on_status_change;
            this.js_on_progress_change = js_on_progress_change;
            this.js_on_complete = js_on_complete;
            
            this.js_export_check_bandwidth = js_export_check_bandwidth;
            this.js_export_stop = js_export_stop;
            
            flash.utils.setTimeout(this.system_on_js_ready, 0);
        }
        
        /**
        * start check bandwidth.
        * @param url, a String indicates the url to check bandwidth, 
        *       format as: rtmp://server:port/app?key=xxx&&vhost=xxx
        *       for example, rtmp://dev:1935/app?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com
        * where the key and vhost must be config in SRS, like:
        *       vhost bandcheck.srs.com {
        *           enabled         on;
        *           chunk_size      65000;
        *           bandcheck {
        *               enabled         on;
        *               key             "35c9b402c12a7246868752e2878f7e0e";
        *               interval        30;
        *               limit_kbps      4000;
        *           }
        *       }
        * 
        * @remark user must invoke this as method, or js exported method.
        */
        public function check_bandwidth(url:String):void {
            this.js_call_check_bandwidth(url);
        }
        
        /**
        * stop check bancwidth.
        * @remark it's optional, however, user can abort the bandwidth check.
        */
        public function stop():void {
            this.js_call_stop();
        }
        
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////         Private Section, ignore please.                     ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////                                                             ///////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        //////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        
        /**
        * ***********************************************************************
        * private section, including private fields, method and embeded classes.
        * ***********************************************************************
        */
        
        /**
        * as callback.
        */
        private var as_on_ready:Function;
        private var as_on_srs_info:Function;
        private var as_on_status_change:Function;
        private var as_on_progress_change:Function;
        private var as_on_complete:Function;
        
        /**
         * js callback.
         */
        private var js_id:String;
        private var js_on_ready:String;
        private var js_on_srs_info:String;
        private var js_on_status_change:String;
        private var js_on_progress_change:String;
        private var js_on_complete:String;
        
        /**
        * js export functions.
        */
        private var js_export_check_bandwidth:String;
        private var js_export_stop:String;
        
        /**
        * srs debug infos
        */
        private var srs_server:String = null;
        private var srs_primary:String = null;
        private var srs_authors:String = null;
        private var srs_id:String = null;
        private var srs_pid:String = null;
        private var srs_server_ip:String = null;
        
        /**
        * the underlayer connection, to send call message to do the bandwidth 
        * check/test with server.
        */
        private var connection:NetConnection = null;
		// for bms4, use stream to play then do bandwidth test.
		private var stream:NetStream = null;
        
        /**
         * use timeout to sendout publish call packets.
         * when got stop publish packet from server, stop publish call loop.
         */
        private var publish_timeout_handler:uint = 0;
        
        /**
         * system callack event, when js ready, register callback for js.
         * the actual main function.
         */
        private function system_on_js_ready():void {
            if (!flash.external.ExternalInterface.available) {
                log("js not ready, try later.");
                flash.utils.setTimeout(this.system_on_js_ready, 100);
                return;
            }
            
            if (this.js_export_check_bandwidth != null) {
                flash.external.ExternalInterface.addCallback(this.js_export_check_bandwidth, this.js_call_check_bandwidth);
            }
            if (this.js_export_stop != null) {
                flash.external.ExternalInterface.addCallback(this.js_export_stop, this.js_call_stop);
            }
            
            if (as_on_ready != null) {
                as_on_ready();
            }
            if (js_on_ready != null) {
                flash.external.ExternalInterface.call(this.js_on_ready, this.js_id);
            }
        }
        private function js_call_check_bandwidth(url:String):void {
            js_call_stop();
            
            __on_progress_change(0);
            
            // init connection
			log("create connection for bandwidth check");
            connection = new NetConnection;
            connection.objectEncoding = ObjectEncoding.AMF0;
            connection.client = {
                onStatus: onStatus,
                // play
                onSrsBandCheckStartPlayBytes: onSrsBandCheckStartPlayBytes,
                onSrsBandCheckPlaying: onSrsBandCheckPlaying,
                onSrsBandCheckStopPlayBytes: onSrsBandCheckStopPlayBytes,
                // publish
                onSrsBandCheckStartPublishBytes: onSrsBandCheckStartPublishBytes,
                onSrsBandCheckStopPublishBytes: onSrsBandCheckStopPublishBytes,
                onSrsBandCheckFinished: onSrsBandCheckFinished
            };
            connection.addEventListener(NetStatusEvent.NET_STATUS, onStatus);
            connection.connect(url);
            
            __on_progress_change(3);
        }
        private function js_call_stop():void {
            if (connection) {
                connection.close();
                connection = null;
            }
        }
        
        /**
         * NetConnection callback this function, when recv server call "onSrsBandCheckStartPlayBytes"
         * then start @updatePlayProgressTimer for updating the progressbar
         * */
        private function onSrsBandCheckStartPlayBytes(evt:Object):void{
            var duration_ms:Number = evt.duration_ms;
            var interval_ms:Number = evt.interval_ms;
            log("start play test, duration=" + duration_ms + ", interval=" + interval_ms);
            
            connection.call("onSrsBandCheckStartingPlayBytes", null);
            __on_status_change(SrsBandwidth.StatusSrsBwtcPlayStart);
            
            __on_progress_change(10);
        }
        private function onSrsBandCheckPlaying(evt:Object):void{
        }
        private function onSrsBandCheckStopPlayBytes(evt:Object):void{			
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
            __on_status_change(SrsBandwidth.StatusSrsBwtcPlayStop, String(kbps));
            
            __on_progress_change(40);
        }
        private function stopPlayTest():void{
            connection.call("onSrsBandCheckStoppedPlayBytes", null);
        }
        /**
        * publishing methods.
        */
        private function onSrsBandCheckStartPublishBytes(evt:Object):void{			
            var duration_ms:Number = evt.duration_ms;
            var interval_ms:Number = evt.interval_ms;
            
            connection.call("onSrsBandCheckStartingPublishBytes", null);
            
            flash.utils.setTimeout(publisher, 0);
            __on_status_change(SrsBandwidth.StatusSrsBwtcPublishStart);
            
            __on_progress_change(60);
        }
        private function publisher():void{
            var data:Array = new Array();
            
            /**
            * the data size cannot too large, it will increase the test time.
            * server need atleast got one packet, then timeout to stop the publish.
            * 
            * cannot too small neither, it will limit the max publish kbps.
            * 
            * the test values:
            *                                               test_s             test_s
            *       data_size        max_publish_kbps       (no limit)      (limit upload to 5KBps)
            *       100                 2116                  6.5               7.3
            *       200                 4071                  6.5               7.7
            *       300                 6438                  6.5               10.3
            *       400                 9328                  6.5               10.2
            *       500                 10377                 6.5               10.0
            *       600                 13737                 6.5               10.8
            *       700                 15635                 6.5               12.0
            *       800                 18103                 6.5               14.0
            *       900                 20484                 6.5               14.2
            *       1000                21447                 6.5               16.8
            */
            var data_size:int = 900;
            for(var i:int; i < data_size; i++) {
                data.push("SrS band check data from client's publishing......");
            }
            
            connection.call("onSrsBandCheckPublishing", null, data);
            
            publish_timeout_handler = flash.utils.setTimeout(publisher, 0);			
        }
        private function onSrsBandCheckStopPublishBytes(evt:Object):void{
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
            
            __on_progress_change(90);
        }
        private function stopPublishTest():void{
            // the stop publish response packet can not send out, for the queue is full.
            //connection.call("onSrsBandCheckStoppedPublishBytes", null);
            
            // clear the timeout to stop the send loop.
            if (publish_timeout_handler > 0) {
                flash.utils.clearTimeout(publish_timeout_handler);
                publish_timeout_handler = 0;
            }
        }
        private function onSrsBandCheckFinished(evt:Object):void{
            var start_time:Number = evt.start_time;
            var end_time:Number = evt.end_time;
            var play_kbps:Number = evt.play_kbps;
            var publish_kbps:Number = evt.publish_kbps;
            var play_bytes:Number = evt.play_bytes;
            var play_time:Number = evt.play_time;
            var publish_bytes:Number = evt.publish_bytes;
            var publish_time:Number = evt.publish_time;
            
            if (this.as_on_complete != null) {
                this.as_on_complete(start_time, end_time, play_kbps, publish_kbps, play_bytes, publish_bytes, play_time, publish_time);
            }
            if (this.js_on_complete != null) {
                flash.external.ExternalInterface.call(this.js_on_complete, this.js_id,
                    start_time, end_time, play_kbps, publish_kbps, play_bytes, publish_bytes, play_time, publish_time);
            }
            
            __on_progress_change(100);
            
            // when got finish packet, directly close connection.
            js_call_stop();
            
            // the last final packet can not send out, for the queue is full.
            //connection.call("finalClientPacket", null);
        }
        
        /**
        * get NetConnection NetStatusEvent
        */
        private function onStatus(evt:NetStatusEvent): void {
            log(evt.info.code);
            
			var srs_version:String = null;
            if (evt.info.hasOwnProperty("data") && evt.info.data) {
                if (evt.info.data.hasOwnProperty("srs_server")) {
                    srs_server = evt.info.data.srs_server;
                }
                if (evt.info.data.hasOwnProperty("srs_primary")) {
                    srs_primary = evt.info.data.srs_primary;
                }
                if (evt.info.data.hasOwnProperty("srs_authors")) {
                    srs_authors = evt.info.data.srs_authors;
                }
                if (evt.info.data.hasOwnProperty("srs_id")) {
                    srs_id = evt.info.data.srs_id;
                }
                if (evt.info.data.hasOwnProperty("srs_pid")) {
                    srs_pid = evt.info.data.srs_pid;
                }
                if (evt.info.data.hasOwnProperty("srs_server_ip")) {
                    srs_server_ip = evt.info.data.srs_server_ip;
                }
				if (evt.info.data.hasOwnProperty("srs_version")) {
					srs_version = evt.info.data.srs_version;
				}
                
                if (this.as_on_srs_info != null) {
                    this.as_on_srs_info(srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip);
                }
                if (this.js_on_srs_info != null) {
                    flash.external.ExternalInterface.call(this.js_on_srs_info, this.js_id, 
                        srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip);
                }
			}
			
			var e:NetStatusEvent = evt;
			var foo:Function = function():void{
				var evt:NetStatusEvent = e;
				if (evt.info.code) {
					__on_status_change(evt.info.code);
				}
				switch(evt.info.code){
					case "NetConnection.Connect.Success":
						__on_progress_change(8);
						break;
				}
			};
			foo();
			
			// for bms4, play stream to trigger the bandwidth check.
			if (evt.info.code != "NetConnection.Connect.Success") {
				return;
			}
			if (stream != null) {
				return;
			}
			
			var is_bms:Boolean = false;
			if (srs_server.indexOf("BMS/") == 0 || srs_server.indexOf("UPYUN/") == 0) {
				is_bms = true;
			}
			if (parseInt(srs_version.charAt(0)) >= 4 && is_bms) {
				stream = new NetStream(connection);
				stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void{
					log(evt.info.code);
					
					if (evt.info.code == "NetStream.Play.Start") {
					}
				});
				stream.play("test");
				log("play stream for " + srs_server + " " + srs_version);
				return;
			}
        }
        
        /**
        * invoke the callback.
        */
        private function __on_progress_change(percent:Number):void {
            if (this.as_on_progress_change != null) {
                this.as_on_progress_change(percent);
            }
            if (this.js_on_progress_change != null) {
                flash.external.ExternalInterface.call(this.js_on_progress_change, this.js_id, 
                    percent);
            }
        }
        private function __on_status_change(code:String, data:String=""):void {
            if (this.as_on_status_change != null) {
                this.as_on_status_change(code, data);
            }
            if (this.js_on_status_change != null) {
                flash.external.ExternalInterface.call(this.js_on_status_change, this.js_id, 
                    code, data);
            }
        }
		
		private function log(msg:String):void {
			trace(msg);
			if (ExternalInterface.available) {
				ExternalInterface.call("console.log", msg);
			}
		}
    }
}
