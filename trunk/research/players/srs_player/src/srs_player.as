package
{
    import flash.display.Sprite;
    import flash.display.StageAlign;
    import flash.display.StageScaleMode;
    import flash.events.Event;
    import flash.events.NetStatusEvent;
    import flash.external.ExternalInterface;
    import flash.media.Video;
    import flash.net.NetConnection;
    import flash.net.NetStream;
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
        // play param, user set width
        private var w:int = 0;
        // play param, user set height.
        private var h:int = 0;
        
        private var conn:NetConnection = null;
        private var stream:NetStream = null;
        private var video:Video = null;
        private var metadata:Object = {};
        
        public function srs_player()
        {
            if (!this.stage) {
                this.addEventListener(Event.ADDED_TO_STAGE, this.onAddToStage);
            } else {
                this.onAddToStage(null);
            }
        }
        
        private function onAddToStage(evt:Event):void {
            this.stage.align = StageAlign.TOP_LEFT;
            this.stage.scaleMode = StageScaleMode.NO_SCALE;
            
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
            
            flash.utils.setTimeout(this.onJsReady, 0);
        }
        
        private function onJsReady():void {
            if (!flash.external.ExternalInterface.available) {
                trace("js not ready, try later.");
                flash.utils.setTimeout(this.onJsReady, 100);
                return;
            }
            
            flash.external.ExternalInterface.addCallback("__play", this.js_call_play);
            flash.external.ExternalInterface.addCallback("__stop", this.js_call_stop);
            flash.external.ExternalInterface.addCallback("__pause", this.js_call_pause);
            flash.external.ExternalInterface.addCallback("__resume", this.js_call_resume);
            flash.external.ExternalInterface.addCallback("__dar", this.js_call_dar);
            
            var code:int = flash.external.ExternalInterface.call(this.on_player_ready, this.id);
            if (code != 0) {
                throw new Error("callback on_player_ready failed. code=" + code);
            }
        }
        
        public function js_call_pause():int {
            if (this.stream) {
                this.stream.pause();
            }
            return 0;
        }
        
        public function js_call_resume():int {
            if (this.stream) {
                this.stream.resume();
            }
            return 0;
        }
        
        /**
        * to set the DAR, for example, DAR=16:9
        * @param num, for example, 9.
        * @param den, for example, 16.
        */
        public function js_call_dar(num:int, den:int):int {
            if (this.video && num > 0 && den > 0 && this.video.width > 0) {
                // set DAR.
                if (num < den) {
                    // calc the height by DAR
                    var _height:int = this.video.width * num / den;
                    
                    // height overflow, calc the width by DAR
                    if (_height > this.h) {
                        var _width:int = this.video.height * den / num;
                        
                        this.video.width = _width;
                        this.video.height = this.h;
                    } else {
                        this.video.width = this.w;
                        this.video.height = _height;
                    }
                }
                
                // align center.
                this.video.y = (this.h - this.video.height) / 2;
                this.video.x = (this.w - this.video.width) / 2;
            }
            return 0;
        }
        
        public function js_call_stop():int {
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
        
        public function js_call_play(url:String, _width:int, _height:int, _buffer_time:Number):int {
            this.url = url;
            this.w = _width;
            this.h = _height;
            trace("start to play url: " + this.url + ", w=" + this.w + ", h=" + this.h);
            
            // draw black bg.
            this.graphics.beginFill(0x00, 1.0);
            this.graphics.drawRect(0, 0, this.w, this.h);
            this.graphics.endFill();
            
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
            var obj:Object = {
                width: video.width,
                    height: video.height,
                    server: 'srs',
                    contributor: 'winlin'
            };
            
            if (metadata.hasOwnProperty("width")) {
                obj.width = metadata.width;
            }
            if (metadata.hasOwnProperty("height")) {
                obj.height = metadata.height;
            }
            
            if (video.videoWidth > 0) {
                obj.width = video.videoWidth;
            }
            if (video.videoHeight > 0) {
                obj.height = video.videoHeight;
            }
            
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
    }
}