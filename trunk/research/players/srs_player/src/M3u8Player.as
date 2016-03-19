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
    import flash.events.ProgressEvent;
    import flash.events.TimerEvent;
    import flash.external.ExternalInterface;
    import flash.media.SoundTransform;
    import flash.media.Video;
    import flash.net.NetConnection;
    import flash.net.NetStream;
    import flash.net.NetStreamAppendBytesAction;
    import flash.net.URLLoader;
    import flash.net.URLLoaderDataFormat;
    import flash.net.URLRequest;
    import flash.net.URLRequestHeader;
    import flash.net.URLRequestMethod;
    import flash.net.URLStream;
    import flash.net.URLVariables;
    import flash.system.Security;
    import flash.ui.ContextMenu;
    import flash.ui.ContextMenuItem;
    import flash.utils.ByteArray;
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
        private var hls:Hls = null; // parse m3u8 and ts

        // the uuid similar to Safari, to identify this play session.
        // @see https://github.com/winlinvip/srs-plus/blob/bms/trunk/src/app/srs_app_http_stream.cpp#L45
        public var XPlaybackSessionId:String = createRandomIdentifier(32);
        private function createRandomIdentifier(length:uint, radix:uint = 61):String {
            var characters:Array = new Array('0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q',
                'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h',
                'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y',
                'z');
            var id:Array  = new Array();
            radix = (radix > 61) ? 61 : radix;
            while (length--) {
                id.push(characters[randomIntegerWithinRange(0, radix)]);
            }
            return id.join('');
        }
        private function randomIntegerWithinRange(min:int, max:int):int {
            return Math.floor(Math.random() * (1 + max - min) + min);
        }
		
		// callback for hls.
		public var flvHeader:ByteArray = null;
		public function onSequenceHeader():void {
			if (!media_stream) {
				setTimeout(onSequenceHeader, 1000);
				return;
			}
			
			var s:NetStream = media_stream;
			s.appendBytesAction(NetStreamAppendBytesAction.RESET_BEGIN);
			s.appendBytes(flvHeader);
			log("FLV: sps/pps " + flvHeader.length + " bytes");
			
			writeFlv(flvHeader);
		}
		public function onFlvBody(uri:String, flv:ByteArray):void {
			if (!media_stream) {
				return;
			}
			
			if (!flvHeader) {
				return;
			}
			
			var s:NetStream = media_stream;
			s.appendBytes(flv);
			log("FLV: ts " + uri + " parsed to flv " + flv.length + " bytes");
			
			writeFlv(flv);
		}
		private function writeFlv(data:ByteArray):void {
            return;
            
			var r:URLRequest = new URLRequest("http://192.168.1.117:8088/api/v1/flv");
			r.method = URLRequestMethod.POST;
			r.data = data;
			
			var pf:URLLoader = new URLLoader();
			pf.dataFormat = URLLoaderDataFormat.BINARY;
			pf.load(r);
		}

        public function M3u8Player(o:srs_player) {
            owner = o;
            hls = new Hls(this);
        }

        public function init(flashvars:Object):void {
            this.js_id = flashvars.id;
        }

        public function stream():NetStream {
            return this.media_stream;
        }

        // owner.on_player_metadata(evt.info.data);
        public function play(url:String):void {
			var streamName:String;
            this.user_url = url;

            this.media_conn = new NetConnection();
            this.media_conn.client = {};
            this.media_conn.client.onBWDone = function():void {};
            this.media_conn.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                log("NetConnection: code=" + evt.info.code + ", data is " + evt.info.data);

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

                media_stream.play(null);
                refresh_m3u8();

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

        private var parsed_ts_seq_no:Number = -1;
        private function refresh_m3u8():void {
            download(user_url, function(stream:ByteArray):void {
                var m3u8:String = stream.toString();
                hls.parse(user_url, m3u8);

                // redirect by variant m3u8.
                if (hls.variant) {
                    var smu:String = hls.getTsUrl(0);
                    log("variant hls=" + user_url + ", redirect2=" + smu);
                    user_url = smu;
                    setTimeout(refresh_m3u8, 0);
                    return;
                }

                // fetch from the last one.
                if (parsed_ts_seq_no == -1) {
                    parsed_ts_seq_no = hls.seq_no + hls.tsCount - 1;
                }

                // not changed.
                if (parsed_ts_seq_no >= hls.seq_no + hls.tsCount) {
                    refresh_ts();
                    return;
                }

                // parse each ts.
                var nb_ts:Number = hls.seq_no + hls.tsCount - parsed_ts_seq_no;
                log("m3u8 changed, got " + nb_ts + " new ts, count=" + hls.tsCount + ", seqno=" + hls.seq_no + ", parsed=" + parsed_ts_seq_no);

                refresh_ts();
            })
        }
        private function refresh_ts():void {
            // all ts parsed.
            if (parsed_ts_seq_no >= hls.seq_no + hls.tsCount) {
                var to:Number = 1000;
                if (hls.tsCount > 0) {
                    to = hls.duration * 1000 / hls.tsCount * Consts.M3u8RefreshRatio;
                }
                setTimeout(refresh_m3u8, to);
                log("m3u8 not changed, retry after " + to.toFixed(2) + "ms");
                return;
            }

            // parse current ts.
            var uri:String = hls.getTsUrl(parsed_ts_seq_no - hls.seq_no);
			
			// parse metadata from uri.
			if (uri.indexOf("?") >= 0) {
				var uv:URLVariables = new URLVariables(uri.substr(uri.indexOf("?") + 1));
	            var obj:Object = {};
				for (var k:String in uv) {
					var v:String = uv[k];
	                if (k == "shp_sip1") {
	                    obj.srs_server_ip = v;
	                } else if (k == "shp_cid") {
	                    obj.srs_id = v;
	                } else if (k == "shp_pid") {
	                    obj.srs_pid = v;
	                }
					//log("uv[" + k + "]=" + v);
				}
	            owner.on_player_metadata(obj);
			}
			
            download(uri, function(stream:ByteArray):void{
                log("got ts seqno=" + parsed_ts_seq_no + ", " + stream.length + " bytes");

                var flv:FlvPiece = new FlvPiece(parsed_ts_seq_no);
                var body:ByteArray = new ByteArray();
				stream.position = 0;
                hls.parseBodyAsync(flv, stream, body, function():void{
					body.position = 0;
                    //log("ts parsed, seqno=" + parsed_ts_seq_no + ", flv=" + body.length + "B");
					onFlvBody(uri, body);
                    
                    parsed_ts_seq_no++;
                    setTimeout(refresh_ts, 0);
                });
            });
        }
        private function download(uri:String, completed:Function):void {
            var url:URLStream = new URLStream();
            var stream:ByteArray = new ByteArray();

            url.addEventListener(ProgressEvent.PROGRESS, function(evt:ProgressEvent):void {
                if (url.bytesAvailable <= 0) {
                    return;
                }

                //log(uri + " total=" + evt.bytesTotal + ", loaded=" + evt.bytesLoaded + ", available=" + url.bytesAvailable);
                var bytes:ByteArray = new ByteArray();
                url.readBytes(bytes, 0, url.bytesAvailable);
				stream.writeBytes(bytes);
            });

            url.addEventListener(Event.COMPLETE, function(evt:Event):void {
				log(uri + " completed, total=" + stream.length + "bytes");
                if (url.bytesAvailable <= 0) {
					completed(stream);
                    return;
                }

                //log(uri + " completed" + ", available=" + url.bytesAvailable);
                var bytes:ByteArray = new ByteArray();
                url.readBytes(bytes, 0, url.bytesAvailable);
                stream.writeBytes(bytes);

                completed(stream);
            });

            // we set to the query.
            uri += ((uri.indexOf("?") == -1)? "?":"&") + "shp_xpsid=" + XPlaybackSessionId;
            var r:URLRequest = new URLRequest(uri);
            // seems flash not allow set this header.
			r.requestHeaders.push(new URLRequestHeader("X-Playback-Session-Id", XPlaybackSessionId));

            log("start download " + uri);
            url.load(r);
        }

        private function log(msg:String):void {
            Utility.log(js_id, msg);
        }
    }
}