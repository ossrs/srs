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
        private var id:String = null;
        private var on_player_ready:String = null;
        
        private var url:String = null;
        
        private var conn:NetConnection = null;
        private var stream:NetStream = null;
        private var video:Video = null;
        
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
            trace("start to play url: " + this.url);
            
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
                stream.client.onMetaData = function(metadata:Object):void {
                    var customItems:Array = [new ContextMenuItem("SrsPlayer")];
                    if (metadata.hasOwnProperty("server")) {
                        customItems.push(new ContextMenuItem("Server: " + metadata.server));
                    }
                    if (metadata.hasOwnProperty("contributor")) {
                        customItems.push(new ContextMenuItem("Contributor: " + metadata.contributor));
                    }
                    contextMenu.customItems = customItems;
                };
                stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                    trace ("NetStream: code=" + evt.info.code);
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
    }
}