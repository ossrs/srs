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
    import flash.external.ExternalInterface;
    import flash.media.Video;
    import flash.net.NetConnection;
    import flash.net.NetStream;
    import flash.system.Security;
    import flash.ui.ContextMenu;
    import flash.ui.ContextMenuItem;
    import flash.utils.setTimeout;
    
    public class srs_player extends Sprite
    {
        // user set id.
        private var id:String = null;
        // user set callback
        private var on_player_ready:String = null;
        private var on_player_metadata:String = null;
        
        // play param url.
        private var url:String = null;
        // play param, user set width and height
        private var w:int = 0;
        private var h:int = 0;
        // user set dar den:num
        private var dar_num:int = 0;
        private var dar_den:int = 0;
        // user set fs(fullscreen) refer and percent.
        private var fs_refer:String = null;
        private var fs_percent:int = 0;
        
        private var conn:NetConnection = null;
        private var stream:NetStream = null;
        private var video:Video = null;
        private var metadata:Object = {};
        
        // flash donot allow js to set to fullscreen,
        // only allow user click to enter fullscreen.
        private var fs_mask:Sprite = new Sprite();
        
        public function srs_player()
        {
            flash.system.Security.allowDomain("*");
            
            if (!this.stage) {
                this.addEventListener(Event.ADDED_TO_STAGE, this.onAddToStage);
            } else {
                this.onAddToStage(null);
            }
        }
        
        private function onAddToStage(evt:Event):void {
            this.stage.align = StageAlign.TOP_LEFT;
            this.stage.scaleMode = StageScaleMode.NO_SCALE;
            
            this.stage.addEventListener(FullScreenEvent.FULL_SCREEN, this.on_stage_fullscreen);
            
            this.addChild(this.fs_mask);
            this.fs_mask.buttonMode = true;
            this.fs_mask.addEventListener(MouseEvent.CLICK, on_user_click_video);
            
            this.contextMenu = new ContextMenu();
            this.contextMenu.hideBuiltInItems();
            
            var flashvars:Object = this.root.loaderInfo.parameters;
            
            if (!flashvars.hasOwnProperty("id")) {
                throw new Error("must specifies the id");
            }
            if (!flashvars.hasOwnProperty("on_player_ready")) {
                throw new Error("must specifies the on_player_ready");
            }
            
            this.id = flashvars.id;
            this.on_player_ready = flashvars.on_player_ready;
            this.on_player_metadata = flashvars.on_player_metadata;
            
            flash.utils.setTimeout(this.on_js_ready, 0);
        }
        
        private function on_stage_fullscreen(evt:FullScreenEvent):void {
            if (!evt.fullScreen) {
                execute_user_set_dar();
            } else {
                execute_user_enter_fullscreen();
            }
        }
        
        private function on_js_ready():void {
            if (!flash.external.ExternalInterface.available) {
                trace("js not ready, try later.");
                flash.utils.setTimeout(this.on_js_ready, 100);
                return;
            }
            
            flash.external.ExternalInterface.addCallback("__play", this.js_call_play);
            flash.external.ExternalInterface.addCallback("__stop", this.js_call_stop);
            flash.external.ExternalInterface.addCallback("__pause", this.js_call_pause);
            flash.external.ExternalInterface.addCallback("__resume", this.js_call_resume);
            flash.external.ExternalInterface.addCallback("__dar", this.js_call_dar);
            flash.external.ExternalInterface.addCallback("__set_fs", this.js_call_set_fs_size);
            
            var code:int = flash.external.ExternalInterface.call(this.on_player_ready, this.id);
            if (code != 0) {
                throw new Error("callback on_player_ready failed. code=" + code);
            }
        }
        
        private function js_call_pause():int {
            if (this.stream) {
                this.stream.pause();
            }
            return 0;
        }
        
        private function js_call_resume():int {
            if (this.stream) {
                this.stream.resume();
            }
            return 0;
        }
        
        /**
         * to set the DAR, for example, DAR=16:9
         * @param num, for example, 9. 
         *       use metadata height if 0.
         *       use user specified height if -1.
         * @param den, for example, 16. 
         *       use metadata width if 0.
         *       use user specified width if -1.
         */
        private function js_call_dar(num:int, den:int):int {
            dar_num = num;
            dar_den = den;
            
            flash.utils.setTimeout(execute_user_set_dar, 0);
            return 0;
        }
        
        /**
         * set the fullscreen size data.
         * @refer the refer fullscreen mode. it can be:
         *       video: use video orignal size.
         *       screen: use screen size to rescale video.
         * @param percent, the rescale percent, where
         *       100 means 100%.
         */
        private function js_call_set_fs_size(refer:String, percent:int):int {
            fs_refer = refer;
            fs_percent = percent;
            
            return 0;
        }
        /**
        * js cannot enter the fullscreen mode, user must click to.
        */
        private function on_user_click_video(evt:MouseEvent):void {
            if (!this.stage.allowsFullScreen) {
                trace("donot allow fullscreen.");
                return;
            }
            
            // enter fullscreen to get the fullscreen size correctly.
            if (this.stage.displayState == StageDisplayState.FULL_SCREEN) {
                this.stage.displayState = StageDisplayState.NORMAL;
            } else {
                this.stage.displayState = StageDisplayState.FULL_SCREEN;
            }
        }
        
        private function js_call_stop():int {
            if (this.stream) {
                this.stream.close();
                this.stream = null;
            }
            if (this.conn) {
                this.conn.close();
                this.conn = null;
            }
            if (this.video) {
                this.removeChild(this.video);
                this.video = null;
            }
            
            return 0;
        }
        
        private function draw_black_background(_width:int, _height:int):void {
            // draw black bg.
            this.graphics.beginFill(0x00, 1.0);
            this.graphics.drawRect(0, 0, _width, _height);
            this.graphics.endFill();
            
            // draw the fs mask.
            this.fs_mask.graphics.beginFill(0xff0000, 0);
            this.fs_mask.graphics.drawRect(0, 0, _width, _height);
            this.fs_mask.graphics.endFill();
        }
        
        private function js_call_play(url:String, _width:int, _height:int, _buffer_time:Number):int {
            this.url = url;
            this.w = _width;
            this.h = _height;
            trace("start to play url: " + this.url + ", w=" + this.w + ", h=" + this.h);
            
            js_call_stop();
            
            this.conn = new NetConnection();
            this.conn.client = {};
            this.conn.client.onBWDone = function():void {};
            this.conn.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                trace ("NetConnection: code=" + evt.info.code);
                if (evt.info.code != "NetConnection.Connect.Success") {
                    return;
                }
                
                stream = new NetStream(conn);
                stream.bufferTime = _buffer_time;
                stream.client = {};
                stream.client.onMetaData = on_metadata;
                stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                    trace ("NetStream: code=" + evt.info.code);
                    
                    if (evt.info.code == "NetStream.Video.DimensionChange") {
                        on_metadata(metadata);
                    }
                });
                stream.play(url.substr(url.lastIndexOf("/")));
                
                video = new Video();
                video.width = _width;
                video.height = _height;
                video.attachNetStream(stream);
                video.smoothing = true;
                addChild(video);
                
                draw_black_background(_width, _height);
                
                // lowest layer, for mask to cover it.
                setChildIndex(video, 0);
            });
            this.conn.connect(this.url.substr(0, this.url.lastIndexOf("/")));
            
            return 0;
        }
        
        private function on_metadata(metadata:Object):void {
            this.metadata = metadata;
            
            // for context menu
            var customItems:Array = [new ContextMenuItem("SrsPlayer")];
            if (metadata.hasOwnProperty("server")) {
                customItems.push(new ContextMenuItem("Server: " + metadata.server));
            }
            if (metadata.hasOwnProperty("contributor")) {
                customItems.push(new ContextMenuItem("Contributor: " + metadata.contributor));
            }
            contextMenu.customItems = customItems;
            
            // for js.
            var obj:Object = get_video_size_object();
            
            obj.server = 'srs';
            obj.contributor = 'winlin';
            
            if (metadata.hasOwnProperty("server")) {
                obj.server = metadata.server;
            }
            if (metadata.hasOwnProperty("contributor")) {
                obj.contributor = metadata.contributor;
            }
            
            var code:int = flash.external.ExternalInterface.call(on_player_metadata, id, obj);
            if (code != 0) {
                throw new Error("callback on_player_metadata failed. code=" + code);
            }
        }
        
        /**
        * get the "right" size of video,
        * 1. initialize with the original video object size.
        * 2. override with metadata size if specified.
        * 3. override with codec size if specified.
        */
        private function get_video_size_object():Object {
            var obj:Object = {
                width: video.width,
                height: video.height
            };
            
            // override with metadata size.
            if (metadata.hasOwnProperty("width")) {
                obj.width = metadata.width;
            }
            if (metadata.hasOwnProperty("height")) {
                obj.height = metadata.height;
            }
            
            // override with codec size.
            if (video.videoWidth > 0) {
                obj.width = video.videoWidth;
            }
            if (video.videoHeight > 0) {
                obj.height = video.videoHeight;
            }
            
            return obj;
        }
        
        /**
        * execute the enter fullscreen action.
        */
        private function execute_user_enter_fullscreen():void {
            if (!fs_refer || fs_percent <= 0) {
                return;
            }
            
            // change to video size if refer to video.
            var obj:Object = get_video_size_object();
            
            // get the DAR
            var num:int = dar_num;
            var den:int = dar_den;
            
            if (num == 0) {
                num = obj.height;
            }
            if (num == -1) {
                num = this.stage.fullScreenHeight;
            }
            
            if (den == 0) {
                den = obj.width;
            }
            if (den == -1) {
                den = this.stage.fullScreenWidth;
            }
                
            // for refer is screen.
            if (fs_refer == "screen") {
                obj = {
                    width: this.stage.fullScreenWidth,
                    height: this.stage.fullScreenHeight
                };
            }
            
            // rescale to fs
            update_video_size(num, den, obj.width * fs_percent / 100, obj.height * fs_percent / 100, this.stage.fullScreenWidth, this.stage.fullScreenHeight);
        }
        
        /**
         * for user set dar, or leave fullscreen to recover the dar.
         */
        private function execute_user_set_dar():void {
            // get the DAR
            var num:int = dar_num;
            var den:int = dar_den;
            
            var obj:Object = get_video_size_object();
            
            if (num == 0) {
                num = obj.height;
            }
            if (num == -1) {
                num = this.h;
            }
            
            if (den == 0) {
                den = obj.width;
            }
            if (den == -1) {
                den = this.w;
            }
            
            update_video_size(num, den, this.w, this.h, this.w, this.h);
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
        private function update_video_size(_num:int, _den:int, _w:int, _h:int, _sw:int, _sh:int):void {
            if (!this.video || _num <= 0 || _den <= 0) {
                return;
            }
            
            // set DAR.
            // calc the height by DAR
            var _height:int = _w * _num / _den;
            if (_height <= _h) {
                this.video.width = _w;
                this.video.height = _height;
            } else {
                // height overflow, calc the width by DAR
                var _width:int = _h * _den / _num;
                
                this.video.width = _width;
                this.video.height = _h;
            }
            
            // align center.
            this.video.x = (_sw - this.video.width) / 2;
            this.video.y = (_sh - this.video.height) / 2;
            
            draw_black_background(_sw, _sh);
        }
    }
}