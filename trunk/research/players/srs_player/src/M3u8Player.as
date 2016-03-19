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

    /**
     * the m3u8 player.
     */
    public class M3u8Player implements IPlayer
    {
        private var js_id:String = null;

        // play param url.
        private var user_url:String = null;

        private var media_stream:NetStream = null;
        private var media_conn:NetConnection = null;

        private var owner:srs_player = null;

        public function M3u8Player(o:srs_player) {
            owner = o;
        }

        public function init(flashvars:Object):void {
            this.js_id = flashvars.id;
        }

        public function stream():NetStream {
            return this.media_stream;
        }

        public function play(url:String):void {
            this.user_url = url;

            this.media_conn = new NetConnection();
            this.media_conn.client = {};
            this.media_conn.client.onBWDone = function():void {};
            this.media_conn.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                log("NetConnection: code=" + evt.info.code);

                if (evt.info.hasOwnProperty("data") && evt.info.data) {
                    owner.on_player_metadata(evt.info.data);
                }

                // reject by server, maybe redirect.
                if (evt.info.code == "NetConnection.Connect.Rejected") {
                    // RTMP 302 redirect.
                    if (evt.info.hasOwnProperty("ex") && evt.info.ex.code == 302) {
                        streamName = url.substr(url.lastIndexOf("/") + 1);
                        url = evt.info.ex.redirect + "/" + streamName;
                        log("Async RTMP 302 Redirect to: " + url);

                        // notify server.
                        media_conn.call("Redirected", null, evt.info.ex.redirect);

                        // do 302.
                        owner.on_player_302(url);
                        return;
                    }
                }

                // TODO: FIXME: failed event.
                if (evt.info.code != "NetConnection.Connect.Success") {
                    return;
                }

                media_stream = new NetStream(media_conn);
                media_stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                    log("NetStream: code=" + evt.info.code);

                    if (evt.info.code == "NetStream.Video.DimensionChange") {
                        owner.on_player_dimension_change();
                    } else if (evt.info.code == "NetStream.Buffer.Empty") {
                        owner.on_player_buffer_empty();
                    } else if (evt.info.code == "NetStream.Buffer.Full") {
                        owner.on_player_buffer_full();
                    }

                    // TODO: FIXME: failed event.
                });

                // setup stream before play.
                owner.on_player_before_play();

                if (url.indexOf("http") == 0) {
                    media_stream.play(url);
                } else {
                    streamName = url.substr(url.lastIndexOf("/") + 1);
                    media_stream.play(streamName);
                }

                owner.on_player_play();
            });

            this.media_conn.connect(null);
        }

        public function close():void {
            if (this.media_stream) {
                this.media_stream.close();
                this.media_stream = null;
            }
            if (this.media_conn) {
                this.media_conn.close();
                this.media_conn = null;
            }
        }

        private function log(msg:String):void {
            Utility.log(js_id, msg);
        }
    }
}