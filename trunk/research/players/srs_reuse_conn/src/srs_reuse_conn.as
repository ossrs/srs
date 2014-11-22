package
{
    import fl.controls.Button;
    import fl.controls.TextInput;
    
    import flash.display.Sprite;
    import flash.display.StageAlign;
    import flash.display.StageScaleMode;
    import flash.events.Event;
    import flash.events.MouseEvent;
    import flash.events.NetStatusEvent;
    import flash.media.Video;
    import flash.net.NetConnection;
    import flash.net.NetStream;
    
    [SWF(backgroundColor="0xEEEEEE",frameRate="30",width="1024",height="576")]
    public class srs_reuse_conn extends Sprite
    {
        public function srs_reuse_conn()
        {
            if (stage) {
                onAddedToStage(null);
            } else {
                addEventListener(Event.ADDED_TO_STAGE, onAddedToStage);
            }
        }
        
        private function onAddedToStage(evt:Event):void
        {
            stage.align = StageAlign.TOP_LEFT;
            stage.scaleMode = StageScaleMode.NO_SCALE;
            
            var txtUrl:TextInput = new TextInput();
            var btnConn:Button = new Button();
            var btnPlay:Button = new Button();
            
            txtUrl.x = 10;
            txtUrl.y = 10;
            txtUrl.width = 400;
            txtUrl.text = "rtmp://dev/live/livestream";
            addChild(txtUrl);
            
            btnConn.label = "Connect";
            btnConn.x = txtUrl.x + txtUrl.width + 10;
            btnConn.y = txtUrl.y;
            btnConn.width = 100;
            addChild(btnConn);
            
            btnPlay.label = "Play";
            btnPlay.x = btnConn.x + btnConn.width + 10;
            btnPlay.y = btnConn.y;
            btnPlay.width = 100;
            addChild(btnPlay);
            
            var video:Video = new Video();
            video.x = txtUrl.x;
            video.y = txtUrl.y + txtUrl.height + 10;
            addChild(video);
            
            var conn:NetConnection = null;
            var stream:NetStream = null;
            
            var tcUrl:Function = function():String {
                var url:String = txtUrl.text;
                return url.substr(0, url.lastIndexOf("/"));
            }
            var streamName:Function = function():String {
                var url:String = txtUrl.text;
                return url.substr(tcUrl().length + 1);
            }
            
            var closeConnection:Function = function():void {
                if (stream) {
                    stream.close();
                    stream = null;
                }
                if (conn) {
                    conn.close();
                    conn = null;
                }
                btnConn.label = "Connect";
                btnPlay.visible = false;
            };
            
            btnPlay.visible = false;
            btnConn.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void {
                if (btnConn.label == "Connect") {
                    conn = new NetConnection();
                    conn.client = {
                        onBWDone: function():void{}
                    };
                    conn.addEventListener(NetStatusEvent.NET_STATUS, function(ne:NetStatusEvent):void {
                        if (ne.info.code == "NetConnection.Connect.Success") {
                            btnPlay.visible = true;
                        } else if (ne.info.code == "NetConnection.Connect.Closed") {
                            closeConnection();
                        }
                        trace(ne.info.code);
                    });
                    conn.connect(tcUrl());
                    btnConn.label = "Close";
                } else {
                    closeConnection();
                }
            });
            btnPlay.addEventListener(MouseEvent.CLICK, function(e:MouseEvent):void {
                if (stream) {
                    stream.close();
                    stream = null;
                }
                stream = new NetStream(conn);
                stream.client = {
                    onMetaData: function(metadata:Object):void {
                        video.width = metadata.width;
                        video.height = metadata.height;
                    }
                };
                video.attachNetStream(stream);
                stream.play(streamName());
            });
        }
    }
}