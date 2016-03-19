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