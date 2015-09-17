package
{
    import flash.display.Sprite;
    import flash.display.StageAlign;
    import flash.display.StageDisplayState;
    import flash.display.StageScaleMode;
    import flash.events.Event;
    import flash.events.FullScreenEvent;
    import flash.events.MouseEvent;
    import flash.events.NetStatusEvent;
    import flash.events.TimerEvent;
    import flash.external.ExternalInterface;
    import flash.media.SoundTransform;
    import flash.media.Video;
    import flash.net.NetConnection;
    import flash.net.NetStream;
    import flash.system.Security;
    import flash.ui.ContextMenu;
    import flash.ui.ContextMenuItem;
    import flash.utils.Timer;
    import flash.utils.getTimer;
    import flash.utils.setTimeout;
    
    import flashx.textLayout.formats.Float;
    
    public class srs_player extends Sprite
    {
        // user set id.
        private var js_id:String = null;
        // user set callback
        private var js_on_player_ready:String = null;
        private var js_on_player_metadata:String = null;
        private var js_on_player_timer:String = null;
        private var js_on_player_empty:String = null;
		private var js_on_player_full:String = null;
		
        // play param url.
        private var user_url:String = null;
        // play param, user set width and height
        private var user_w:int = 0;
        private var user_h:int = 0;
        // user set dar den:num
        private var user_dar_den:int = 0;
        private var user_dar_num:int = 0;
        // user set fs(fullscreen) refer and percent.
        private var user_fs_refer:String = null;
        private var user_fs_percent:int = 0;
        
        // media specified.
        private var media_conn:NetConnection = null;
        private var media_stream:NetStream = null;
        private var media_video:Video = null;
        private var media_metadata:Object = {};
        private var media_timer:Timer = new Timer(300);
        
        // controls.
        // flash donot allow js to set to fullscreen,
        // only allow user click to enter fullscreen.
        private var control_fs_mask:Sprite = new Sprite();
        
        public function srs_player()
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
            this.removeEventListener(Event.ADDED_TO_STAGE, this.system_on_add_to_stage);
            
            this.stage.align = StageAlign.TOP_LEFT;
            this.stage.scaleMode = StageScaleMode.NO_SCALE;
            
            this.stage.addEventListener(FullScreenEvent.FULL_SCREEN, this.user_on_stage_fullscreen);
            
            Security.allowDomain("*");
            
            this.addChild(this.control_fs_mask);
            this.control_fs_mask.buttonMode = true;
            this.control_fs_mask.addEventListener(MouseEvent.CLICK, user_on_click_video);
            
            this.contextMenu = new ContextMenu();
            this.contextMenu.hideBuiltInItems();
            
            var flashvars:Object = this.root.loaderInfo.parameters;
            
            if (!flashvars.hasOwnProperty("id")) {
                throw new Error("must specifies the id");
            }
            
            this.js_id = flashvars.id;
            this.js_on_player_ready = flashvars.on_player_ready;
            this.js_on_player_metadata = flashvars.on_player_metadata;
            this.js_on_player_timer = flashvars.on_player_timer;
			this.js_on_player_empty = flashvars.on_player_empty;
			this.js_on_player_full = flashvars.on_player_full;
            
            this.media_timer.addEventListener(TimerEvent.TIMER, this.system_on_timer);
            this.media_timer.start();
            
            flash.utils.setTimeout(this.system_on_js_ready, 0);
        }
        
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
            
            flash.external.ExternalInterface.addCallback("__play", this.js_call_play);
            flash.external.ExternalInterface.addCallback("__stop", this.js_call_stop);
            flash.external.ExternalInterface.addCallback("__pause", this.js_call_pause);
			flash.external.ExternalInterface.addCallback("__resume", this.js_call_resume);
            flash.external.ExternalInterface.addCallback("__set_dar", this.js_call_set_dar);
            flash.external.ExternalInterface.addCallback("__set_fs", this.js_call_set_fs_size);
            flash.external.ExternalInterface.addCallback("__set_bt", this.js_call_set_bt);
			flash.external.ExternalInterface.addCallback("__set_mbt", this.js_call_set_mbt);
            
            flash.external.ExternalInterface.call(this.js_on_player_ready, this.js_id);
        }
        
        /**
        * system callack event, timer to do some regular tasks.
        */
        private function system_on_timer(evt:TimerEvent):void {
			var ms:NetStream = this.media_stream;
			
            if (!ms) {
                log("stream is null, ignore timer event.");
                return;
            }
			
			var rtime:Number = flash.utils.getTimer();
			var bitrate:Number = Number((ms.info.videoBytesPerSecond + ms.info.audioBytesPerSecond) * 8 / 1000);
            log("on timer, time=" + ms.time.toFixed(2) + "s, buffer=" + ms.bufferLength.toFixed(2) + "s" 
				+ ", bitrate=" + bitrate.toFixed(1) + "kbps"
				+ ", fps=" + ms.currentFPS.toFixed(1)
				+ ", rtime=" + rtime.toFixed(0)
			);
            flash.external.ExternalInterface.call(
                this.js_on_player_timer, this.js_id, ms.time, ms.bufferLength,
				bitrate, ms.currentFPS, rtime
			);
        }
		
		/**
		 * system callback event, when stream is empty.
		 */
		private function system_on_buffer_empty():void {
			var time:Number = flash.utils.getTimer();
			log("stream is empty at " + time + "ms");
			flash.external.ExternalInterface.call(this.js_on_player_empty, this.js_id, time);
		}
		private function system_on_buffer_full():void {
			var time:Number = flash.utils.getTimer();
			log("stream is full at " + time + "ms");
			flash.external.ExternalInterface.call(this.js_on_player_full, this.js_id, time);
		}
        
        /**
         * system callack event, when got metadata from stream.
         * or got video dimension change event(the DAR notification), to update the metadata manually.
         */
        private function system_on_metadata(metadata:Object):void {
            this.media_metadata = metadata;
            
            // for js.
            var obj:Object = __get_video_size_object();
            
            obj.server = 'srs';
            obj.contributor = 'winlin';
            
            if (srs_server != null) {
                obj.server = srs_server;
            }
            if (srs_primary != null) {
                obj.contributor = srs_primary;
            }
            if (srs_authors != null) {
                obj.contributor = srs_authors;
            }
            
            var code:int = flash.external.ExternalInterface.call(js_on_player_metadata, js_id, obj);
            if (code != 0) {
                throw new Error("callback on_player_metadata failed. code=" + code);
            }
        }
        
        /**
         * player callack event, when user click video to enter or leave fullscreen.
         */
        private function user_on_stage_fullscreen(evt:FullScreenEvent):void {
            if (!evt.fullScreen) {
                __execute_user_set_dar();
            } else {
                __execute_user_enter_fullscreen();
            }
        }
        
        /**
         * user event callback, js cannot enter the fullscreen mode, user must click to.
         */
        private function user_on_click_video(evt:MouseEvent):void {
            if (!this.stage.allowsFullScreen) {
                log("donot allow fullscreen.");
                return;
            }
            
            // enter fullscreen to get the fullscreen size correctly.
            if (this.stage.displayState == StageDisplayState.FULL_SCREEN) {
                this.stage.displayState = StageDisplayState.NORMAL;
            } else {
                this.stage.displayState = StageDisplayState.FULL_SCREEN;
            }
        }
        
        /**
         * function for js to call: to pause the stream. ignore if not play.
         */
        private function js_call_pause():void {
            if (this.media_stream) {
                this.media_stream.pause();
				log("user pause play");
            }
        }
        
        /**
         * function for js to call: to resume the stream. ignore if not play.
         */
        private function js_call_resume():void {
            if (this.media_stream) {
                this.media_stream.resume();
				log("user resume play");
            }
        }
        
        /**
        * to set the DAR, for example, DAR=16:9 where num=16,den=9.
        * @param num, for example, 16. 
        *       use metadata width if 0.
        *       use user specified width if -1.
        * @param den, for example, 9. 
        *       use metadata height if 0.
        *       use user specified height if -1.
         */
        private function js_call_set_dar(num:int, den:int):void {
            user_dar_num = num;
            user_dar_den = den;
            
            flash.utils.setTimeout(__execute_user_set_dar, 0);
			log("user set dar to " + num + "/" + den);
        }
        
        /**
         * set the fullscreen size data.
         * @refer the refer fullscreen mode. it can be:
         *       video: use video orignal size.
         *       screen: use screen size to rescale video.
         * @param percent, the rescale percent, where
         *       100 means 100%.
         */
        private function js_call_set_fs_size(refer:String, percent:int):void {
            user_fs_refer = refer;
            user_fs_percent = percent;
			log("user set refer to " + refer + ", percent to" + percent);
        }
        
        /**
         * set the stream buffer time in seconds.
         * @buffer_time the buffer time in seconds.
         */
        private function js_call_set_bt(buffer_time:Number):void {
            if (this.media_stream) {
                this.media_stream.bufferTime = buffer_time;
				log("user set bufferTime to " + buffer_time.toFixed(2) + "s");
            }
        }
		
		/**
		 * set the max stream buffer time in seconds.
		 * @max_buffer_time the max buffer time in seconds.
		 * @remark this is the key feature for realtime communication by flash.
		 */
		private function js_call_set_mbt(max_buffer_time:Number):void {
			if (this.media_stream) {
				this.media_stream.bufferTimeMax = max_buffer_time;
				log("user set bufferTimeMax to " + max_buffer_time.toFixed(2) + "s");
			}
		}
        
        /**
         * function for js to call: to stop the stream. ignore if not play.
         */
        private function js_call_stop():void {
            if (this.media_video) {
                this.removeChild(this.media_video);
                this.media_video = null;
            }
            if (this.media_stream) {
                this.media_stream.close();
                this.media_stream = null;
            }
            if (this.media_conn) {
                this.media_conn.close();
                this.media_conn = null;
            }
			log("player stopped");
        }
        
        // srs infos
        private var srs_server:String = null;
        private var srs_primary:String = null;
        private var srs_authors:String = null;
        private var srs_id:String = null;
        private var srs_pid:String = null;
        private var srs_server_ip:String = null;
        private function update_context_items():void {
            // for context menu
            var customItems:Array = [new ContextMenuItem("SrsPlayer")];
            if (srs_server != null) {
                customItems.push(new ContextMenuItem("Server: " + srs_server));
            }
            if (srs_primary != null) {
                customItems.push(new ContextMenuItem("Primary: " + srs_primary));
            }
            if (srs_authors != null) {
                customItems.push(new ContextMenuItem("Authors: " + srs_authors));
            }
            if (srs_server_ip != null) {
                customItems.push(new ContextMenuItem("SrsIp: " + srs_server_ip));
            }
            if (srs_pid != null) {
                customItems.push(new ContextMenuItem("SrsPid: " + srs_pid));
            }
            if (srs_id != null) {
                customItems.push(new ContextMenuItem("SrsId: " + srs_id));
            }
            contextMenu.customItems = customItems;
        }
        
        /**
         * function for js to call: to play the stream. stop then play.
         * @param url, the rtmp/http url to play.
         * @param _width, the player width.
         * @param _height, the player height.
         * @param buffer_time, the buffer time in seconds. recommend to >=0.5
		 * @param max_buffer_time, the max buffer time in seconds. recommend to 3 x buffer_time.
         * @param volume, the volume, 0 is mute, 1 is 100%, 2 is 200%.
         */
        private function js_call_play(url:String, _width:int, _height:int, buffer_time:Number, max_buffer_time:Number, volume:Number):void {
            this.user_url = url;
            this.user_w = _width;
            this.user_h = _height;
            log("start to play url: " + this.user_url + ", w=" + this.user_w + ", h=" + this.user_h
				+ ", buffer=" + buffer_time.toFixed(2) + "s, max_buffer=" + max_buffer_time.toFixed(2) + "s, volume=" + volume.toFixed(2)
			);
            
            js_call_stop();
            
            this.media_conn = new NetConnection();
            this.media_conn.client = {};
            this.media_conn.client.onBWDone = function():void {};
            this.media_conn.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                log("NetConnection: code=" + evt.info.code);
                
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
                    update_context_items();
                }
                
                // TODO: FIXME: failed event.
                if (evt.info.code != "NetConnection.Connect.Success") {
                    return;
                }
                
                media_stream = new NetStream(media_conn);
                media_stream.soundTransform = new SoundTransform(volume);
                media_stream.bufferTime = buffer_time;
				media_stream.bufferTimeMax = max_buffer_time;
                media_stream.client = {};
                media_stream.client.onMetaData = system_on_metadata;
                media_stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                    log("NetStream: code=" + evt.info.code);
                    
                    if (evt.info.code == "NetStream.Video.DimensionChange") {
                        system_on_metadata(media_metadata);
                    } else if (evt.info.code == "NetStream.Buffer.Empty") {
						system_on_buffer_empty();
					} else if (evt.info.code == "NetStream.Buffer.Full") {
						system_on_buffer_full();
					}
                    
                    // TODO: FIXME: failed event.
                });
                
                if (url.indexOf("http") == 0) {
                    media_stream.play(url);
                } else {
                    var streamName:String = url.substr(url.lastIndexOf("/") + 1);
                    media_stream.play(streamName);
                }
                
                media_video = new Video();
                media_video.width = _width;
                media_video.height = _height;
                media_video.attachNetStream(media_stream);
                media_video.smoothing = true;
                addChild(media_video);
                
                __draw_black_background(_width, _height);
                
                // lowest layer, for mask to cover it.
                setChildIndex(media_video, 0);
            });
            
            if (url.indexOf("http") == 0) {
                this.media_conn.connect(null);
            } else {
                var tcUrl:String = this.user_url.substr(0, this.user_url.lastIndexOf("/"));
                this.media_conn.connect(tcUrl);
            }
        }
        
        /**
        * get the "right" size of video,
        * 1. initialize with the original video object size.
        * 2. override with metadata size if specified.
        * 3. override with codec size if specified.
        */
        private function __get_video_size_object():Object {
            var obj:Object = {
                width: media_video.width,
                height: media_video.height
            };
            
            // override with metadata size.
            if (this.media_metadata.hasOwnProperty("width")) {
                obj.width = this.media_metadata.width;
            }
            if (this.media_metadata.hasOwnProperty("height")) {
                obj.height = this.media_metadata.height;
            }
            
            // override with codec size.
            if (media_video.videoWidth > 0) {
                obj.width = media_video.videoWidth;
            }
            if (media_video.videoHeight > 0) {
                obj.height = media_video.videoHeight;
            }
            
            return obj;
        }
        
        /**
        * execute the enter fullscreen action.
        */
        private function __execute_user_enter_fullscreen():void {
            if (!user_fs_refer || user_fs_percent <= 0) {
                return;
            }
            
            // change to video size if refer to video.
            var obj:Object = __get_video_size_object();
            
            // get the DAR
            var den:int = user_dar_den;
            var num:int = user_dar_num;
            
            if (den == 0) {
                den = obj.height;
            }
            if (den == -1) {
                den = this.stage.fullScreenHeight;
            }
            
            if (num == 0) {
                num = obj.width;
            }
            if (num == -1) {
                num = this.stage.fullScreenWidth;
            }
                
            // for refer is screen.
            if (user_fs_refer == "screen") {
                obj = {
                    width: this.stage.fullScreenWidth,
                    height: this.stage.fullScreenHeight
                };
            }
            
            // rescale to fs
            __update_video_size(num, den, 
				obj.width * user_fs_percent / 100, 
				obj.height * user_fs_percent / 100, 
				this.stage.fullScreenWidth, this.stage.fullScreenHeight
			);
        }
        
        /**
         * for user set dar, or leave fullscreen to recover the dar.
         */
        private function __execute_user_set_dar():void {
            // get the DAR
            var den:int = user_dar_den;
            var num:int = user_dar_num;
            
            var obj:Object = __get_video_size_object();
            
            if (den == 0) {
                den = obj.height;
            }
            if (den == -1) {
                den = this.user_h;
            }
            
            if (num == 0) {
                num = obj.width;
            }
            if (num == -1) {
                num = this.user_w;
            }
            
            __update_video_size(num, den, this.user_w, this.user_h, this.user_w, this.user_h);
        }
        
        /**
        * update the video width and height, 
        * according to the specifies DAR(den:num) and max size(w:h).
        * set the position of video(x,y) specifies by size(sw:sh),
        * and update the bg to size(sw:sh).
        * @param _num/_den the DAR. use to rescale the player together with paper size.
        * @param _w/_h the video draw paper size. used to rescale the player together with DAR.
        * @param _sw/_wh the stage size, >= paper size. used to center the player.
        */
        private function __update_video_size(_num:int, _den:int, _w:int, _h:int, _sw:int, _sh:int):void {
            if (!this.media_video || _den <= 0 || _num <= 0) {
                return;
            }
            
            // set DAR.
            // calc the height by DAR
            var _height:int = _w * _den / _num;
            if (_height <= _h) {
                this.media_video.width = _w;
                this.media_video.height = _height;
            } else {
                // height overflow, calc the width by DAR
                var _width:int = _h * _num / _den;
                
                this.media_video.width = _width;
                this.media_video.height = _h;
            }
            
            // align center.
            this.media_video.x = (_sw - this.media_video.width) / 2;
            this.media_video.y = (_sh - this.media_video.height) / 2;
            
            __draw_black_background(_sw, _sh);
        }
        
        /**
        * draw black background and draw the fullscreen mask.
        */
        private function __draw_black_background(_width:int, _height:int):void {
            // draw black bg.
            this.graphics.beginFill(0x00, 1.0);
            this.graphics.drawRect(0, 0, _width, _height);
            this.graphics.endFill();
            
            // draw the fs mask.
            this.control_fs_mask.graphics.beginFill(0xff0000, 0);
            this.control_fs_mask.graphics.drawRect(0, 0, _width, _height);
            this.control_fs_mask.graphics.endFill();
        }
		
		private function log(msg:String):void {
			msg = "[" + new Date() +"][srs-player][" + js_id + "] " + msg;
			
			trace(msg);
			
			if (!flash.external.ExternalInterface.available) {
				flash.utils.setTimeout(log, 300, msg);
				return;
			}
			
			ExternalInterface.call("console.log", msg);
		}
    }
}
