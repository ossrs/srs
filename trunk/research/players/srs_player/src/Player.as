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
    import flash.net.URLVariables;
    import flash.system.Security;
    import flash.ui.ContextMenu;
    import flash.ui.ContextMenuItem;
    import flash.utils.Timer;
    import flash.utils.getTimer;
    import flash.utils.setTimeout;
    
    import flashx.textLayout.formats.Float;

    /**
     * common player to play rtmp/flv stream,
     * use system NetStream.
     */
    public class Player
    {
		// refresh every ts_fragment_seconds*M3u8RefreshRatio
		public static var M3u8RefreshRatio:Number = 0.3;
		
		// parse ts every this ms.
		public static var TsParseAsyncInterval:Number = 80;
		
        private var js_id:String = null;

        // play param url.
        private var user_url:String = null;

        private var media_stream:NetStream = null;
        private var media_conn:NetConnection = null;

        private var owner:srs_player = null;

        public function Player(o:srs_player) {
            owner = o;
        }

        public function init(flashvars:Object):void {
            this.js_id = flashvars.id;
        }

        public function stream():NetStream {
            return this.media_stream;
        }
		
		private function dumps_object(obj:Object):String {
			var smr:String = "";
			for (var k:String in obj) {
				smr += k + "=" + obj[k] + ", ";
			}
			
			return smr;
		}

        public function play(url:String):void {
			owner.on_player_status("init", "Ready to play");
			
			var streamName:String;
            this.user_url = url;

            this.media_conn = new NetConnection();
            this.media_conn.client = {};
            this.media_conn.client.onBWDone = function():void {};
            this.media_conn.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                log("NetConnection: type=" + evt.type + ", bub=" + evt.bubbles + ", can=" + evt.cancelable 
					+ ", info is " + dumps_object(evt.info));

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
					
					owner.on_player_status("rejected", "Server reject play");
					close();
                }
				
				if (evt.info.code == "NetConnection.Connect.Success") {
					owner.on_player_status("connected", "Connected at server");
				}
				if (evt.info.code == "NetConnection.Connect.Closed") {
					close();
				}
				if (evt.info.code == "NetConnection.Connect.Failed") {
					owner.on_player_status("failed", "Connect to server failed.");
					close();
				}

                // TODO: FIXME: failed event.
                if (evt.info.code != "NetConnection.Connect.Success") {
                    return;
                }

				if (url.indexOf(".m3u8") > 0) {
					media_stream = new HlsNetStream(M3u8RefreshRatio, TsParseAsyncInterval, media_conn);
				} else {
                	media_stream = new NetStream(media_conn);
				}
                media_stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
					log("NetStream: type=" + evt.type + ", bub=" + evt.bubbles + ", can=" + evt.cancelable 
						+ ", info is " + dumps_object(evt.info));
					
					if (evt.info.code == "NetStream.Play.Start") {
						owner.on_player_status("play", "Start to play stream");
					}
					if (evt.info.code == "NetStream.Play.StreamNotFound") {
						owner.on_player_status("rejected", "Stream not found");
						close();
					}

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

            if (url.indexOf("http") == 0) {
                this.media_conn.connect(null);
            } else {
                var tcUrl:String = this.user_url.substr(0, this.user_url.lastIndexOf("/"));
				streamName = url.substr(url.lastIndexOf("/") + 1);
				
                // parse vhost from stream query.
				if (streamName.indexOf("?") >= 0) {
					var uv:URLVariables = new URLVariables(user_url.substr(user_url.indexOf("?") + 1));	
					var domain:String = uv["domain"];
					if (!domain) {
						domain = uv["vhost"];
					}
					if (domain) {
						tcUrl += "?vhost=" + domain;
					}
				}
				
                this.media_conn.connect(tcUrl);
            }
        }

        public function close():void {
			var notify:Boolean = false;
			
            if (this.media_stream) {
                this.media_stream.close();
                this.media_stream = null;
				notify = true;
            }
			
            if (this.media_conn) {
                this.media_conn.close();
                this.media_conn = null;
				notify = true;
            }
			
			if (notify) {
				owner.on_player_status("closed", "Server closed.");
			}
        }

        private function log(msg:String):void {
            Utility.log(js_id, msg);
        }
    }
}