package
{
    import flash.events.Event;
    import flash.events.IOErrorEvent;
    import flash.events.NetStatusEvent;
    import flash.events.ProgressEvent;
    import flash.events.SecurityErrorEvent;
    import flash.external.ExternalInterface;
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
    import flash.utils.ByteArray;
    import flash.utils.setTimeout;

    // the NetStream to play hls or hls+.
    public class HlsNetStream extends NetStream
    {
        private var hls:HlsCodec = null; // parse m3u8 and ts

        // for hls codec.
        public var m3u8_refresh_ratio:Number;
        public var ts_parse_async_interval:Number;
		
		// play param url.
		private var user_url:String = null;
		
		private var conn:NetConnection = null;

		/**
		 * create stream to play hls.
		 * @param m3u8_refresh_ratio, for example, 0.5, fetch m3u8 every 0.5*ts_duration.
		 * @param ts_parse_async_interval, for example, 80ms to parse ts async.
		 */
        public function HlsNetStream(m3u8_refresh_ratio:Number, ts_parse_async_interval:Number, conn:NetConnection)
        {
            super(conn);
			
			this.m3u8_refresh_ratio = m3u8_refresh_ratio;
			this.ts_parse_async_interval = ts_parse_async_interval;
			hls = new HlsCodec(this);
			this.conn = conn;
        }
		
		/**
		 * to play the hls stream.
		 * for example, HlsNetStream.play("http://ossrs.net:8080/live/livestream.m3u8").
		 * user can set the metadata callback by:
		 * 		var ns:NetStream = new HlsNetStream(...);
		 * 		ns.client = {};
		 * 		ns.client.onMetaData = system_on_metadata;
		 */
		public override function play(... params):void 
		{
			super.play(null);
			user_url = params[0] as String;
			refresh_m3u8();
		}
		
		/////////////////////////////////////////////////////////////////////////////////////////
		////////////////////////////Private Section//////////////////////////////////////////////
		/////////////////////////////////////////////////////////////////////////////////////////
		
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
		
		private var metadata:Object = null;
		private function refresh_ts():void {
			// all ts parsed.
			if (parsed_ts_seq_no >= hls.seq_no + hls.tsCount) {
				var to:Number = 1000;
				if (hls.tsCount > 0) {
					to = hls.duration * 1000 / hls.tsCount * m3u8_refresh_ratio;
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
				
				// ignore when not changed.
				if (!metadata || metadata.srs_server_ip != obj.srs_server_ip || metadata.srs_id != obj.srs_id || metadata.srs_pid != obj.srs_pid) {
					if (client && client.hasOwnProperty("onMetaData")) {
						log("got metadata for url " + uri);
						client.onMetaData(obj);
					}
				}
				metadata = obj;
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
			// http get.
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
			
			url.addEventListener(IOErrorEvent.IO_ERROR, function(evt:IOErrorEvent):void{
				onPlayFailed(evt);
			});
			
			url.addEventListener(SecurityErrorEvent.SECURITY_ERROR, function(evt:SecurityErrorEvent):void{
				onPlayRejected(evt);
			});
			
			// we set to the query.
			uri += ((uri.indexOf("?") == -1)? "?":"&") + "shp_xpsid=" + XPlaybackSessionId;
			var r:URLRequest = new URLRequest(uri);
			// seems flash not allow set this header.
			// @remark disable it for it will cause security exception.
			//r.requestHeaders.push(new URLRequestHeader("X-Playback-Session-Id", XPlaybackSessionId));

			log("start download " + uri);
			url.load(r);
		}

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
            var s:NetStream = super;
            s.appendBytesAction(NetStreamAppendBytesAction.RESET_BEGIN);
            s.appendBytes(flvHeader);
            log("FLV: sps/pps " + flvHeader.length + " bytes");

            writeFlv(flvHeader);
			onPlayStart();
        }
		
		private function onPlayStart():void {
			log("dispatch NetStream.Play.Start.");
			dispatchEvent(new NetStatusEvent(NetStatusEvent.NET_STATUS, false, false, {
				code: "NetStream.Play.Start",
				stream: user_url,
				descrption: "play start"
			}));
		}
		
		private function onPlayFailed(evt:IOErrorEvent):void {
			log("dispatch NetConnection.Connect.Failed.");
			this.conn.dispatchEvent(new NetStatusEvent(NetStatusEvent.NET_STATUS, false, false, {
				code: "NetConnection.Connect.Failed",
				stream: user_url,
				descrption: evt.text
			}));
		}
		
		private function onPlayRejected(evt:SecurityErrorEvent):void {
			log("dispatch NetConnection.Connect.Rejected.");
			this.conn.dispatchEvent(new NetStatusEvent(NetStatusEvent.NET_STATUS, false, false, {
				code: "NetConnection.Connect.Rejected",
				stream: user_url,
				descrption: evt.text
			}));
		}

        private function onFlvBody(uri:String, flv:ByteArray):void {
            if (!flvHeader) {
                return;
            }

            var s:NetStream = super;
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
		
		private function log(msg:String):void {
			msg = "[" + new Date() +"][srs-player] " + msg;
			
			trace(msg);
			
			if (!flash.external.ExternalInterface.available) {
				return;
			}
			
			ExternalInterface.call("console.log", msg);
		}
    }
}

import flash.events.Event;
import flash.net.URLLoader;
import flash.net.URLRequest;
import flash.net.URLRequestMethod;
import flash.utils.ByteArray;

/**
 * the hls main class.
 */
class HlsCodec
{
    private var m3u8:M3u8;

    private var avc:SrsRawH264Stream;
    private var h264_sps:ByteArray;
    private var h264_pps:ByteArray;

    private var aac:SrsRawAacStream;
    private var aac_specific_config:ByteArray;
    private var width:int;
    private var height:int;

    private var video_sh_tag:ByteArray;
    private var audio_sh_tag:ByteArray;

    private var owner:HlsNetStream;
    private var _log:ILogger = new TraceLogger("HLS");

    public static const SRS_TS_PACKET_SIZE:int = 188;

    public function HlsCodec(o:HlsNetStream)
    {
        owner = o;
        m3u8 = new M3u8(this);

        reset();
    }

    /**
     * parse the m3u8.
     * @param url, the m3u8 url, for m3u8 to generate the ts url.
     * @param v, the m3u8 string.
     */
    public function parse(url:String, v:String):void
    {
        // TODO: FIXME: reset the hls when parse.
        m3u8.parse(url, v);
    }

    /**
     * get the total count of ts in m3u8.
     */
    public function get tsCount():Number
    {
        return m3u8.tsCount;
    }

    /**
    * get the total duration in seconds of m3u8.
    */
    public function get duration():Number
    {
        return m3u8.duration;
    }

    /**
     * get the sequence number, the id of first ts.
     */
    public function get seq_no():Number
    {
        return m3u8.seq_no;
    }

    /**
     * whether the m3u8 contains variant m3u8.
     */
    public function get variant():Boolean
    {
        return m3u8.variant;
    }

    /**
    * dumps the metadata, for example, set the width and height,
    * which is decoded from sps.
    */
    public function dumpMetaData(metadata:Object):void
    {
        if (width > 0) {
            metadata.width = width;
        }
        if (height > 0) {
            metadata.height = height;
        }
    }

    /**
    * get the ts url by piece id, which is actually the piece index.
    */
    public function getTsUrl(pieceId:Number):String
    {
        return m3u8.getTsUrl(pieceId);
    }

    /**
    * reset the HLS when parse m3u8.
    */
    public function reset():void
    {
        avc = new SrsRawH264Stream();
        h264_sps = new ByteArray();
        h264_pps = new ByteArray();

        aac = new SrsRawAacStream();
        aac_specific_config = new ByteArray();

        width = 0;
        height = 0;

        video_sh_tag = new ByteArray();
        audio_sh_tag = new ByteArray();
    }

    /**
     * parse the piece in hls format,
     * set the piece.skip if error.
     * @param onParsed, a function(piece:FlvPiece, body:ByteArray):void callback.
     */
    public function parseBodyAsync(piece:FlvPiece, data:ByteArray, body:ByteArray, onParsed:Function):void
    {
        var handler:SrsTsHanlder = new SrsTsHanlder(
            avc, aac,
            h264_sps, h264_pps,
            aac_specific_config,
            video_sh_tag, audio_sh_tag,
            this, body,
            _on_size_changed, _on_sequence_changed
        );

        // the context used to parse the whole ts file.
        var context:SrsTsContext = new SrsTsContext(this);

        // we assumpt to parse the piece in 10 times.
        // the total parse time is 10*AlgP2P.HlsAsyncParseTimeout
        var ts_packets:uint = data.length / SRS_TS_PACKET_SIZE;
        var each_parse:uint = ts_packets / 10;
        var nb_parsed:uint = 0;
        var aysncParse:Function = function():void {
            try {
                // do the parse.
                doParseBody(piece, data, body, handler, context, each_parse);

                // check whether parsed.
                nb_parsed += each_parse;

                if (nb_parsed < ts_packets) {
                    flash.utils.setTimeout(aysncParse, owner.ts_parse_async_interval);
                    return;
                }

                // flush the messages in queue.
                handler.flush_message_queue(body);

                __report(body);
                _log.info("hls async parsed to flv, piece={0}, hls={1}B, flv={2}B", piece.pieceId, data.length, body.length);
            } catch (e:Error) {
                piece.skip = true;
                _log.error("hls async parse piece={0}, exception={1}, stack={2}",
                    piece.pieceId, e.message, e.getStackTrace());
            }

            onParsed(piece, body);
        };

        aysncParse();
    }

    /**
     * parse the piece in hls format,
     * set the piece.skip if error.
     */
    public function parseBody(piece:FlvPiece, data:ByteArray, body:ByteArray):void
    {
        try {
            var handler:SrsTsHanlder = new SrsTsHanlder(
                avc, aac,
                h264_sps, h264_pps,
                aac_specific_config,
                video_sh_tag, audio_sh_tag,
                this, body,
                _on_size_changed, _on_sequence_changed
            );

            // the context used to parse the whole ts file.
            var context:SrsTsContext = new SrsTsContext(this);

            // do the parse.
            doParseBody(piece, data, body, handler, context, -1);

            // flush the messages in queue.
            handler.flush_message_queue(body);

            __report(body);
            _log.info("hls sync parsed to flv, piece={0}, hls={1}B, flv={2}B", piece.pieceId, data.length, body.length);
        } catch (e:Error) {
            piece.skip = true;
            _log.error("hls sync parse piece={0}, exception={1}, stack={2}",
                piece.pieceId, e.message, e.getStackTrace());
        }
    }

    private function _on_size_changed(w:int, h:int):void
    {
        width = w;
        height = h;
    }

    private function _on_sequence_changed(
        pavc:SrsRawH264Stream, paac:SrsRawAacStream,
        ph264_sps:ByteArray, ph264_pps:ByteArray,
        paac_specific_config:ByteArray,
        pvideo_sh_tag:ByteArray, paudio_sh_tag:ByteArray,
        sh:ByteArray):void
    {
        // when sequence header not changed, ignore.
        if (SrsUtils.array_equals(h264_sps, ph264_sps)) {
            if (SrsUtils.array_equals(h264_pps, ph264_pps)) {
                if (SrsUtils.array_equals(aac_specific_config, paac_specific_config)) {
                    return;
                }
            }
        }

        avc = pavc;
        h264_sps = ph264_sps;
        h264_pps = ph264_pps;

        aac = paac;
        aac_specific_config = paac_specific_config;

        video_sh_tag = pvideo_sh_tag;
        audio_sh_tag = paudio_sh_tag;

        _log.info("hls: got sequence header, ash={0}B, bsh={1}B", audio_sh_tag.length, video_sh_tag.length);
        owner.flvHeader = sh;
        owner.onSequenceHeader();

        __report(sh);
    }

    /**
    * do the parse.
    * @maxTsPackets the max ts packets to parse, stop when exceed this ts packet.
    *       -1 to parse all packets.
    */
    private function doParseBody(
        piece:FlvPiece, data:ByteArray, body:ByteArray,
        handler:SrsTsHanlder, context:SrsTsContext, maxTsPackets:int):void
    {
        for (var i:int = 0; (maxTsPackets == -1 || i < maxTsPackets) && data.bytesAvailable > 0; i++) {
            var tsBytes:ByteArray = new ByteArray();
            data.readBytes(tsBytes, 0, HlsCodec.SRS_TS_PACKET_SIZE);
            context.decode(tsBytes, handler);
        }
    }

    private function __report(flv:ByteArray):void
    {
        // report only for debug.
        return;

        var url:URLRequest = new URLRequest("http://192.168.10.108:1980/api/v3/file");
        url.data = flv;
        url.method = URLRequestMethod.POST;

        var loader:URLLoader = new URLLoader();
        loader.addEventListener(Event.COMPLETE, function(e:Event):void {
            loader.close();
        });
        loader.load(url);
    }
}

import flash.utils.Dictionary;

class Dict
{
    private var _dict:Dictionary;
    private var _size:uint;

    public function Dict()
    {
        clear();
    }

    /**
     * get the underlayer dict.
     * @remark for core-ng.
     */
    public function get dict():Dictionary
    {
        return _dict;
    }

    public function has(key:Object):Boolean
    {
        return (key in _dict);
    }

    public function get(key:Object):Object
    {
        return ((key in _dict) ? _dict[key] : null);
    }

    public function set(key:Object, object:Object):void
    {
        if (!(key in _dict))
        {
            _size++;
        }
        _dict[key] = object;
    }

    public function remove(key:Object):Object
    {
        var object:Object;
        if (key in _dict)
        {
            object = _dict[key];
            delete _dict[key];
            _size--;
        }
        return (object);
    }

    public function keys():Array
    {
        var array:Array = new Array(_size);
        var index:int;
        for (var key:Object in _dict)
        {
            var _local6:int = index++;
            array[_local6] = key;
        }
        return (array);
    }

    public function values():Array
    {
        var array:Array = new Array(_size);
        var index:int;
        for each (var value:Object in _dict)
        {
            var _local6:int = index++;
            array[_local6] = value;
        };
        return (array);
    }

    public function clear():void
    {
        _dict = new Dictionary();
        _size = 0;
    }

    public function toArray():Array
    {
        var array:Array = new Array(_size * 2);
        var index:int;
        for (var key:Object in _dict)
        {
            var _local6:int = index++;
            array[_local6] = key;
            var _local7:int = index++;
            array[_local7] = _dict[key];
        };
        return (array);
    }

    public function toObject():Object
    {
        return (toArray());
    }

    public function fromObject(object:Object):void
    {
        clear();
        var index:uint;
        while (index < (object as Array).length) {
            set((object as Array)[index], (object as Array)[(index + 1)]);
            index += 2;
        };
    }

    public function get size():uint
    {
        return (_size);
    }

}

import flash.utils.ByteArray;

/**
 * a piece of flv, fetch from cdn or p2p.
 */
class FlvPiece
{
    private var _pieceId:Number;
    protected var _flv:ByteArray;
    /**
     * the private object for the channel,
     * for example, the cdn channel will set to CdnEdge object.
     */
    private var _privateObject:Object;
    /**
     * when encoder error, this piece cannot be generated,
     * and it should be skip. default to false.
     */
    private var _skip:Boolean;

    public function FlvPiece(pieceId:Number)
    {
        _pieceId = pieceId;
        _flv = null;
        _skip = false;
    }

    /**
     * when piece is fetch ok.
     */
    public function onPieceDone(flv:ByteArray):void
    {
        // save body.
        _flv = flv;
    }

    /**
     * when piece is fetch error.
     */
    public function onPieceError():void
    {
    }

    /**
     * when piece is empty.
     */
    public function onPieceEmpty():void
    {
    }

    /**
     * destroy the object, set reference to null.
     */
    public function destroy():void
    {
        _privateObject = null;
        _flv = null;
    }

    public function get privateObject():Object
    {
        return _privateObject;
    }

    public function set privateObject(v:Object):void
    {
        _privateObject = v;
    }

    public function get skip():Boolean
    {
        return _skip;
    }

    public function set skip(v:Boolean):void
    {
        _skip = v;
    }

    public function get pieceId():Number
    {
        return _pieceId;
    }

    public function get flv():ByteArray
    {
        return _flv;
    }

    public function get completed():Boolean
    {
        return _flv != null;
    }
}

interface ILogger
{
    function debug0(message:String, ... rest):void;
    function debug(message:String, ... rest):void;
    function info(message:String, ... rest):void;
    function warn(message:String, ... rest):void;
    function error(message:String, ... rest):void;
    function fatal(message:String, ... rest):void;
}

import flash.globalization.DateTimeFormatter;
import flash.external.ExternalInterface;

class TraceLogger implements ILogger
{
    private var _category:String;

    public function get category():String
    {
        return _category;
    }
    public function TraceLogger(category:String)
    {
        _category = category;
    }
    public function debug0(message:String, ...rest):void
    {
    }

    public function debug(message:String, ...rest):void
    {
    }

    public function info(message:String, ...rest):void
    {
        logMessage(LEVEL_INFO, message, rest);
    }

    public function warn(message:String, ...rest):void
    {
        logMessage(LEVEL_WARN, message, rest);
    }

    public function error(message:String, ...rest):void
    {
        logMessage(LEVEL_ERROR, message, rest);
    }

    public function fatal(message:String, ...rest):void
    {
        logMessage(LEVEL_FATAL, message, rest);
    }
    protected function logMessage(level:String, message:String, params:Array):void
    {
        var msg:String = "";

        // add datetime
        var date:Date = new Date();
        var dtf:DateTimeFormatter = new DateTimeFormatter("UTC");
        dtf.setDateTimePattern("yyyy-MM-dd HH:mm:ss");

        // TODO: FIXME: the SSS format not run, use date.milliseconds instead.
        msg += '[' + dtf.format(date) + "." + date.milliseconds + ']';
        msg += " [" + level + "] ";

        // add category and params
        msg += "[" + category + "] " + applyParams(message, params);

        // trace the message
        trace(msg);

        if (!flash.external.ExternalInterface.available) {
            return;
        }

        ExternalInterface.call("console.log", msg);
    }
    private function leadingZeros(x:Number):String
    {
        if (x < 10) {
            return "00" + x.toString();
        }

        if (x < 100) {
            return "0" + x.toString();
        }

        return x.toString();
    }
    private function applyParams(message:String, params:Array):String
    {
        var result:String = message;

        var numParams:int = params.length;

        for (var i:int = 0; i < numParams; i++) {
            result = result.replace(new RegExp("\\{" + i + "\\}", "g"), params[i]);
        }
        return result;
    }

    private static const LEVEL_DEBUG:String = "DEBUG";
    private static const LEVEL_WARN:String = "WARN";
    private static const LEVEL_INFO:String = "INFO";
    private static const LEVEL_ERROR:String = "ERROR";
    private static const LEVEL_FATAL:String = "FATAL";
}

import flash.utils.ByteArray;

class SrsTsHanlder implements ISrsTsHandler
{
    private var avc:SrsRawH264Stream;
    private var h264_sps:ByteArray;
    private var h264_pps:ByteArray;
    private var h264_sps_changed:Boolean;
    private var h264_pps_changed:Boolean;
    
    private var aac:SrsRawAacStream;
    private var aac_specific_config:ByteArray;
    private var width:int;
    private var height:int;
    
    private var video_sh_tag:ByteArray;
    private var audio_sh_tag:ByteArray;
    
    private var queue:Array;
    
    // hls data.
    private var _hls:HlsCodec;
    private var _body:ByteArray;
    private var _on_size_changed:Function;
    private var _on_sequence_changed:Function;
	
    private var _log:ILogger = new TraceLogger("HLS");
    
    public function SrsTsHanlder(
        pavc:SrsRawH264Stream, paac:SrsRawAacStream, 
        ph264_sps:ByteArray, ph264_pps:ByteArray, 
        paac_specific_config:ByteArray,
        pvideo_sh_tag:ByteArray, paudio_sh_tag:ByteArray, 
        hls:HlsCodec, body:ByteArray, oszc:Function, oshc:Function)
    {
        _hls = hls;
        _body = body;
        _on_size_changed = oszc;
        _on_sequence_changed = oshc;
        
        avc = pavc;
        h264_sps = ph264_sps;
        h264_pps = ph264_pps;
        
        aac = paac;
        aac_specific_config = paac_specific_config;
        
        video_sh_tag = pvideo_sh_tag;
        audio_sh_tag = paudio_sh_tag;
        
        queue = new Array();
        width = 0;
        height = 0;
        h264_sps_changed = false;
        h264_pps_changed = false;
    }
    
    public function on_ts_message(msg:SrsTsMessage):void
    {
        do_on_ts_message(msg, _body);
    }
    
    private function do_on_ts_message(msg:SrsTsMessage, body:ByteArray):void
    {
        // @see SrsMpegtsOverUdp::on_ts_message
        if (false) {
            _log.info("got ts {4} message, dts={0}, pts={1}, size={2}/{3}", 
                msg.dts, msg.pts, msg.PES_packet_length, msg.payload.length,
                (msg.channel.apply.equals(SrsTsPidApply.Video)? "Video":"Audio"));
        } else {
            _log.debug("got ts {4} message, dts={0}, pts={1}, size={2}/{3}", 
                msg.dts, msg.pts, msg.PES_packet_length, msg.payload.length,
                (msg.channel.apply.equals(SrsTsPidApply.Video)? "Video":"Audio"));
        }
        
        // when not audio/video, or not adts/annexb format, donot support.
        if (msg.stream_number() != 0) {
            throw new Error("mpegts: unsupported stream format, sid=" + msg.stream_number());
        }
        
        // check supported codec
        if (msg.channel.stream != SrsTsStream.VideoH264 && msg.channel.stream != SrsTsStream.AudioAAC) {
            throw new Error("mpegts: unsupported stream codec=" + msg.channel.stream.toString());
        }
        
        // we must use queue to cache the msg, then parse it if possible.
        queue.push(msg);
        parse_message_queue(body);
    }
    
    private function parse_message_queue(body:ByteArray):void
    {
        if (queue.length == 0) {
            return;
        }
        
        var first_ts_msg:SrsTsMessage = queue[0] as SrsTsMessage;
        var context:SrsTsContext = first_ts_msg.channel.context;
        var cpa:Boolean = context.is_pure_audio();
        
        var nb_videos:uint = 0;
        if (!cpa) {
            for (var i:int = 0; i < queue.length; i++) {
                var msg:SrsTsMessage = queue[i] as SrsTsMessage;
                
                // publish audio or video.
                if (msg.channel.stream == SrsTsStream.VideoH264) {
                    nb_videos++;
                }
            }
            
            // always wait 2+ videos, to left one video in the queue.
            // TODO: FIXME: support pure audio hls.
            if (nb_videos <= 1) {
                return;
            }
        }
        
        // we must sort the adio and videos, for they maybe not monotonically increase.
        queue.sort(function(a:SrsTsMessage, b:SrsTsMessage):int{
            return a.dts - b.dts;
        });
        
        // parse messages util the last video.
        while ((cpa && queue.length > 1) || nb_videos > 1) {
            if (queue.length == 0) {
                throw new Error("assert queue not empty.");
            }
            
            msg = queue[0] as SrsTsMessage;
            if (msg.channel.stream == SrsTsStream.VideoH264) {
                nb_videos--;
            }
            queue.splice(0, 1);
            
            // publish audio or video.
            if (msg.channel.stream == SrsTsStream.VideoH264) {
                on_ts_video(msg, body);
            }
            if (msg.channel.stream == SrsTsStream.AudioAAC) {
                on_ts_audio(msg, body);
            }
        }
    }
    
    public function flush_message_queue(body:ByteArray):void
    {
        for (var i:int = 0; i < queue.length; i++) {
            var msg:SrsTsMessage = queue[i] as SrsTsMessage;
            
            // publish audio or video.
            if (msg.channel.stream == SrsTsStream.VideoH264) {
                on_ts_video(msg, body);
            }
            if (msg.channel.stream == SrsTsStream.AudioAAC) {
                on_ts_audio(msg, body);
            }
        }
        
        // clear queue.
        queue = new Array();
    }
    
    private function on_ts_video(msg:SrsTsMessage, body:ByteArray):void
    {
        // ts tbn to flv tbn.
        var dts:uint = (uint)(msg.dts / 90); 
        var pts:uint = (uint)(msg.pts / 90);
        
        var ibps:ByteArray = new ByteArray();
        var frame_type:uint = SrsConsts.SrsCodecVideoAVCFrameInterFrame;
        
        // each frame must prefixed by annexb format.
        // first check the msg.payload outside the while cycle, to avoid throw error inside.
        // if msg.payload not startwith annexb, just return.
        var annexb:Object = SrsUtils.srs_avc_startswith_annexb(msg.payload);
        if (!annexb.ok) {
            _log.warn("msg.payload not startwith annexb, drop size={0}B, dts={1}", msg.payload.length, dts);
            return;
        }
        
        // group each NALU frame to a RTMP/flv/ts message
        while (msg.payload.bytesAvailable) {
            var frame:ByteArray = avc.annexb_demux(msg.payload);
            
            // 5bits, 7.3.1 NAL unit syntax, 
            // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
            //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
            var nal_unit_type:uint = (uint)(frame[0] & 0x1f);
            
            // for IDR frame, the frame is keyframe.
            if (nal_unit_type == SrsConsts.SrsAvcNaluTypeIDR) {
                frame_type = SrsConsts.SrsCodecVideoAVCFrameKeyFrame;
            }
            
            // ignore the nalu type aud(9)
            if (nal_unit_type == SrsConsts.SrsAvcNaluTypeAccessUnitDelimiter) {
                var aud_nalu:String = "";
                for (var i:int = 0; i < frame.length; i++) {
                    aud_nalu += " 0x" + int(frame[i]).toString(16);
                }
                _log.debug("hls, aud nalu: {0}", aud_nalu);
                continue;
            }
            
            // for sps
            if (avc.is_sps(frame)) {
                var sps:ByteArray = avc.sps_demux(frame);
                
                if (SrsUtils.array_equals(h264_sps, sps)) {
                    continue;
                }
                h264_sps = sps;
                h264_sps_changed = true;
                
                // demux the sps, get the width x height.
                avc_demux_sps(sps);
                
                if (false) {
                    _log.info("hls: got sps, size={0}B", sps.length);
                } else {
                    _log.debug("hls: got sps, size={0}B", sps.length);
                }
                continue;
            }
            
            // for pps
            if (avc.is_pps(frame)) {
                var pps:ByteArray = avc.pps_demux(frame);
                
                if (SrsUtils.array_equals(h264_pps, pps)) {
                    continue;
                }
                h264_pps = pps;
                h264_pps_changed = true;
                
                if (false) {
                    _log.info("hls: got pps, size={0}B", pps.length);
                } else {
                    _log.debug("hls: got pps, size={0}B", pps.length);
                }
                continue;
            }
            
            // ibp frame.
            //info("mpegts: demux avc ibp frame size=%d, dts=%d", ibpframe_size, dts);
            var ibp:ByteArray = avc.mux_ipb_frame(frame);
            ibps.writeBytes(ibp);
        }
        
        write_h264_sps_pps(msg.channel.context, dts, pts);
        write_h264_ipb_frame(ibps, frame_type, dts, pts, body);
    }
    
    private function on_ts_audio(msg:SrsTsMessage, piece:ByteArray):void
    {
        // ts tbn to flv tbn.
        var dts:uint = msg.dts / 90;
        
        // got the next message to calc the delta duration for each audio.
        var duration:uint = 0;
        if (queue.length > 0) {
            var nm:SrsTsMessage = queue[0] as SrsTsMessage;
            duration = (uint)(Math.max(0, nm.dts - msg.dts) / 90);
        }
        var min_dts:uint = dts;
        var max_dts:uint = min_dts + duration;
        
        // send each frame.
        while (msg.payload.bytesAvailable) {
            var ret:Object = aac.adts_demux(msg.payload);
            var frame:ByteArray = ret.frame;
            var codec:SrsRawAacStreamCodec = ret.codec;
            
            // ignore invalid frame,
            //  * atleast 1bytes for aac to decode the data.
            if (!frame.bytesAvailable) {
                continue;
            }
            //info("mpegts: demux aac frame size=%d, dts=%d", frame_size, dts);
            
            // generate sh.
            if (!aac_specific_config.length) {
                aac_specific_config = aac.mux_sequence_header(codec);
                codec.aac_packet_type = 0;
                if (false) {
                    _log.info("hls: got audio specific config, size={0}B", aac_specific_config.length);
                } else {
                    _log.debug("hls: got audio specific config, size={0}B", aac_specific_config.length);
                }
                
                var tag_body:ByteArray = aac.mux_aac2flv(aac_specific_config, codec, dts);
                audio_sh_tag = mux_flv_packet(SrsConsts.SrsCodecFlvTagAudio, dts, tag_body);
                on_sequence_header(msg.channel.context);
            }
            
            // audio raw data.
            codec.aac_packet_type = 1;
            write_audio_raw_frame(frame, codec, dts, piece);
            
            // calc the delta of dts, when previous frame output.
            var delta:uint = duration / (msg.payload.length / frame.length);
            dts = (uint)(Math.min(max_dts, dts + delta));
            
            if (msg.payload.bytesAvailable) {
                _log.debug("Audio [{0}, {1}], the A2+ is {2}", min_dts, max_dts, dts);
            }
        }
    }
    
    private function write_audio_raw_frame(frame:ByteArray, codec:SrsRawAacStreamCodec, dts:uint, piece:ByteArray):void
    {
        var tag_body:ByteArray = aac.mux_aac2flv(frame, codec, dts);
        var tag:ByteArray = mux_flv_packet(SrsConsts.SrsCodecFlvTagAudio, dts, tag_body);
        
        // append flv packet to piece.
        piece.writeBytes(tag);
    }
    
    private function avc_demux_sps(sps:ByteArray):void
    {
        sps.position = 0;
        
        if (false) {
            var str:String = "";
            for (var i:int = 0; i < sps.length; i++) {
                str += " 0x" + int(sps[i]).toString(16);
            }
        }
        
        var nalu_type:uint = sps.readUnsignedByte();
        if ((nalu_type & 0x1f) != SrsConsts.SrsAvcNaluTypeSPS) {
            _log.warn("avc: sps nalu type invalid.");
            return;
        }
        
        var rbsp:ByteArray = new ByteArray();
        while (sps.bytesAvailable) {
            rbsp.writeByte(sps.readByte());
            
            // XX 00 00 03 XX, the 03 byte should be drop.
            var nb_rbsp:uint = rbsp.length;
            if (nb_rbsp > 2) {
                var p2:uint = rbsp[nb_rbsp - 3];
                var p1:uint = rbsp[nb_rbsp - 2];
                var p0:uint = rbsp[nb_rbsp - 1];
                if (p2 == 0 && p1 == 0 && p0 == 3) {
                    rbsp.position = rbsp.length - 1;
                    
                    // read 1 byte more.
                    if (!sps.bytesAvailable) {
                        break;
                    }
                    rbsp.writeByte(sps.readByte());
                }
            }
        }
        
        try {
            avc_demux_sps_rbsp(rbsp);
        } catch (e:Error) {
            _log.warn(e.message);
        }
    }
    private function avc_demux_sps_rbsp(rbsp:ByteArray):void
    {
        rbsp.position = 0;
        
        // for SPS, 7.3.2.1.1 Sequence parameter set data syntax
        // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62.
        if (rbsp.bytesAvailable < 3) {
            _log.warn("avc: sps shall atleast 3bytes");
            return;
        }
        
        var profile_idc:uint = rbsp.readUnsignedByte();
        var flags:uint = rbsp.readUnsignedByte();
        var level_idc:uint = rbsp.readUnsignedByte();
        
        var bs:SrsBitStream = new SrsBitStream(rbsp);
        var seq_parameter_set_id:uint = SrsUtils.srs_avc_nalu_read_uev(bs);
        _log.debug("sps parse profile={0}, level={1}, sps_id={2}", profile_idc, level_idc, seq_parameter_set_id);
        
        if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244
            || profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118
            || profile_idc == 128
        ) {
            var chroma_format_idc:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            if (chroma_format_idc == 3) {
                var separate_colour_plane_flag:int = SrsUtils.srs_avc_nalu_read_bit(bs);
            }
            var bit_depth_luma_minus8:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            var bit_depth_chroma_minus8:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            var qpprime_y_zero_transform_bypass_flag:int = SrsUtils.srs_avc_nalu_read_bit(bs);
            var seq_scaling_matrix_present_flag:int = SrsUtils.srs_avc_nalu_read_bit(bs);
            if (seq_scaling_matrix_present_flag) {
                throw new Error("sps seq_scaling_matrix_present_flag not zero.");
            }
            _log.debug("sps cfi={0}, bdlm={1}, bdcm={2}, qyztb={3}, ssmpf={4}", 
                chroma_format_idc, bit_depth_luma_minus8, bit_depth_chroma_minus8, 
                qpprime_y_zero_transform_bypass_flag, seq_scaling_matrix_present_flag);
        }
        
        var log2_max_frame_num_minus4:int = SrsUtils.srs_avc_nalu_read_uev(bs);
        var pic_order_cnt_type:int = SrsUtils.srs_avc_nalu_read_uev(bs);
        if (pic_order_cnt_type == 0) {
            var log2_max_pic_order_cnt_lsb_minus4:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            _log.debug("sps lmpoclm={0}", log2_max_pic_order_cnt_lsb_minus4);
        } else if (pic_order_cnt_type == 1) {
            var delta_pic_order_always_zero_flag:int = SrsUtils.srs_avc_nalu_read_bit(bs);
            var offset_for_non_ref_pic:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            var offset_for_top_to_bottom_field:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            var num_ref_frames_in_pic_order_cnt_cycle:int = SrsUtils.srs_avc_nalu_read_uev(bs);
            _log.debug("sps dpoazf={0}, ofnrp={1}, ofttbf={2}, nrfipocc={3}",
                delta_pic_order_always_zero_flag, offset_for_non_ref_pic, offset_for_top_to_bottom_field,
                num_ref_frames_in_pic_order_cnt_cycle);
        }
        var max_num_ref_frames:int = SrsUtils.srs_avc_nalu_read_uev(bs);
        var gaps_in_frame_num_value_allowed_flag:int = SrsUtils.srs_avc_nalu_read_bit(bs);
        var pic_width_in_mbs_minus1:int = SrsUtils.srs_avc_nalu_read_uev(bs);
        var pic_height_in_map_units_minus1:int = SrsUtils.srs_avc_nalu_read_uev(bs);
        
        width = (int)(pic_width_in_mbs_minus1 + 1) * 16;
        height = (int)(pic_height_in_map_units_minus1 + 1) * 16;
        _log.info("sps parse profile={0}, level={1}, size={2}x{3}", profile_idc, level_idc, width, height);
        
        _on_size_changed(width, height);
    }
    
    private function write_h264_sps_pps(context:SrsTsContext, dts:uint, pts:uint):void
    {
        if (!h264_sps_changed && !h264_pps_changed) {
            return;
        }
        
        // when not got sps/pps, wait.
        if (h264_pps.length == 0 || h264_sps.length == 0) {
            return;
        }
        
        // h264 raw to h264 packet.
        var sh:ByteArray = avc.mux_sequence_header(h264_sps, h264_pps, dts, pts);
        
        // h264 packet to flv packet.
        var frame_type:uint = SrsConsts.SrsCodecVideoAVCFrameKeyFrame;
        var avc_packet_type:uint = SrsConsts.SrsCodecVideoAVCTypeSequenceHeader;
        var tag_body:ByteArray = avc.mux_avc2flv(sh, frame_type, avc_packet_type, dts, pts);
        
        // the timestamp in rtmp message header is dts.
        video_sh_tag = mux_flv_packet(SrsConsts.SrsCodecFlvTagVideo, dts, tag_body);
        on_sequence_header(context);
    }
    
    private function write_h264_ipb_frame(ibps:ByteArray, frame_type:uint, dts:uint, pts:uint, piece:ByteArray):void
    {
        // when sps or pps not sent, ignore the packet.
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/203
        if (video_sh_tag.length == 0) {
            return;
        }
        
        var avc_packet_type:uint = SrsConsts.SrsCodecVideoAVCTypeNALU;
        var tag_body:ByteArray = avc.mux_avc2flv(ibps, frame_type, avc_packet_type, dts, pts);
        
        // the timestamp in rtmp message header is dts.
        var timestamp:uint = dts;
        var tag:ByteArray = mux_flv_packet(SrsConsts.SrsCodecFlvTagVideo, timestamp, tag_body);
        
        // append flv packet to piece.
        piece.writeBytes(tag);
    }
    
    private function mux_flv_packet(type:uint, timestamp:uint, flv:ByteArray):ByteArray
    {   
        // E.4.1 FLV Tag
        var packet:ByteArray = new ByteArray();
        
        // Reserved UB [2]
        // Filter UB [1]
        // TagType UB [5]
        packet.writeByte(type & 0x1f);
        
        // DataSize UI24
        var size:uint = flv.length;
        packet.writeByte(size >> 16);
        packet.writeByte(size >> 8);
        packet.writeByte(size);
        
        // Timestamp UI24
        var dts:uint = timestamp;
        packet.writeByte(dts >> 16);
        packet.writeByte(dts >> 8);
        packet.writeByte(dts);
        
        // TimestampExtended UI8
        packet.writeByte(dts >> 24);
        
        // StreamID, UI24, Always 0.
        packet.writeByte(0x00);
        packet.writeByte(0x00);
        packet.writeByte(0x00);
        
        // tag body.
        packet.writeBytes(flv);
        
        // PreviousTagSizeN, UI32, Size of last tag, including its header, in bytes.
        size = packet.length;
        packet.writeUnsignedInt(size);
        
        if (false) {
            _log.info("FLV: mux flv type={0}, time={1}, size={3}", type, timestamp, dts, packet.length);
        } else {
            _log.debug("mux flv type={0}, time={1}, size={3}", type, timestamp, dts, packet.length);
        }
        
        return packet;
    }
    
    private function on_sequence_header(context:SrsTsContext):void
    {
        if (!audio_sh_tag.length) {
            return;
        }
        if (!context.is_pure_audio() && !video_sh_tag.length) {
            return;
        }
        
        var sh:ByteArray = new ByteArray();
        
        // @remark HSS without flv header.
        // 9bytes header and 4bytes first previous-tag-size
        // Signatures "FLV"
        sh.writeByte(0x46); // 'F'
        sh.writeByte(0x4c); // 'L'
        sh.writeByte(0x56); // 'V'
        // File version (for example, 0x01 for FLV version 1)
        sh.writeByte(0x01);
        // 4, audio; 1, video; 5 audio+video.
        if (context.is_pure_audio()) {
            sh.writeByte(0x04);
        } else {
            sh.writeByte(0x05);
        }
        // DataOffset UI32 The length of this header in bytes
        sh.writeUnsignedInt(0x00000009);
        // previous tag size.
        sh.writeUnsignedInt(0x00000000);
        
        // append video and audio sequence header.
        if (!context.is_pure_audio()) {
            sh.writeBytes(video_sh_tag);
        }
        sh.writeBytes(audio_sh_tag);
        
        // reset the positions.
        h264_sps.position = 0;
        h264_pps.position = 0;
        aac_specific_config.position = 0;
        video_sh_tag.position = 0;
        audio_sh_tag.position = 0;
        
        // notice the HLS to change sh if should to.
        _on_sequence_changed(
            avc, aac,
            h264_sps, h264_pps,
            aac_specific_config,
            video_sh_tag, audio_sh_tag,
            sh
        );
    }
}

class SrsConsts
{
    // E.4.3.1 VIDEODATA
    // Frame Type UB [4]
    // Type of video frame. The following values are defined:
    //     1 = key frame (for AVC, a seekable frame)
    //     2 = inter frame (for AVC, a non-seekable frame)
    //     3 = disposable inter frame (H.263 only)
    //     4 = generated key frame (reserved for server use only)
    //     5 = video info/command frame
    public static const SrsCodecVideoAVCFrameReserved:uint = 0;
    public static const SrsCodecVideoAVCFrameReserved1:uint = 6;
        
    public static const SrsCodecVideoAVCFrameKeyFrame:uint = 1;
    public static const SrsCodecVideoAVCFrameInterFrame:uint = 2;
    public static const SrsCodecVideoAVCFrameDisposableInterFrame:uint = 3;
    public static const SrsCodecVideoAVCFrameGeneratedKeyFrame:uint = 4;
    public static const SrsCodecVideoAVCFrameVideoInfoFrame:uint = 5;
    
    // AACPacketType IF SoundFormat == 10 UI8
    // The following values are defined:
    //     0 = AAC sequence header
    //     1 = AAC raw
    public static const SrsCodecAudioTypeReserved:uint = 2;
            
    public static const SrsCodecAudioTypeSequenceHeader:uint = 0;
    public static const SrsCodecAudioTypeRawData:uint = 1;
    
    // AVCPacketType IF CodecID == 7 UI8
    // The following values are defined:
    //     0 = AVC sequence header
    //     1 = AVC NALU
    //     2 = AVC end of sequence (lower level NALU sequence ender is
    //         not required or supported)
    public static const SrsCodecVideoAVCTypeReserved:uint = 3;
            
    public static const SrsCodecVideoAVCTypeSequenceHeader:uint = 0;
    public static const SrsCodecVideoAVCTypeNALU:uint = 1;
    public static const SrsCodecVideoAVCTypeSequenceHeaderEOF:uint = 2;
    
    // E.4.3.1 VIDEODATA
    // CodecID UB [4]
    // Codec Identifier. The following values are defined:
    //     2 = Sorenson H.263
    //     3 = Screen video
    //     4 = On2 VP6
    //     5 = On2 VP6 with alpha channel
    //     6 = Screen video version 2
    //     7 = AVC
    public static const SrsCodecVideoReserved:uint = 0;
    public static const SrsCodecVideoReserved1:uint = 1;
    public static const SrsCodecVideoReserved2:uint = 9;
    
    // for user to disable video, for example, use pure audio hls.
    public static const SrsCodecVideoDisabled:uint = 8;
    
    public static const SrsCodecVideoSorensonH263:uint = 2;
    public static const SrsCodecVideoScreenVideo:uint = 3;
    public static const SrsCodecVideoOn2VP6:uint = 4;
    public static const SrsCodecVideoOn2VP6WithAlphaChannel:uint = 5;
    public static const SrsCodecVideoScreenVideoVersion2:uint = 6;
    public static const SrsCodecVideoAVC:uint = 7;
    
    // SoundFormat UB [4] 
    // Format of SoundData. The following values are defined:
    //     0 = Linear PCM, platform endian
    //     1 = ADPCM
    //     2 = MP3
    //     3 = Linear PCM, little endian
    //     4 = Nellymoser 16 kHz mono
    //     5 = Nellymoser 8 kHz mono
    //     6 = Nellymoser
    //     7 = G.711 A-law logarithmic PCM
    //     8 = G.711 mu-law logarithmic PCM
    //     9 = reserved
    //     10 = AAC
    //     11 = Speex
    //     14 = MP3 8 kHz
    //     15 = Device-specific sound
    // Formats 7, 8, 14, and 15 are reserved.
    // AAC is supported in Flash Player 9,0,115,0 and higher.
    // Speex is supported in Flash Player 10 and higher.
    public static const SrsCodecAudioReserved1:uint = 16;
        
    public static const SrsCodecAudioLinearPCMPlatformEndian:uint = 0;
    public static const SrsCodecAudioADPCM:uint = 1;
    public static const SrsCodecAudioMP3:uint = 2;
    public static const SrsCodecAudioLinearPCMLittleEndian:uint = 3;
    public static const SrsCodecAudioNellymoser16kHzMono:uint = 4;
    public static const SrsCodecAudioNellymoser8kHzMono:uint = 5;
    public static const SrsCodecAudioNellymoser:uint = 6;
    public static const SrsCodecAudioReservedG711AlawLogarithmicPCM:uint = 7;
    public static const SrsCodecAudioReservedG711MuLawLogarithmicPCM:uint = 8;
    public static const SrsCodecAudioReserved:uint = 9;
    public static const SrsCodecAudioAAC:uint = 10;
    public static const SrsCodecAudioSpeex:uint = 11;
    public static const SrsCodecAudioReservedMP3_8kHz:uint = 14;
    public static const SrsCodecAudioReservedDeviceSpecificSound:uint = 15;
    
    /**
     * the FLV/RTMP supported audio sample rate.
     * Sampling rate. The following values are defined:
     * 0 = 5.5 kHz = 5512 Hz
     * 1 = 11 kHz = 11025 Hz
     * 2 = 22 kHz = 22050 Hz
     * 3 = 44 kHz = 44100 Hz
     */
    public static const SrsCodecAudioSampleRateReserved:uint = 4;
    
    public static const SrsCodecAudioSampleRate5512:uint = 0;
    public static const SrsCodecAudioSampleRate11025:uint = 1;
    public static const SrsCodecAudioSampleRate22050:uint = 2;
    public static const SrsCodecAudioSampleRate44100:uint = 3;
    
    /**
     * E.4.1 FLV Tag, page 75
     */
    public static const SrsCodecFlvTagReserved:uint = 0;
    
    // 8 = audio
    public static const SrsCodecFlvTagAudio:uint = 8;
    // 9 = video
    public static const SrsCodecFlvTagVideo:uint = 9;
    // 18 = script data
    public static const SrsCodecFlvTagScript:uint = 18;
    
    /**
     * Table 7-1 – NAL unit type codes, syntax element categories, and NAL unit type classes
     * H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 83.
     */
    // Coded slice of a non-IDR picture slice_layer_without_partitioning_rbsp( )
    public static const SrsAvcNaluTypeNonIDR:uint = 1;
    // Coded slice data partition A slice_data_partition_a_layer_rbsp( )
    public static const SrsAvcNaluTypeDataPartitionA:uint = 2;
    // Coded slice data partition B slice_data_partition_b_layer_rbsp( )
    public static const SrsAvcNaluTypeDataPartitionB:uint = 3;
    // Coded slice data partition C slice_data_partition_c_layer_rbsp( )
    public static const SrsAvcNaluTypeDataPartitionC:uint = 4;
    // Coded slice of an IDR picture slice_layer_without_partitioning_rbsp( )
    public static const SrsAvcNaluTypeIDR:uint = 5;
    // Supplemental enhancement information (SEI) sei_rbsp( )
    public static const SrsAvcNaluTypeSEI:uint = 6;
    // Sequence parameter set seq_parameter_set_rbsp( )
    public static const SrsAvcNaluTypeSPS:uint = 7;
    // Picture parameter set pic_parameter_set_rbsp( )
    public static const SrsAvcNaluTypePPS:uint = 8;
    // Access unit delimiter access_unit_delimiter_rbsp( )
    public static const SrsAvcNaluTypeAccessUnitDelimiter:uint = 9;
    // End of sequence end_of_seq_rbsp( )
    public static const SrsAvcNaluTypeEOSequence:uint = 10;
    // End of stream end_of_stream_rbsp( )
    public static const SrsAvcNaluTypeEOStream:uint = 11;
    // Filler data filler_data_rbsp( )
    public static const SrsAvcNaluTypeFilterData:uint = 12;
    // Sequence parameter set extension seq_parameter_set_extension_rbsp( )
    public static const SrsAvcNaluTypeSPSExt:uint = 13;
    // Prefix NAL unit prefix_nal_unit_rbsp( )
    public static const SrsAvcNaluTypePrefixNALU:uint = 14;
    // Subset sequence parameter set subset_seq_parameter_set_rbsp( )
    public static const SrsAvcNaluTypeSubsetSPS:uint = 15;
    // Coded slice of an auxiliary coded picture without partitioning slice_layer_without_partitioning_rbsp( )
    public static const SrsAvcNaluTypeLayerWithoutPartition:uint = 19;
    // Coded slice extension slice_layer_extension_rbsp( )
    public static const SrsAvcNaluTypeCodedSliceExt:uint = 20;
}

class SrsUtils
{
    /*
    * MPEG2 transport stream (aka DVB) mux
    * Copyright (c) 2003 Fabrice Bellard.
    *
    * This library is free software; you can redistribute it and/or
    * modify it under the terms of the GNU Lesser General Public
    * License as published by the Free Software Foundation; either
    * version 2 of the License, or (at your option) any later version.
    *
    * This library is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    * Lesser General Public License for more details.
    *
    * You should have received a copy of the GNU Lesser General Public
    * License along with this library; if not, write to the Free Software
    * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
    */
    private static const crc_table:Array = [
        0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
        0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
        0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 0x4c11db70, 0x48d0c6c7,
        0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
        0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3,
        0x709f7b7a, 0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
        0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58, 0xbaea46ef,
        0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
        0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb,
        0xceb42022, 0xca753d95, 0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
        0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
        0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
        0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4,
        0x0808d07d, 0x0cc9cdca, 0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
        0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08,
        0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
        0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc,
        0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
        0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a, 0xe0b41de7, 0xe4750050,
        0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
        0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
        0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
        0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb, 0x4f040d56, 0x4bc510e1,
        0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
        0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5,
        0x3f9b762c, 0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
        0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e, 0xf5ee4bb9,
        0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
        0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd,
        0xcda1f604, 0xc960ebb3, 0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
        0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
        0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
        0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2,
        0x470cdd2b, 0x43cdc09c, 0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
        0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e,
        0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
        0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a,
        0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
        0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c, 0xe3a1cbc1, 0xe760d676,
        0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
        0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
        0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
        0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
    ];
    
    // @see http://www.stmc.edu.hk/~vincent/ffmpeg_0.4.9-pre1/libavformat/mpegtsenc.c
    private static function mpegts_crc32(bytes:ByteArray):uint
    {
        var crc:uint = 0xffffffff;
        
        for (var i:int = 0; i < bytes.length; i++) {
            crc = (crc << 8) ^ crc_table[((crc >> 24) ^ bytes[i]) & 0xff];
        }
        
        return crc;
    }
    
    public static function srs_crc32(bytes:ByteArray):uint
    {
        return mpegts_crc32(bytes);
    }
    
    /**
    * parse the annexb header.
    * @return an object which is:
    *   nb_header, an int start code, the header size.
    *   ok, a bool indicates whether the stream is annexb.
    */
    public static function srs_avc_startswith_annexb(stream:ByteArray):Object
    {
        var nb_start_code:int = 0;
        var is_annexb:Boolean = false;
        
        var bytes:uint = stream.position;
        var p:uint = bytes;
        for (;;) {
            if (stream.bytesAvailable < p - bytes + 3) {
                break;
            }

            // not match
            if (stream[p] != 0x00 || stream[p + 1] != 0x00) {
                break;
            }
            
            // match N[00] 00 00 01, where N>=0
            if (stream[p + 2] == 0x01) {
                nb_start_code = p - bytes + 3;
                is_annexb = true;
                break;
            }
            
            p++;
        }
        
        return {
            nb_header: nb_start_code,
            ok: is_annexb
        };
    }
    
    /**
     * whether stream starts with the aac ADTS 
     * from aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 75, 1.A.2.2 ADTS.
     * start code must be '1111 1111 1111'B, that is 0xFFF
     */
    public static function srs_aac_startswith_adts(stream:ByteArray):Boolean
    {
        if (stream.bytesAvailable < 2) {
            return false;
        }
        
        // matched 12bits 0xFFF,
        // @remark, we must cast the 0xff to char to compare.
        var p:uint = stream.position;
        if (stream[p] != 0xff || (stream[p + 1] & 0xf0) != 0xf0) {
            return false;
        }
        
        return true;
    }
    
    public static function array_equals(a:ByteArray, b:ByteArray):Boolean
    {
        if ((a && !b) || (!b && a) || a.length != b.length) {
            return false;
        }
        
        for (var i:int = 0; i < a.length; i++) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        
        return true;
    }
    
    /**
    * read the ue(v) of h.264 bit stream.
    */
    public static function srs_avc_nalu_read_uev(stream:SrsBitStream):int
    {
        if (stream.empty()) {
            throw new Error("avc: h.264 bit stream empty.");
        }
        
        // ue(v) in 9.1 Parsing process for Exp-Golomb codes
        // H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 227.
        // Syntax elements coded as ue(v), me(v), or se(v) are Exp-Golomb-coded.
        //      leadingZeroBits = -1;
        //      for( b = 0; !b; leadingZeroBits++ )
        //          b = read_bits( 1 )
        // The variable codeNum is then assigned as follows:
        //      codeNum = (2<<leadingZeroBits) – 1 + read_bits( leadingZeroBits )
        var leadingZeroBits:int = -1;
        for (var b:uint = 0; !b && !stream.empty(); leadingZeroBits++) {
            b = stream.read_bit();
        }
        
        if (leadingZeroBits >= 31) {
            throw new Error("avc: h.264 ue(v) overflow.");
        }
        
        var v:int = (1 << leadingZeroBits) - 1;
        for (var i:int = 0; i < leadingZeroBits; i++) {
           b = stream.read_bit();
            v += b << (leadingZeroBits - 1);
        }
        
        return v;
    }
    
    /**
    * read a bit from the h.264 avc bit stream.
    */
    public static function srs_avc_nalu_read_bit(stream:SrsBitStream):int
    {
        if (stream.empty()) {
            throw new Error("avc: h.264 bit stream empty.");
        }
        
        var v:int = stream.read_bit();
        
        return v;
    }
    
    public static function srs_print_bytes(bytes:ByteArray, len:int):void
    {
        var prt_len:int;
        var print:String = "";
        
        prt_len = len == -1? (bytes.length - bytes.position): len;
        prt_len = Math.min(prt_len, Math.min((bytes.length - bytes.position), 2048));
        
        for (var i:int = 0; i < prt_len; i++)
        {
            print += bytes[i + bytes.position].toString(16).toUpperCase() + " ";
        }
        trace(print);
    }
}

class SrsBitStream
{
    private var stream:ByteArray;
    private var cb:uint;
    private var cb_left:uint;
    
    public function SrsBitStream(s:ByteArray)
    {
        cb = 0;
        cb_left = 0;
        stream = s;
    }
    
    public function empty():Boolean
    {
        if (cb_left) {
            return false;
        }
        return stream.bytesAvailable == 0;
    }
    
    public function read_bit():uint
    {
        if (!cb_left) {
            cb = stream.readUnsignedByte();
            cb_left = 8;
        }
        
        var v:uint = (cb >> (cb_left - 1)) & 0x01;
        cb_left--;
        return v;
    }
}

/**
 * the raw h.264 stream, in annexb.
 */
class SrsRawH264Stream
{
    private var _log:ILogger = new TraceLogger("HLS");
    
    public function SrsRawH264Stream()
    {
    }
    
    /**
     * demux the stream in annexb format.
     */
    public function annexb_demux(stream:ByteArray):ByteArray
    {
        var frame:ByteArray = new ByteArray();
        while (stream.bytesAvailable) {
            // each frame must prefixed by annexb format.
            // about annexb, @see H.264-AVC-ISO_IEC_14496-10.pdf, page 211.
            var annexb:Object = SrsUtils.srs_avc_startswith_annexb(stream);
            if (!annexb.ok) {
                throw new Error("avc: not annexb format.");
            }
            
            var nb_annexb_header:uint = annexb.nb_header;
            var start:uint = stream.position + annexb.nb_header;
            stream.position += annexb.nb_header;
            
            // find the last frame prefixed by annexb format.
            while (stream.bytesAvailable) {
                annexb = SrsUtils.srs_avc_startswith_annexb(stream);
                if (annexb.ok) {
                    break;
                }
                stream.position++;
            }
            
            // demux the frame.
            var pos:uint = stream.position;
            var nb_frame:int = pos - start;
            stream.position = start;
            stream.readBytes(frame, 0, nb_frame);
            stream.position = pos;
            _log.debug("avc: annexb {0}B header, {1}B frame", nb_annexb_header, nb_frame);
            break;
        }
        return frame;
    }
    /**
     * whether the frame is sps or pps.
     */
    public function is_sps(frame:ByteArray):Boolean
    {
        // 5bits, 7.3.1 NAL unit syntax, 
        // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        var nal_unit_type:uint = (frame[0] &  0x1f);
        
        return nal_unit_type == 7;
    }
    public function is_pps(frame:ByteArray):Boolean
    {
        // 5bits, 7.3.1 NAL unit syntax, 
        // H.264-AVC-ISO_IEC_14496-10.pdf, page 44.
        //  7: SPS, 8: PPS, 5: I Frame, 1: P Frame
        var nal_unit_type:uint = (frame[0] &  0x1f);
        
        return nal_unit_type == 8;
    }
    /**
     * demux the sps or pps to string.
     * @param sps/pps output the sps/pps.
     */
    public function sps_demux(frame:ByteArray):ByteArray
    {
        // atleast 1bytes for SPS to decode the type, profile, constrain and level.
        if (frame.bytesAvailable < 4) {
            return null;
        }
        
        return frame;
    }
    public function pps_demux(frame:ByteArray):ByteArray
    {
        if (!frame.bytesAvailable) {
            return null;
        }
        return frame;
    }
    
    /**
     * h264 raw data to h264 packet, without flv payload header.
     * mux the sps/pps to flv sequence header packet.
     * @param sh output the sequence header.
     */
    public function mux_sequence_header(sps:ByteArray, pps:ByteArray, dts:uint, pts:uint):ByteArray
    {
        var sh:ByteArray = new ByteArray();
        
        // 5bytes sps/pps header:
        //      configurationVersion, AVCProfileIndication, profile_compatibility,
        //      AVCLevelIndication, lengthSizeMinusOne
        // 3bytes size of sps:
        //      numOfSequenceParameterSets, sequenceParameterSetLength(2B)
        // Nbytes of sps.
        //      sequenceParameterSetNALUnit
        // 3bytes size of pps:
        //      numOfPictureParameterSets, pictureParameterSetLength
        // Nbytes of pps:
        //      pictureParameterSetNALUnit
        
        // decode the SPS: 
        // @see: 7.3.2.1.1, H.264-AVC-ISO_IEC_14496-10-2012.pdf, page 62
        if (true) {
            if (sps.length < 4) {
                throw new Error("sps atleast 4bytes.");
            }
            var frame:ByteArray = sps;
            
            // @see: Annex A Profiles and levels, H.264-AVC-ISO_IEC_14496-10.pdf, page 205
            //      Baseline profile profile_idc is 66(0x42).
            //      Main profile profile_idc is 77(0x4d).
            //      Extended profile profile_idc is 88(0x58).
            var profile_idc:int = (int)(frame[1]);
            //u_int8_t constraint_set = frame[2];
            var level_idc:int = (int)(frame[3]);
            
            // generate the sps/pps header
            // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
            // configurationVersion
            sh.writeByte(0x01);
            // AVCProfileIndication
            sh.writeByte(profile_idc);
            // profile_compatibility
            sh.writeByte(0x00);
            // AVCLevelIndication
            sh.writeByte(level_idc);
            // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size,
            // so we always set it to 0x03.
            sh.writeByte(0x03);
        }
        
        // sps
        if (true) {
            // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
            // numOfSequenceParameterSets, always 1
            sh.writeByte(0x01);
            // sequenceParameterSetLength
            sh.writeShort(sps.length);
            // sequenceParameterSetNALUnit
            sh.writeBytes(sps);
        }
        
        // pps
        if (true) {
            // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
            // numOfPictureParameterSets, always 1
            sh.writeByte(0x01);
            // pictureParameterSetLength
            sh.writeShort(pps.length);
            // pictureParameterSetNALUnit
            sh.writeBytes(pps);
        }
        
        // TODO: FIXME: for more profile.
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 144
        
        return sh;
    }
    /**
     * h264 raw data to h264 packet, without flv payload header.
     * mux the ibp to flv ibp packet.
     * @return ibp an ByteArray contains the bytes.
     */
    public function mux_ipb_frame(frame:ByteArray):ByteArray
    {
        var ibp:ByteArray = new ByteArray();
        
        // 4bytes size of nalu:
        //      NALUnitLength
        // Nbytes of nalu.
        //      NALUnit
        
        // 5.3.4.2.1 Syntax, H.264-AVC-ISO_IEC_14496-15.pdf, page 16
        // lengthSizeMinusOne, or NAL_unit_length, always use 4bytes size
        var NAL_unit_length:uint = frame.length;
        
        // mux the avc NALU in "ISO Base Media File Format" 
        // from H.264-AVC-ISO_IEC_14496-15.pdf, page 20
        // NALUnitLength
        ibp.writeUnsignedInt(NAL_unit_length);
        // NALUnit
        ibp.writeBytes(frame);
        
        return ibp;
    }
    /**
     * mux the avc video packet to flv video packet.
     * @param frame_type, SrsCodecVideoAVCFrameKeyFrame or SrsCodecVideoAVCFrameInterFrame.
     * @param avc_packet_type, SrsCodecVideoAVCTypeSequenceHeader or SrsCodecVideoAVCTypeNALU.
     * @param video the h.264 raw data.
     * @param flv output the muxed flv packet.
     * @param nb_flv output the muxed flv size.
     */
    public function mux_avc2flv(frame:ByteArray, frame_type:uint,  avc_packet_type:uint, dts:uint, pts:uint):ByteArray
    {
        var flv:ByteArray = new ByteArray();
        
        // for h264 in RTMP video payload, there is 5bytes header:
        //      1bytes, FrameType | CodecID
        //      1bytes, AVCPacketType
        //      3bytes, CompositionTime, the cts.
        // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
        
        // @see: E.4.3 Video Tags, video_file_format_spec_v10_1.pdf, page 78
        // Frame Type, Type of video frame.
        // CodecID, Codec Identifier.
        // set the rtmp header
        flv.writeByte((frame_type << 4) | SrsConsts.SrsCodecVideoAVC);
        
        // AVCPacketType
        flv.writeByte(avc_packet_type);
        
        // CompositionTime
        // pts = dts + cts, or 
        // cts = pts - dts.
        // where cts is the header in rtmp video packet payload header.
        var cts:uint = pts - dts;
        flv.writeByte(cts >> 16);
        flv.writeByte(cts >> 8);
        flv.writeByte(cts);
        
        // h.264 raw data.
        flv.writeBytes(frame);
        
        return flv;
    }
};

/**
 * the header of adts sample.
 */
class SrsRawAacStreamCodec
{
    public var protection_absent:uint;
    public var aac_object:SrsAacObjectType;
    public var sampling_frequency_index:uint;
    public var channel_configuration:uint;
    public var frame_length:uint;
    
    public var sound_format:uint;
    public var sound_rate:uint;
    public var sound_size:uint;
    public var sound_type:uint;
    // 0 for sh; 1 for raw data.
    public var aac_packet_type:uint;
};

/**
 * the raw aac stream, in adts.
 */
class SrsRawAacStream
{
	private var _log:ILogger = new TraceLogger("HLS");
	
    public function SrsRawAacStream()
    {
    }
    
    /**
     * demux the stream in adts format.
     * @param stream the input stream bytes.
     * @return an object which is:
     *      frame a byte array contains the demuxed aac frame.
     *      code a aac stream codec info.
     */
    public function adts_demux(stream:ByteArray):Object
    {
        var frame:ByteArray = new ByteArray();
        var codec:SrsRawAacStreamCodec = new SrsRawAacStreamCodec();
        
        while (stream.bytesAvailable) {
            var adts_header_start:uint = stream.position;
            
            // decode the ADTS.
            // @see aac-iso-13818-7.pdf, page 26
            //      6.2 Audio Data Transport Stream, ADTS
            // @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64145885
            // byte_alignment()
            
            // adts_fixed_header:
            //      12bits syncword,
            //      16bits left.
            // adts_variable_header:
            //      28bits
            //      12+16+28=56bits
            // adts_error_check:
            //      16bits if protection_absent
            //      56+16=72bits
            // if protection_absent:
            //      require(7bytes)=56bits
            // else
            //      require(9bytes)=72bits
            if (stream.bytesAvailable < 7) {
                throw new Error("aac: adts required atleast 7bytes.");
            }
            
            // for aac, the frame must be ADTS format.
            if (!SrsUtils.srs_aac_startswith_adts(stream)) {
                throw new Error("aac: adts schema invalid.");
            }
            
            // syncword 12 bslbf
            stream.readByte();
            // 4bits left.
            // adts_fixed_header(), 1.A.2.2.1 Fixed Header of ADTS
            // ID 1 bslbf
            // layer 2 uimsbf
            // protection_absent 1 bslbf
            var pav:uint = (stream.readUnsignedByte() & 0x0f);
            var id:uint = (uint)((pav >> 3) & 0x01);
            /*int8_t layer = (pav >> 1) & 0x03;*/
            var protection_absent:uint = pav & 0x01;
            
            /**
             * ID: MPEG identifier, set to ‘1’ if the audio data in the ADTS stream are MPEG-2 AAC (See ISO/IEC 13818-7)
             * and set to ‘0’ if the audio data are MPEG-4. See also ISO/IEC 11172-3, subclause 2.4.2.3.
             */
            if (id != 0x01) {
				//warn("adts: id must be 1(aac), actual 0(mp4a).");
				
				// well, some system always use 0, but actually is aac format.
				// for example, houjian vod ts always set the aac id to 0, actually 1.
				// we just ignore it, and alwyas use 1(aac) to demux.
				id = 0x01;
            }
            
            var sfiv:uint = stream.readUnsignedShort();
            // profile 2 uimsbf
            // sampling_frequency_index 4 uimsbf
            // private_bit 1 bslbf
            // channel_configuration 3 uimsbf
            // original/copy 1 bslbf
            // home 1 bslbf
            var profile:uint = (sfiv >> 14) & 0x03;
            var sampling_frequency_index:uint = (sfiv >> 10) & 0x0f;
            /*int8_t private_bit = (sfiv >> 9) & 0x01;*/
            var channel_configuration:uint = (sfiv >> 6) & 0x07;
            /*int8_t original = (sfiv >> 5) & 0x01;*/
            /*int8_t home = (sfiv >> 4) & 0x01;*/
            //int8_t Emphasis; @remark, Emphasis is removed, @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64154736
            // 4bits left.
            // adts_variable_header(), 1.A.2.2.2 Variable Header of ADTS
            // copyright_identification_bit 1 bslbf
            // copyright_identification_start 1 bslbf
            /*int8_t fh_copyright_identification_bit = (fh1 >> 3) & 0x01;*/
            /*int8_t fh_copyright_identification_start = (fh1 >> 2) & 0x01;*/
            // frame_length 13 bslbf: Length of the frame including headers and error_check in bytes.
            // use the left 2bits as the 13 and 12 bit,
            // the frame_length is 13bits, so we move 13-2=11.
            var frame_length:uint = (sfiv << 11) & 0x1800;
            
            // skip -1 to read 4B for actually read 3B
            stream.position -= 1;
            var abfv:uint = (stream.readUnsignedInt() & 0x00ffffff);
            // frame_length 13 bslbf: consume the first 13-2=11bits
            // the fh2 is 24bits, so we move right 24-11=13.
            frame_length |= (abfv >> 13) & 0x07ff;
            // adts_buffer_fullness 11 bslbf
            /*int16_t fh_adts_buffer_fullness = (abfv >> 2) & 0x7ff;*/
            // number_of_raw_data_blocks_in_frame 2 uimsbf
            /*int16_t number_of_raw_data_blocks_in_frame = abfv & 0x03;*/
            // adts_error_check(), 1.A.2.2.3 Error detection
            if (!protection_absent) {
                if (stream.bytesAvailable < 2) {
                    throw new Error("aac: adts header corrupt.");
                }
                // crc_check 16 Rpchof
                /*int16_t crc_check = */ stream.readUnsignedShort();
            }
            
            // TODO: check the sampling_frequency_index
            // TODO: check the channel_configuration
            
            // raw_data_blocks
            var adts_header_size:uint = stream.position - adts_header_start;
            var raw_data_size:uint = frame_length - adts_header_size;
            if (stream.bytesAvailable < raw_data_size) {
                throw new Error("aac: adts raw data corrupt.");
            }
            
            // the codec info.
            codec.protection_absent = protection_absent;
            codec.aac_object = SrsAacProfile.parse(profile).toRtmpObjectType();
            codec.sampling_frequency_index = sampling_frequency_index;
            codec.channel_configuration = channel_configuration;
            codec.frame_length = frame_length;
            
            // @see srs_audio_write_raw_frame().
            codec.sound_format = 10; // AAC
            // TODO: FIXME: maybe need to resample audio.
            if (sampling_frequency_index <= 0x0c && sampling_frequency_index > 0x0a) {
                codec.sound_rate = SrsConsts.SrsCodecAudioSampleRate5512;
            } else if (sampling_frequency_index <= 0x0a && sampling_frequency_index > 0x07) {
                codec.sound_rate = SrsConsts.SrsCodecAudioSampleRate11025;
            } else if (sampling_frequency_index <= 0x07 && sampling_frequency_index > 0x04) {
                codec.sound_rate = SrsConsts.SrsCodecAudioSampleRate22050;
            } else if (sampling_frequency_index <= 0x04) {
                codec.sound_rate = SrsConsts.SrsCodecAudioSampleRate44100;
            } else {
                codec.sound_rate = SrsConsts.SrsCodecAudioSampleRate44100;
                _log.warn("adts invalid sample rate for flv, rate=%{0}", sampling_frequency_index);
            }
            codec.sound_type = (uint)(Math.max(0, Math.min(1, channel_configuration - 1)));
            // TODO: FIXME: finger it out the sound size by adts.
            codec.sound_size = 1; // 0(8bits) or 1(16bits).
            
            // frame data.
            stream.readBytes(frame, 0, raw_data_size);
            
            break;
        }
        
        return {
            frame: frame,
            codec: codec
        };
    }
    /**
     * aac raw data to aac packet, without flv payload header.
     * mux the aac specific config to flv sequence header packet.
     * @param sh output the sequence header.
     */
    public function mux_sequence_header(codec:SrsRawAacStreamCodec):ByteArray
    {
        var sh:ByteArray = new ByteArray();
        
        // only support aac profile 1-4.
        if (codec.aac_object == SrsAacObjectType.Reserved) {
            throw new Error("aac: profile invalid.");
        }
        
        var audioObjectType:SrsAacObjectType = codec.aac_object;
        var channelConfiguration:uint = codec.channel_configuration;
        var samplingFrequencyIndex:uint = codec.sampling_frequency_index;
        
        // override the aac samplerate by user specified.
        // @see https://github.com/winlinvip/simple-rtmp-server/issues/212#issuecomment-64146899
        switch (codec.sound_rate) {
            case SrsConsts.SrsCodecAudioSampleRate11025: 
                samplingFrequencyIndex = 0x0a; break;
            case SrsConsts.SrsCodecAudioSampleRate22050: 
                samplingFrequencyIndex = 0x07; break;
            case SrsConsts.SrsCodecAudioSampleRate44100: 
                samplingFrequencyIndex = 0x04; break;
            default:
                break;
        }
        
        var ch:uint = 0;
        // @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf
        // AudioSpecificConfig (), page 33
        // 1.6.2.1 AudioSpecificConfig
        // audioObjectType; 5 bslbf
        ch = (audioObjectType.toInt() << 3) & 0xf8;
        // 3bits left.
        
        // samplingFrequencyIndex; 4 bslbf
        ch |= (samplingFrequencyIndex >> 1) & 0x07;
        sh.writeByte(ch);
        ch = (samplingFrequencyIndex << 7) & 0x80;
        if (samplingFrequencyIndex == 0x0f) {
            throw new Error("aac: sample rate invalid.");
        }
        // 7bits left.
        
        // channelConfiguration; 4 bslbf
        ch |= (channelConfiguration << 3) & 0x78;
        // 3bits left.
        
        // GASpecificConfig(), page 451
        // 4.4.1 Decoder configuration (GASpecificConfig)
        // frameLengthFlag; 1 bslbf
        // dependsOnCoreCoder; 1 bslbf
        // extensionFlag; 1 bslbf
        sh.writeByte(ch);
        
        return sh;
    }
    /**
     * mux the aac audio packet to flv audio packet.
     * @param frame the aac raw data.
     * @param nb_frame the count of aac frame.
     * @param codec the codec info of aac.
     * @param flv output the muxed flv packet.
     * @param nb_flv output the muxed flv size.
     */
    public function mux_aac2flv(frame:ByteArray, codec:SrsRawAacStreamCodec, dts:uint):ByteArray
    {
        var flv:ByteArray = new ByteArray();
        
        var sound_format:uint = codec.sound_format;
        var sound_type:uint = codec.sound_type;
        var sound_size:uint = codec.sound_size;
        var sound_rate:uint = codec.sound_rate;
        var aac_packet_type:uint = codec.aac_packet_type;
        
        // for audio frame, there is 1 or 2 bytes header:
        //      1bytes, SoundFormat|SoundRate|SoundSize|SoundType
        //      1bytes, AACPacketType for SoundFormat == 10, 0 is sequence header.
        
        var audio_header:uint = sound_type & 0x01;
        audio_header |= (sound_size << 1) & 0x02;
        audio_header |= (sound_rate << 2) & 0x0c;
        audio_header |= (sound_format << 4) & 0xf0;
        
        flv.writeByte(audio_header);
        
        if (sound_format == SrsConsts.SrsCodecAudioAAC) {
            flv.writeByte(aac_packet_type);
        }
        
        flv.writeBytes(frame);
        
        return flv;
    }
};

/**
 * the fake enum.
 */
class SrsEnum
{
    protected var value:int;
    
    public function SrsEnum(v:int)
    {
        value = v;
    }
    
    public function equals(e:SrsEnum):Boolean
    {
        return value == e.value;
    }
    
    public function notEquals(e:SrsEnum):Boolean
    {
        return value != e.value;
    }
    
    public function equalsValue(v:int):Boolean
    {
        return value == v;
    }
    
    public function toString():String
    {
        return String(value);
    }
    
    public function toInt():int
    {
        return value;
    }
}

/**
 * the aac profile, for ADTS(HLS/TS)
 * @see https://github.com/winlinvip/simple-rtmp-server/issues/310
 */
class SrsAacProfile extends SrsEnum
{
    public function SrsAacProfile(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsAacProfile
    {
        switch (v) {
            case 0x00: return SrsAacProfile.Main;
            case 0x01: return SrsAacProfile.LC;
            case 0x02: return SrsAacProfile.SSR;
            default: case 0x03: return SrsAacProfile.Reserved;
        }
    }
    
    public static const Reserved:SrsAacProfile = new SrsAacProfile(0x03);
        
    // @see 7.1 Profiles, aac-iso-13818-7.pdf, page 40
    public static const Main:SrsAacProfile = new SrsAacProfile(0x00);
    public static const LC:SrsAacProfile = new SrsAacProfile(0x01);
    public static const SSR:SrsAacProfile = new SrsAacProfile(0x02);
    
    // ts/hls/adts audio header profile to RTMP sequence header object type.
    public function toRtmpObjectType():SrsAacObjectType
    {
        if (SrsAacProfile.Main.equals(this)) {
            return SrsAacObjectType.AacMain;
        } else if (SrsAacProfile.LC.equals(this)) {
            return SrsAacObjectType.AacLC;
        } else if (SrsAacProfile.SSR.equals(this)) {
            return SrsAacObjectType.AacSSR;
        } else {
            return SrsAacObjectType.Reserved;
        }
    }
};

/**
 * the aac object type, for RTMP sequence header
 * for AudioSpecificConfig, @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 33
 * for audioObjectType, @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 23
 */
class SrsAacObjectType extends SrsEnum
{
    public function SrsAacObjectType(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsAacObjectType
    {
        switch (v) {
            case 0x01: return SrsAacObjectType.AacMain;
            case 0x02: return SrsAacObjectType.AacLC;
            case 0x03: return SrsAacObjectType.AacSSR;
            case 0x05: return SrsAacObjectType.AacHE;
            case 0x29: return SrsAacObjectType.AacHEV2;
            default: case 0x00: return SrsAacObjectType.Reserved;
        }
    }
    
    public static const Reserved:SrsAacObjectType = new SrsAacObjectType(0x00);
        
    // Table 1.1 – Audio Object Type definition
    // @see @see aac-mp4a-format-ISO_IEC_14496-3+2001.pdf, page 23
    public static const AacMain:SrsAacObjectType = new SrsAacObjectType(0x01);
    public static const AacLC:SrsAacObjectType = new SrsAacObjectType(0x02);
    public static const AacSSR:SrsAacObjectType = new SrsAacObjectType(0x03);
    
    // AAC HE = LC+SBR
    public static const AacHE:SrsAacObjectType = new SrsAacObjectType(0x05);
    // AAC HEv2 = LC+SBR+PS
    public static const AacHEV2:SrsAacObjectType = new SrsAacObjectType(0x29);
    
    // RTMP sequence header object type to ts/hls/adts audio header profile.
    public function toTsProfile():SrsAacProfile
    {
        if (SrsAacObjectType.AacMain.equals(this)) {
            return SrsAacProfile.Main;
        } else if (SrsAacObjectType.AacLC.equals(this)) {
            return SrsAacProfile.LC;
        } else if (SrsAacObjectType.AacHE.equals(this)) {
            return SrsAacProfile.LC;
        } else if (SrsAacObjectType.AacHEV2.equals(this)) {
            return SrsAacProfile.LC;
        } else if (SrsAacObjectType.AacSSR.equals(this)) {
            return SrsAacProfile.SSR;
        } else {
            return SrsAacProfile.Reserved;
        }
    }
};

/**
 * the pid of ts packet,
 * Table 2-3 - PID table, hls-mpeg-ts-iso13818-1.pdf, page 37
 * NOTE - The transport packets with PID values 0x0000, 0x0001, and 0x0010-0x1FFE are allowed to carry a PCR.
 */
class SrsTsPid extends SrsEnum
{
    public function SrsTsPid(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsTsPid
    {
        switch (v) {
            case 0x00: return SrsTsPid.PAT;
            case 0x01: return SrsTsPid.CAT;
            case 0x02: return SrsTsPid.TSDT;
            case 0x03: return SrsTsPid.ReservedStart;
            case 0x0f: return SrsTsPid.ReservedEnd;
            case 0x10: return SrsTsPid.AppStart;
            case 0x1ffe: return SrsTsPid.AppEnd;
            case 0x01FFF: return SrsTsPid.NULL;
            default: return new SrsTsPid(v);
        }
    }
    
    // Program Association Table(see Table 2-25).
    public static const PAT:SrsTsPid = new SrsTsPid(0x00);
    // Conditional Access Table (see Table 2-27).
    public static const CAT:SrsTsPid = new SrsTsPid(0x01);
    // Transport Stream Description Table
    public static const TSDT:SrsTsPid = new SrsTsPid(0x02);
    // Reserved
    public static const ReservedStart:SrsTsPid = new SrsTsPid(0x03);
    public static const ReservedEnd:SrsTsPid = new SrsTsPid(0x0f);
    // May be assigned as network_PID, Program_map_PID, elementary_PID, or for other purposes
    public static const AppStart:SrsTsPid = new SrsTsPid(0x10);
    public static const AppEnd:SrsTsPid = new SrsTsPid(0x1ffe);
    // null packets (see Table 2-3)
    public static const NULL:SrsTsPid = new SrsTsPid(0x01FFF);
};

/**
 * the transport_scrambling_control of ts packet,
 * Table 2-4 - Scrambling control values, hls-mpeg-ts-iso13818-1.pdf, page 38
 */
class SrsTsScrambled extends SrsEnum
{
    public function SrsTsScrambled(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsTsScrambled
    {
        switch (v) {
            case 0x01: return SrsTsScrambled.UserDefined1;
            case 0x02: return SrsTsScrambled.UserDefined2;
            case 0x03: return SrsTsScrambled.UserDefined3;
            default: case 0x00: return SrsTsScrambled.Disabled;
        }
    }
    
    // Not scrambled
    public static const Disabled:SrsTsScrambled = new SrsTsScrambled(0x00);
    // User-defined
    public static const UserDefined1:SrsTsScrambled = new SrsTsScrambled(0x01);
    // User-defined
    public static const UserDefined2:SrsTsScrambled = new SrsTsScrambled(0x02);
    // User-defined
    public static const UserDefined3:SrsTsScrambled = new SrsTsScrambled(0x03);
};

/**
 * the adaption_field_control of ts packet,
 * Table 2-5 - Adaptation field control values, hls-mpeg-ts-iso13818-1.pdf, page 38
 */
class SrsTsAdaptationFieldType extends SrsEnum
{
    public function SrsTsAdaptationFieldType(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsTsAdaptationFieldType
    {
        switch (v) {
            case 0x01: return SrsTsAdaptationFieldType.PayloadOnly;
            case 0x02: return SrsTsAdaptationFieldType.AdaptionOnly;
            case 0x03: return SrsTsAdaptationFieldType.Both;
            default: case 0x00: return SrsTsAdaptationFieldType.Reserved;
        }
    }
    
    // Reserved for future use by ISO/IEC
    public static const Reserved:SrsTsAdaptationFieldType = new SrsTsAdaptationFieldType(0x00);
    // No adaptation_field, payload only
    public static const PayloadOnly:SrsTsAdaptationFieldType = new SrsTsAdaptationFieldType(0x01);
    // Adaptation_field only, no payload
    public static const AdaptionOnly:SrsTsAdaptationFieldType = new SrsTsAdaptationFieldType(0x02);
    // Adaptation_field followed by payload
    public static const Both:SrsTsAdaptationFieldType = new SrsTsAdaptationFieldType(0x03);
};

/**
 * the actually parsed ts pid,
 * @see SrsTsPid, some pid, for example, PMT/Video/Audio is specified by PAT or other tables.
 */
class SrsTsPidApply extends SrsEnum
{
    public function SrsTsPidApply(v:int)
    {
        super(v);
    }
    
    public static const Reserved:SrsTsPidApply = new SrsTsPidApply(0x00); // TSPidTypeReserved, nothing parsed, used reserved.
    public static const PAT:SrsTsPidApply = new SrsTsPidApply(0x01); // Program associtate table
    public static const PMT:SrsTsPidApply = new SrsTsPidApply(0x02); // Program map table.
        
    public static const Video:SrsTsPidApply = new SrsTsPidApply(0x03); // for video
    public static const Audio:SrsTsPidApply = new SrsTsPidApply(0x04); // vor audio
};

/**
 * Table 2-29 - Stream type assignments
 */
class SrsTsStream extends SrsEnum
{
    public function SrsTsStream(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsTsStream
    {
        switch (v) {
            case 0x8a: return SrsTsStream.AudioDTS;
            case 0x80: return SrsTsStream.AudioAC3;
            case 0x1b: return SrsTsStream.VideoH264;
            case 0x11: return SrsTsStream.AudioMpeg4;
            case 0x10: return SrsTsStream.VideoMpeg4;
            case 0x0f: return SrsTsStream.AudioAAC;
            case 0x04: return SrsTsStream.AudioMp3;
            default: case 0x00: return SrsTsStream.Reserved;
        }
    }
    
    // ITU-T | ISO/IEC Reserved
    public static const Reserved:SrsTsStream = new SrsTsStream(0x00);
    // ISO/IEC 11172 Video
    // ITU-T Rec. H.262 | ISO/IEC 13818-2 Video or ISO/IEC 11172-2 constrained parameter video stream
    // ISO/IEC 11172 Audio
    // ISO/IEC 13818-3 Audio
    public static const AudioMp3:SrsTsStream = new SrsTsStream(0x04);
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 private_sections
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 PES packets containing private data
    // ISO/IEC 13522 MHEG
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Annex A DSM-CC
    // ITU-T Rec. H.222.1
    // ISO/IEC 13818-6 type A
    // ISO/IEC 13818-6 type B
    // ISO/IEC 13818-6 type C
    // ISO/IEC 13818-6 type D
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 auxiliary
    // ISO/IEC 13818-7 Audio with ADTS transport syntax
    public static const AudioAAC:SrsTsStream = new SrsTsStream(0x0f);
    // ISO/IEC 14496-2 Visual
    public static const VideoMpeg4:SrsTsStream = new SrsTsStream(0x10);
    // ISO/IEC 14496-3 Audio with the LATM transport syntax as defined in ISO/IEC 14496-3 / AMD 1
    public static const AudioMpeg4:SrsTsStream = new SrsTsStream(0x11);
    // ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in PES packets
    // ISO/IEC 14496-1 SL-packetized stream or FlexMux stream carried in ISO/IEC14496_sections.
    // ISO/IEC 13818-6 Synchronized Download Protocol
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 Reserved
    // 0x15-0x7F
    public static const VideoH264:SrsTsStream = new SrsTsStream(0x1b);
    // User Private
    // 0x80-0xFF
    public static const AudioAC3:SrsTsStream = new SrsTsStream(0x80);
    public static const AudioDTS:SrsTsStream = new SrsTsStream(0x8a);
};

/**
 * the ts channel.
 */
class SrsTsChannel
{
    public var pid:int;
    public var apply:SrsTsPidApply;
    public var stream:SrsTsStream;
    public var msg:SrsTsMessage;
    public var context:SrsTsContext;
    
    public function SrsTsChannel()
    {
        pid = 0;
        apply = SrsTsPidApply.Reserved;
        stream = SrsTsStream.Reserved;
        msg = null;
        context = null;
    }
};

/**
 * the stream_id of PES payload of ts packet.
 * Table 2-18 – Stream_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 52.
 */
class SrsTsPESStreamId extends SrsEnum
{
    public function SrsTsPESStreamId(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsTsPESStreamId
    {
        switch (v) {
            case 0xbc: return SrsTsPESStreamId.ProgramStreamMap;
            case 0xbd: return SrsTsPESStreamId.PrivateStream1;
            case 0xbe: return SrsTsPESStreamId.PaddingStream;
            case 0xbf: return SrsTsPESStreamId.PrivateStream2;
            case 0x06: return SrsTsPESStreamId.AudioChecker;
            case 0xc0: return SrsTsPESStreamId.AudioCommon;
            case 0x0e: return SrsTsPESStreamId.VideoChecker;
            case 0xe0: return SrsTsPESStreamId.VideoCommon;
            case 0xf0: return SrsTsPESStreamId.EcmStream;
            case 0xf1: return SrsTsPESStreamId.EmmStream;
            case 0xf2: return SrsTsPESStreamId.DsmccStream;
            case 0xf3: return SrsTsPESStreamId._13522Stream;
            case 0xf4: return SrsTsPESStreamId.H2221TypeA;
            case 0xf5: return SrsTsPESStreamId.H2221TypeB;
            case 0xf6: return SrsTsPESStreamId.H2221TypeC;
            case 0xf7: return SrsTsPESStreamId.H2221TypeD;
            case 0xf8: return SrsTsPESStreamId.H2221TypeE;
            case 0xf9: return SrsTsPESStreamId.AncillaryStream;
            case 0xfa: return SrsTsPESStreamId.SlPacketizedStream;
            case 0xfb: return SrsTsPESStreamId.FlexMuxStream;
            case 0xff: return SrsTsPESStreamId.ProgramStreamDirectory;
            default: case 0x00: return SrsTsPESStreamId.Reserved;
        }
    }
    
    // reserved
    public static const Reserved:SrsTsPESStreamId = new SrsTsPESStreamId(0x00);
    
    // program_stream_map
    public static const ProgramStreamMap:SrsTsPESStreamId = new SrsTsPESStreamId(0xbc);
    // private_stream_1
    public static const PrivateStream1:SrsTsPESStreamId = new SrsTsPESStreamId(0xbd);
    // padding_stream
    public static const PaddingStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xbe);
    // private_stream_2
    public static const PrivateStream2:SrsTsPESStreamId = new SrsTsPESStreamId(0xbf);
    
    // 110x xxxx
    // ISO/IEC 13818-3 or ISO/IEC 11172-3 or ISO/IEC 13818-7 or ISO/IEC
    // 14496-3 audio stream number x xxxx
    // ((sid >> 5) & 0x07) == SrsTsPESStreamIdAudio
    // @remark, use SrsTsPESStreamIdAudioCommon as actually audio, SrsTsPESStreamIdAudio to check whether audio.
    public static const AudioChecker:SrsTsPESStreamId = new SrsTsPESStreamId(0x06);
    public static const AudioCommon:SrsTsPESStreamId = new SrsTsPESStreamId(0xc0);
    
    // 1110 xxxx
    // ITU-T Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 11172-2 or ISO/IEC
    // 14496-2 video stream number xxxx
    // ((stream_id >> 4) & 0x0f) == SrsTsPESStreamIdVideo
    // @remark, use SrsTsPESStreamIdVideoCommon as actually video, SrsTsPESStreamIdVideo to check whether video.
    public static const VideoChecker:SrsTsPESStreamId = new SrsTsPESStreamId(0x0e);
    public static const VideoCommon:SrsTsPESStreamId = new SrsTsPESStreamId(0xe0);
    
    // ECM_stream
    public static const EcmStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xf0);
    // EMM_stream
    public static const EmmStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xf1);
    // DSMCC_stream
    public static const DsmccStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xf2);
    // 13522_stream
    public static const _13522Stream:SrsTsPESStreamId = new SrsTsPESStreamId(0xf3);
    // H_222_1_type_A
    public static const H2221TypeA:SrsTsPESStreamId = new SrsTsPESStreamId(0xf4);
    // H_222_1_type_B
    public static const H2221TypeB:SrsTsPESStreamId = new SrsTsPESStreamId(0xf5);
    // H_222_1_type_C
    public static const H2221TypeC:SrsTsPESStreamId = new SrsTsPESStreamId(0xf6);
    // H_222_1_type_D
    public static const H2221TypeD:SrsTsPESStreamId = new SrsTsPESStreamId(0xf7);
    // H_222_1_type_E
    public static const H2221TypeE:SrsTsPESStreamId = new SrsTsPESStreamId(0xf8);
    // ancillary_stream
    public static const AncillaryStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xf9);
    // SL_packetized_stream
    public static const SlPacketizedStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xfa);
    // FlexMux_stream
    public static const FlexMuxStream:SrsTsPESStreamId = new SrsTsPESStreamId(0xfb);
    // reserved data stream
    // 1111 1100 … 1111 1110
    // program_stream_directory
    public static const ProgramStreamDirectory:SrsTsPESStreamId = new SrsTsPESStreamId(0xff);
};

/**
 * the media audio/video message parsed from PES packet.
 */
class SrsTsMessage
{
    // decoder only,
    // the ts messgae does not use them, 
    // for user to get the channel and packet.
    public var channel:SrsTsChannel;
    public var packet:SrsTsPacket;
    // the audio cache buffer start pts, to flush audio if full.
    // @remark the pts is not the adjust one, it's the orignal pts.
    public var start_pts:Number;
    // whether this message with pcr info,
    // generally, the video IDR(I frame, the keyframe of h.264) carray the pcr info.
    public var write_pcr:Boolean;
    // whether got discontinuity ts, for example, sequence header changed.
    public var discontinuity:Boolean;
    // the timestamp in 90khz
    public var dts:Number;
    public var pts:Number;
    // the id of pes stream to indicates the payload codec.
    // @remark use is_audio() and is_video() to check it, and stream_number() to finger it out.
    public var sid:SrsTsPESStreamId;
    // the size of payload, 0 indicates the length() of payload.
    public var PES_packet_length:uint;
    // the chunk id.
    public var continuity_counter:uint;
    // the payload bytes.
    public var payload:ByteArray;
    
    public function SrsTsMessage(c:SrsTsChannel, p:SrsTsPacket)
    {
        channel = c;
        packet = p;
        
        dts = pts = 0;
        sid = SrsTsPESStreamId.Reserved;
        continuity_counter = 0;
        PES_packet_length = 0;
        payload = new ByteArray();
        
        start_pts = 0;
        write_pcr = false;
    }
    
    // decoder
    /**
     * dumps all bytes in stream to ts message.
     */
    public function dump(stream:ByteArray):int
    {
        if (!stream.bytesAvailable) {
            return 0;
        }
        
        // xB
        var nb_bytes:int = stream.length - stream.position;
        if (PES_packet_length > 0) {
            nb_bytes = (int)(Math.min(nb_bytes, PES_packet_length - payload.length));
        }
        
        if (nb_bytes > 0) {
            if (stream.bytesAvailable < nb_bytes) {
                throw new Error("ts: dump PSE bytes failed, requires=" + nb_bytes + " bytes");
            }
            
            stream.readBytes(payload, payload.length, nb_bytes);
        }
        
        return nb_bytes;
    }
    /**
     * whether ts message is completed to reap.
     * @param payload_unit_start_indicator whether new ts message start.
     *       PES_packet_length is 0, the payload_unit_start_indicator=1 to reap ts message.
     *       PES_packet_length > 0, the payload.length() == PES_packet_length to reap ts message.
     * @remark when PES_packet_length>0, the payload_unit_start_indicator should never be 1 when not completed.
     * @remark when fresh, the payload_unit_start_indicator should be 1.
     */
    public function completed(payload_unit_start_indicator:Number):Boolean
    {
        if (PES_packet_length == 0) {
            return payload_unit_start_indicator != 0;
        }
        return payload.length >= PES_packet_length;
    }
    /**
     * whether the message is fresh.
     */
    public function fresh():Boolean
    {
        return payload.length == 0;
    }
    
    /**
     * whether the sid indicates the elementary stream audio.
     */
    public function is_audio():Boolean
    {
        var sidValue:int = (sid.toInt() >> 5) & 0x07;
        return SrsTsPESStreamId.AudioChecker.equalsValue(sidValue);
    }
    /**
     * whether the sid indicates the elementary stream video.
     */
    public function is_video():Boolean
    {
        var sidValue:int = (sid.toInt() >> 4) & 0x0f;
        return SrsTsPESStreamId.VideoChecker.equalsValue(sidValue);
    }
    /**
     * when audio or video, get the stream number which specifies the format of stream.
     * @return the stream number for audio/video; otherwise, -1.
     */
    public function stream_number():Number
    {
        if (is_audio()) {
            return sid.toInt() & 0x1f;
        } else if (is_video()) {
            return sid.toInt() & 0x0f;
        }
        return -1;
    }
};

/**
 * the ts message handler.
 */
interface ISrsTsHandler
{
    /**
     * when ts context got message, use handler to process it.
     * @param msg the ts msg, user should never free it.
     * @return an int error code.
     */
    function on_ts_message(msg:SrsTsMessage):void;
};

/**
 * the context of ts, to decode the ts stream.
 */
class SrsTsContext
{
    private var _hls:HlsCodec;
    
    // codec
    //      key, a Number indicates the pid,
    //      value, the SrsTsChannel object.
    private var _pids:Dict;
    
    // whether hls pure audio stream.
    private var _pure_audio:Boolean;
    
    public function SrsTsContext(hls:HlsCodec)
    {
        _hls = hls;
        _pure_audio = false;
        _pids = new Dict();
    }
    
    /**
     * whether the hls stream is pure audio stream.
     */
    public function is_pure_audio():Boolean
    {
        return _pure_audio;
    }
    
    /**
    * when PMT table parsed, we know some info about stream.
    */
    public function on_pmt_parsed():void
    {
        _pure_audio = true;
        
        var keys:Array = _pids.keys();
        for (var i:int = 0; i < keys.length; i++) {
            var channel:SrsTsChannel = _pids.get(keys[i]) as SrsTsChannel;
            if (channel.apply == SrsTsPidApply.Video) {
                _pure_audio = false;
            }
        }
    }
    
    // codec
    /**
     * get the pid apply, the parsed pid.
     * @return the apply channel; NULL for invalid.
     */
    public function getChannel(pid:Number):SrsTsChannel
    {
        if (!_pids.has(pid)) {
            return null;
        }
        return _pids.get(pid) as SrsTsChannel;
    }
    
    /**
     * set the pid apply, the parsed pid.
     */
    public function setChannel(pid:Number, apply_pid:SrsTsPidApply, stream:SrsTsStream):void
    {
        var channel:SrsTsChannel = null;
        if (!_pids.get(pid)) {
            channel = new SrsTsChannel();
            channel.context = this;
            _pids.set(pid, channel);
        } else {
            channel = _pids.get(pid) as SrsTsChannel;
        }
        
        channel.pid = pid;
        channel.apply = apply_pid;
        channel.stream = stream;
    }
    
    // decode methods
    /**
     * the stream contains only one ts packet.
     * @param handler the ts message handler to process the msg.
     * @remark we will consume all bytes in stream.
     */
    public function decode(stream:ByteArray, handler:ISrsTsHandler):void
    {
        // parse util EOF of stream.
        // for example, parse multiple times for the PES_packet_length(0) packet.
        while (stream.bytesAvailable) {
            var packet:SrsTsPacket = new SrsTsPacket(this);
            var msg:SrsTsMessage = packet.decode(stream);
            
            if (!msg) {
                continue;
            }
            
            handler.on_ts_message(msg);
        }
    }
};

/**
 * the packet in ts stream,
 * 2.4.3.2 Transport Stream packet layer, hls-mpeg-ts-iso13818-1.pdf, page 36
 * Transport Stream packets shall be 188 bytes long.
 */
class SrsTsPacket
{
    // 1B
    /**
     * The sync_byte is a fixed 8-bit field whose value is '0100 0111' (0x47) or '0111 0100' (0x74). Sync_byte emulation in the choice of
     * values for other regularly occurring fields, such as PID, should be avoided.
     */
    public var sync_byte:int; //8bits
    
    // 2B
    /**
     * The transport_error_indicator is a 1-bit flag. When set to '1' it indicates that at least
     * 1 uncorrectable bit error exists in the associated Transport Stream packet. This bit may be set to '1' by entities external to
     * the transport layer. When set to '1' this bit shall not be reset to '0' unless the bit value(s) in error have been corrected.
     */
    public var transport_error_indicator:int; //1bit
    /**
     * The payload_unit_start_indicator is a 1-bit flag which has normative meaning for
     * Transport Stream packets that carry PES packets (refer to 2.4.3.6) or PSI data (refer to 2.4.4).
     * 
     * When the payload of the Transport Stream packet contains PES packet data, the payload_unit_start_indicator has the
     * following significance: a '1' indicates that the payload of this Transport Stream packet will commence(start) with the first byte
     * of a PES packet and a '0' indicates no PES packet shall start in this Transport Stream packet. If the
     * payload_unit_start_indicator is set to '1', then one and only one PES packet starts in this Transport Stream packet. This
     * also applies to private streams of stream_type 6 (refer to Table 2-29).
     *
     * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
     * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
     * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
     * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
     * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
     * stream_type 5 (refer to Table 2-29).
     * 
     * For null packets the payload_unit_start_indicator shall be set to '0'.
     * 
     * The meaning of this bit for Transport Stream packets carrying only private data is not defined in this Specification.
     */
    public var payload_unit_start_indicator:int; //1bit
    /**
     * The transport_priority is a 1-bit indicator. When set to '1' it indicates that the associated packet is
     * of greater priority than other packets having the same PID which do not have the bit set to '1'. The transport mechanism
     * can use this to prioritize its data within an elementary stream. Depending on the application the transport_priority field
     * may be coded regardless of the PID or within one PID only. This field may be changed by channel specific encoders or
     * decoders.
     */
    public var transport_priority:int; //1bit
    /**
     * The PID is a 13-bit field, indicating the type of the data stored in the packet payload. PID value 0x0000 is
     * reserved for the Program Association Table (see Table 2-25). PID value 0x0001 is reserved for the Conditional Access
     * Table (see Table 2-27). PID values 0x0002 - 0x000F are reserved. PID value 0x1FFF is reserved for null packets (see
     * Table 2-3).
     */
    public var pid:int; //13bits
    
    // 1B
    /**
     * This 2-bit field indicates the scrambling mode of the Transport Stream packet payload.
     * The Transport Stream packet header, and the adaptation field when present, shall not be scrambled. In the case of a null
     * packet the value of the transport_scrambling_control field shall be set to '00' (see Table 2-4).
     */
    public var transport_scrambling_control:SrsTsScrambled; //2bits
    /**
     * This 2-bit field indicates whether this Transport Stream packet header is followed by an
     * adaptation field and/or payload (see Table 2-5).
     *
     * ITU-T Rec. H.222.0 | ISO/IEC 13818-1 decoders shall discard Transport Stream packets with the
     * adaptation_field_control field set to a value of '00'. In the case of a null packet the value of the adaptation_field_control
     * shall be set to '01'.
     */
    public var adaption_field_control:SrsTsAdaptationFieldType; //2bits
    /**
     * The continuity_counter is a 4-bit field incrementing with each Transport Stream packet with the
     * same PID. The continuity_counter wraps around to 0 after its maximum value. The continuity_counter shall not be
     * incremented when the adaptation_field_control of the packet equals '00'(reseverd) or '10'(adaptation field only).
     * 
     * In Transport Streams, duplicate packets may be sent as two, and only two, consecutive Transport Stream packets of the
     * same PID. The duplicate packets shall have the same continuity_counter value as the original packet and the
     * adaptation_field_control field shall be equal to '01'(payload only) or '11'(both). In duplicate packets each byte of the original packet shall be
     * duplicated, with the exception that in the program clock reference fields, if present, a valid value shall be encoded.
     *
     * The continuity_counter in a particular Transport Stream packet is continuous when it differs by a positive value of one
     * from the continuity_counter value in the previous Transport Stream packet of the same PID, or when either of the nonincrementing
     * conditions (adaptation_field_control set to '00' or '10', or duplicate packets as described above) are met.
     * The continuity counter may be discontinuous when the discontinuity_indicator is set to '1' (refer to 2.4.3.4). In the case of
     * a null packet the value of the continuity_counter is undefined.
     */
    public var continuity_counter:int; //4bits
    
    private var adaptation_field:SrsTsAdaptationField;
    private var payload:SrsTsPayload;
    
    public var context:SrsTsContext;
    
    public function SrsTsPacket(c:SrsTsContext)
    {
        context = c;
    }
    
    public function decode(stream:ByteArray):SrsTsMessage
    {
        var pos:uint = stream.position;
        
        // 4B ts packet header.
        if (stream.bytesAvailable < 4) {
            throw new Error("ts: demux header failed");
        }
        
        sync_byte = stream.readUnsignedByte();
        // drm algorithms for ts packet:
        // 1: "tiger", sync_byte is 0x74
        if (sync_byte != 0x47 && sync_byte != 0x74) {
            throw new Error("ts: sync_bytes must be 0x47 or 0x74, actual=" + sync_byte);
        }
        
        var pidv:int = stream.readUnsignedShort();
        transport_error_indicator = (pidv >> 15) & 0x01;
        payload_unit_start_indicator = (pidv >> 14) & 0x01;
        transport_priority = (pidv >> 13) & 0x01;
        pid = pidv & 0x1FFF;
        
        var ccv:int = stream.readUnsignedByte();
        transport_scrambling_control = SrsTsScrambled.parse((ccv >> 6) & 0x03);
        adaption_field_control = SrsTsAdaptationFieldType.parse((ccv >> 4) & 0x03);
        continuity_counter = ccv & 0x0F;
        
        // TODO: FIXME: create pids map when got new pid.
        
        // optional: adaptation field
        if (adaption_field_control == SrsTsAdaptationFieldType.AdaptionOnly 
            || adaption_field_control == SrsTsAdaptationFieldType.Both
        ) {
            adaptation_field = new SrsTsAdaptationField(this);
            adaptation_field.decode(stream);
        }
        
        // calc the user defined data size for payload.
        var nb_payload:int = HlsCodec.SRS_TS_PACKET_SIZE - (stream.position - pos);
        
        // optional: payload.
        if (adaption_field_control == SrsTsAdaptationFieldType.PayloadOnly 
            || adaption_field_control == SrsTsAdaptationFieldType.Both
        ) {
            if (SrsTsPid.PAT.equalsValue(pid)) {
                // 2.4.4.3 Program association Table
                payload = new SrsTsPayloadPAT(this);
            } else {
                var channel:SrsTsChannel = context.getChannel(pid);
                if (channel && channel.apply == SrsTsPidApply.PMT) {
                    // 2.4.4.8 Program Map Table
                    payload = new SrsTsPayloadPMT(this);
                } else if (channel && (channel.apply == SrsTsPidApply.Video || channel.apply == SrsTsPidApply.Audio)) {
                    // 2.4.3.6 PES packet
                    payload = new SrsTsPayloadPES(this);
                } else {
                    // left bytes as reserved.
                    stream.position += nb_payload;
                }
            }
            
            if (payload) {
                return payload.decode(stream);
            }
        }
        
        return null;
    }
    public function size():int
    {
        return 0;
    }
    
    public static function create_pat(context:SrsTsContext, pmt_number:int, pmt_pid:int):SrsTsPacket
    {
        return null;
    }
    public static function create_pmt(context:SrsTsContext, 
        pmt_number:int, pmt_pid:int, vpid:int, vs:SrsTsStream, apid:int, ts:SrsTsStream):SrsTsPacket
    {
        return null;
    }
    public static function create_pes_first(context:SrsTsContext, 
        pid:int, sid:SrsTsPESStreamId, continuity_counter:uint, discontinuity:Boolean, 
        pcr:Number, dts:Number, pts:Number, size:int):SrsTsPacket
    {
        return null;
    }
    public static function create_pes_continue(context:SrsTsContext, 
        pid:int, sid:SrsTsPESStreamId, continuity_counter:int
    ):SrsTsPacket
    {
        return null;
    }
};

/**
 * the adaption field of ts packet.
 * 2.4.3.5 Semantic definition of fields in adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 39
 * Table 2-6 - Transport Stream adaptation field, hls-mpeg-ts-iso13818-1.pdf, page 40
 */
class SrsTsAdaptationField
{
    // 1B
    /**
     * The adaptation_field_length is an 8-bit field specifying the number of bytes in the
     * adaptation_field immediately following the adaptation_field_length. The value 0 is for inserting a single stuffing byte in
     * a Transport Stream packet. When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
     * be in the range 0 to 182. When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
     * be 183. For Transport Stream packets carrying PES packets, stuffing is needed when there is insufficient PES packet data
     * to completely fill the Transport Stream packet payload bytes. Stuffing is accomplished by defining an adaptation field
     * longer than the sum of the lengths of the data elements in it, so that the payload bytes remaining after the adaptation field
     * exactly accommodates the available PES packet data. The extra space in the adaptation field is filled with stuffing bytes.
     *
     * This is the only method of stuffing allowed for Transport Stream packets carrying PES packets. For Transport Stream
     * packets carrying PSI, an alternative stuffing method is described in 2.4.4.
     */
    public var adaption_field_length:int; //8bits
    // 1B
    /**
     * This is a 1-bit field which when set to '1' indicates that the discontinuity state is true for the
     * current Transport Stream packet. When the discontinuity_indicator is set to '0' or is not present, the discontinuity state is
     * false. The discontinuity indicator is used to indicate two types of discontinuities, system time-base discontinuities and
     * continuity_counter discontinuities.
     * 
     * A system time-base discontinuity is indicated by the use of the discontinuity_indicator in Transport Stream packets of a
     * PID designated as a PCR_PID (refer to 2.4.4.9). When the discontinuity state is true for a Transport Stream packet of a
     * PID designated as a PCR_PID, the next PCR in a Transport Stream packet with that same PID represents a sample of a
     * new system time clock for the associated program. The system time-base discontinuity point is defined to be the instant
     * in time when the first byte of a packet containing a PCR of a new system time-base arrives at the input of the T-STD.
     * The discontinuity_indicator shall be set to '1' in the packet in which the system time-base discontinuity occurs. The
     * discontinuity_indicator bit may also be set to '1' in Transport Stream packets of the same PCR_PID prior to the packet
     * which contains the new system time-base PCR. In this case, once the discontinuity_indicator has been set to '1', it shall
     * continue to be set to '1' in all Transport Stream packets of the same PCR_PID up to and including the Transport Stream
     * packet which contains the first PCR of the new system time-base. After the occurrence of a system time-base
     * discontinuity, no fewer than two PCRs for the new system time-base shall be received before another system time-base
     * discontinuity can occur. Further, except when trick mode status is true, data from no more than two system time-bases
     * shall be present in the set of T-STD buffers for one program at any time.
     *
     * Prior to the occurrence of a system time-base discontinuity, the first byte of a Transport Stream packet which contains a
     * PTS or DTS which refers to the new system time-base shall not arrive at the input of the T-STD. After the occurrence of
     * a system time-base discontinuity, the first byte of a Transport Stream packet which contains a PTS or DTS which refers
     * to the previous system time-base shall not arrive at the input of the T-STD.
     *
     * A continuity_counter discontinuity is indicated by the use of the discontinuity_indicator in any Transport Stream packet.
     * When the discontinuity state is true in any Transport Stream packet of a PID not designated as a PCR_PID, the
     * continuity_counter in that packet may be discontinuous with respect to the previous Transport Stream packet of the same
     * PID. When the discontinuity state is true in a Transport Stream packet of a PID that is designated as a PCR_PID, the
     * continuity_counter may only be discontinuous in the packet in which a system time-base discontinuity occurs. A
     * continuity counter discontinuity point occurs when the discontinuity state is true in a Transport Stream packet and the
     * continuity_counter in the same packet is discontinuous with respect to the previous Transport Stream packet of the same
     * PID. A continuity counter discontinuity point shall occur at most one time from the initiation of the discontinuity state
     * until the conclusion of the discontinuity state. Furthermore, for all PIDs that are not designated as PCR_PIDs, when the
     * discontinuity_indicator is set to '1' in a packet of a specific PID, the discontinuity_indicator may be set to '1' in the next
     * Transport Stream packet of that same PID, but shall not be set to '1' in three consecutive Transport Stream packet of that
     * same PID.
     *
     * For the purpose of this clause, an elementary stream access point is defined as follows:
     *       Video - The first byte of a video sequence header.
     *       Audio - The first byte of an audio frame.
     *
     * After a continuity counter discontinuity in a Transport packet which is designated as containing elementary stream data,
     * the first byte of elementary stream data in a Transport Stream packet of the same PID shall be the first byte of an
     * elementary stream access point or in the case of video, the first byte of an elementary stream access point or a
     * sequence_end_code followed by an access point. Each Transport Stream packet which contains elementary stream data
     * with a PID not designated as a PCR_PID, and in which a continuity counter discontinuity point occurs, and in which a
     * PTS or DTS occurs, shall arrive at the input of the T-STD after the system time-base discontinuity for the associated
     * program occurs. In the case where the discontinuity state is true, if two consecutive Transport Stream packets of the same
     * PID occur which have the same continuity_counter value and have adaptation_field_control values set to '01' or '11', the
     * second packet may be discarded. A Transport Stream shall not be constructed in such a way that discarding such a packet
     * will cause the loss of PES packet payload data or PSI data.
     *
     * After the occurrence of a discontinuity_indicator set to '1' in a Transport Stream packet which contains PSI information,
     * a single discontinuity in the version_number of PSI sections may occur. At the occurrence of such a discontinuity, a
     * version of the TS_program_map_sections of the appropriate program shall be sent with section_length = = 13 and the
     * current_next_indicator = = 1, such that there are no program_descriptors and no elementary streams described. This shall
     * then be followed by a version of the TS_program_map_section for each affected program with the version_number
     * incremented by one and the current_next_indicator = = 1, containing a complete program definition. This indicates a
     * version change in PSI data.
     */
    public var discontinuity_indicator:int; //1bit
    /**
     * The random_access_indicator is a 1-bit field that indicates that the current Transport
     * Stream packet, and possibly subsequent Transport Stream packets with the same PID, contain some information to aid
     * random access at this point. Specifically, when the bit is set to '1', the next PES packet to start in the payload of Transport
     * Stream packets with the current PID shall contain the first byte of a video sequence header if the PES stream type (refer
     * to Table 2-29) is 1 or 2, or shall contain the first byte of an audio frame if the PES stream type is 3 or 4. In addition, in
     * the case of video, a presentation timestamp shall be present in the PES packet containing the first picture following the
     * sequence header. In the case of audio, the presentation timestamp shall be present in the PES packet containing the first
     * byte of the audio frame. In the PCR_PID the random_access_indicator may only be set to '1' in Transport Stream packet
     * containing the PCR fields.
     */
    public var random_access_indicator:int; //1bit
    /**
     * The elementary_stream_priority_indicator is a 1-bit field. It indicates, among
     * packets with the same PID, the priority of the elementary stream data carried within the payload of this Transport Stream
     * packet. A '1' indicates that the payload has a higher priority than the payloads of other Transport Stream packets. In the
     * case of video, this field may be set to '1' only if the payload contains one or more bytes from an intra-coded slice. A
     * value of '0' indicates that the payload has the same priority as all other packets which do not have this bit set to '1'.
     */
    public var elementary_stream_priority_indicator:int; //1bit
    /**
     * The PCR_flag is a 1-bit flag. A value of '1' indicates that the adaptation_field contains a PCR field coded in
     * two parts. A value of '0' indicates that the adaptation field does not contain any PCR field.
     */
    public var PCR_flag:int; //1bit
    /**
     * The OPCR_flag is a 1-bit flag. A value of '1' indicates that the adaptation_field contains an OPCR field
     * coded in two parts. A value of '0' indicates that the adaptation field does not contain any OPCR field.
     */
    public var OPCR_flag:int; //1bit
    /**
     * The splicing_point_flag is a 1-bit flag. When set to '1', it indicates that a splice_countdown field
     * shall be present in the associated adaptation field, specifying the occurrence of a splicing point. A value of '0' indicates
     * that a splice_countdown field is not present in the adaptation field.
     */
    public var splicing_point_flag:int; //1bit
    /**
     * The transport_private_data_flag is a 1-bit flag. A value of '1' indicates that the
     * adaptation field contains one or more private_data bytes. A value of '0' indicates the adaptation field does not contain any
     * private_data bytes.
     */
    public var transport_private_data_flag:int; //1bit
    /**
     * The adaptation_field_extension_flag is a 1-bit field which when set to '1' indicates
     * the presence of an adaptation field extension. A value of '0' indicates that an adaptation field extension is not present in
     * the adaptation field.
     */
    public var adaptation_field_extension_flag:int; //1bit
    
    // if PCR_flag, 6B
    /**
     * The program_clock_reference (PCR) is a
     * 42-bit field coded in two parts. The first part, program_clock_reference_base, is a 33-bit field whose value is given by
     * PCR_base(i), as given in equation 2-2. The second part, program_clock_reference_extension, is a 9-bit field whose value
     * is given by PCR_ext(i), as given in equation 2-3. The PCR indicates the intended time of arrival of the byte containing
     * the last bit of the program_clock_reference_base at the input of the system target decoder.
     */
    public var program_clock_reference_base:Number; //33bits
    /**
     * 6bits reserved, must be '1'
     */
    public var const1_value0:int; // 6bits
    public var program_clock_reference_extension:int; //9bits
    
    // if OPCR_flag, 6B
    /**
     * The optional original
     * program reference (OPCR) is a 42-bit field coded in two parts. These two parts, the base and the extension, are coded
     * identically to the two corresponding parts of the PCR field. The presence of the OPCR is indicated by the OPCR_flag.
     * The OPCR field shall be coded only in Transport Stream packets in which the PCR field is present. OPCRs are permitted
     * in both single program and multiple program Transport Streams.
     *
     * OPCR assists in the reconstruction of a single program Transport Stream from another Transport Stream. When
     * reconstructing the original single program Transport Stream, the OPCR may be copied to the PCR field. The resulting
     * PCR value is valid only if the original single program Transport Stream is reconstructed exactly in its entirety. This
     * would include at least any PSI and private data packets which were present in the original Transport Stream and would
     * possibly require other private arrangements. It also means that the OPCR must be an identical copy of its associated PCR
     * in the original single program Transport Stream.
     */
    public var original_program_clock_reference_base:Number; //33bits
    /**
     * 6bits reserved, must be '1'
     */
    public var const1_value2:int; // 6bits
    public var original_program_clock_reference_extension:int; //9bits
    
    // if splicing_point_flag, 1B
    /**
     * The splice_countdown is an 8-bit field, representing a value which may be positive or negative. A
     * positive value specifies the remaining number of Transport Stream packets, of the same PID, following the associated
     * Transport Stream packet until a splicing point is reached. Duplicate Transport Stream packets and Transport Stream
     * packets which only contain adaptation fields are excluded. The splicing point is located immediately after the last byte of
     * the Transport Stream packet in which the associated splice_countdown field reaches zero. In the Transport Stream packet
     * where the splice_countdown reaches zero, the last data byte of the Transport Stream packet payload shall be the last byte
     * of a coded audio frame or a coded picture. In the case of video, the corresponding access unit may or may not be
     * terminated by a sequence_end_code. Transport Stream packets with the same PID, which follow, may contain data from
     * a different elementary stream of the same type.
     *
     * The payload of the next Transport Stream packet of the same PID (duplicate packets and packets without payload being
     * excluded) shall commence with the first byte of a PES packet.In the case of audio, the PES packet payload shall
     * commence with an access point. In the case of video, the PES packet payload shall commence with an access point, or
     * with a sequence_end_code, followed by an access point. Thus, the previous coded audio frame or coded picture aligns
     * with the packet boundary, or is padded to make this so. Subsequent to the splicing point, the countdown field may also
     * be present. When the splice_countdown is a negative number whose value is minus n(-n), it indicates that the associated
     * Transport Stream packet is the n-th packet following the splicing point (duplicate packets and packets without payload
     * being excluded).
     * 
     * For the purposes of this subclause, an access point is defined as follows:
     *       Video - The first byte of a video_sequence_header.
     *       Audio - The first byte of an audio frame.
     */
    public var splice_countdown:int; //8bits
    
    // if transport_private_data_flag, 1+p[0] B
    /**
     * The transport_private_data_length is an 8-bit field specifying the number of
     * private_data bytes immediately following the transport private_data_length field. The number of private_data bytes shall
     * not be such that private data extends beyond the adaptation field.
     */
    public var transport_private_data_length:int; //8bits
    public var transport_private_data:ByteArray; //[transport_private_data_length]bytes
    
    // if adaptation_field_extension_flag, 2+x B
    /**
     * The adaptation_field_extension_length is an 8-bit field. It indicates the number of
     * bytes of the extended adaptation field data immediately following this field, including reserved bytes if present.
     */
    public var adaptation_field_extension_length:int; //8bits
    /**
     * This is a 1-bit field which when set to '1' indicates the presence of the ltw_offset
     * field.
     */
    public var ltw_flag:int; //1bit
    /**
     * This is a 1-bit field which when set to '1' indicates the presence of the piecewise_rate field.
     */
    public var piecewise_rate_flag:int; //1bit
    /**
     * This is a 1-bit flag which when set to '1' indicates that the splice_type and DTS_next_AU fields
     * are present. A value of '0' indicates that neither splice_type nor DTS_next_AU fields are present. This field shall not be
     * set to '1' in Transport Stream packets in which the splicing_point_flag is not set to '1'. Once it is set to '1' in a Transport
     * Stream packet in which the splice_countdown is positive, it shall be set to '1' in all the subsequent Transport Stream
     * packets of the same PID that have the splicing_point_flag set to '1', until the packet in which the splice_countdown
     * reaches zero (including this packet). When this flag is set, if the elementary stream carried in this PID is an audio stream,
     * the splice_type field shall be set to '0000'. If the elementary stream carried in this PID is a video stream, it shall fulfil the
     * constraints indicated by the splice_type value.
     */
    public var seamless_splice_flag:int; //1bit
    /**
     * reserved 5bits, must be '1'
     */
    public var const1_value1:int; //5bits
    // if ltw_flag, 2B
    /**
     * (legal time window_valid_flag) - This is a 1-bit field which when set to '1' indicates that the value of the
     * ltw_offset shall be valid. A value of '0' indicates that the value in the ltw_offset field is undefined.
     */
    public var ltw_valid_flag:int; //1bit
    /**
     * (legal time window offset) - This is a 15-bit field, the value of which is defined only if the ltw_valid flag has
     * a value of '1'. When defined, the legal time window offset is in units of (300/fs) seconds, where fs is the system clock
     * frequency of the program that this PID belongs to, and fulfils:
     *       offset = t1(i) - t(i)
     *       ltw_offset = offset//1
     * where i is the index of the first byte of this Transport Stream packet, offset is the value encoded in this field, t(i) is the
     * arrival time of byte i in the T-STD, and t1(i) is the upper bound in time of a time interval called the Legal Time Window
     * which is associated with this Transport Stream packet.
     */
    public var ltw_offset:int; //15bits
    // if piecewise_rate_flag, 3B
    //2bits reserved
    /**
     * The meaning of this 22-bit field is only defined when both the ltw_flag and the ltw_valid_flag are set
     * to '1'. When defined, it is a positive integer specifying a hypothetical bitrate R which is used to define the end times of
     * the Legal Time Windows of Transport Stream packets of the same PID that follow this packet but do not include the
     * legal_time_window_offset field.
     */
    public var piecewise_rate:int; //22bits
    // if seamless_splice_flag, 5B
    /**
     * This is a 4-bit field. From the first occurrence of this field onwards, it shall have the same value in all the
     * subsequent Transport Stream packets of the same PID in which it is present, until the packet in which the
     * splice_countdown reaches zero (including this packet). If the elementary stream carried in that PID is an audio stream,
     * this field shall have the value '0000'. If the elementary stream carried in that PID is a video stream, this field indicates the
     * conditions that shall be respected by this elementary stream for splicing purposes. These conditions are defined as a
     * function of profile, level and splice_type in Table 2-7 through Table 2-16.
     */
    public var splice_type:int; //4bits
    /**
     * (decoding time stamp next access unit) - This is a 33-bit field, coded in three parts. In the case of
     * continuous and periodic decoding through this splicing point it indicates the decoding time of the first access unit
     * following the splicing point. This decoding time is expressed in the time base which is valid in the Transport Stream
     * packet in which the splice_countdown reaches zero. From the first occurrence of this field onwards, it shall have the
     * same value in all the subsequent Transport Stream packets of the same PID in which it is present, until the packet in
     * which the splice_countdown reaches zero (including this packet).
     */
    public var DTS_next_AU0:int; //3bits
    public var marker_bit0:int; //1bit
    public var DTS_next_AU1:int; //15bits
    public var marker_bit1:int; //1bit
    public var DTS_next_AU2:int; //15bits
    public var marker_bit2:int; //1bit
    // left bytes.
    /**
     * This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder. It is discarded by the
     * decoder.
     */
    public var nb_af_ext_reserved:int;
    
    // left bytes.
    /**
     * This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder. It is discarded by the
     * decoder.
     */
    public var nb_af_reserved:int;
    
    private var packet:SrsTsPacket;
    
    public function SrsTsAdaptationField(pkt:SrsTsPacket)
    {
        packet = pkt;
        
        adaption_field_length = 0;
        discontinuity_indicator = 0;
        random_access_indicator = 0;
        elementary_stream_priority_indicator = 0;
        PCR_flag = 0;
        OPCR_flag = 0;
        splicing_point_flag = 0;
        transport_private_data_flag = 0;
        adaptation_field_extension_flag = 0;
        program_clock_reference_base = 0;
        program_clock_reference_extension = 0;
        original_program_clock_reference_base = 0;
        original_program_clock_reference_extension = 0;
        splice_countdown = 0;
        transport_private_data_length = 0;
        transport_private_data = null;
        adaptation_field_extension_length = 0;
        ltw_flag = 0;
        piecewise_rate_flag = 0;
        seamless_splice_flag = 0;
        ltw_valid_flag = 0;
        ltw_offset = 0;
        piecewise_rate = 0;
        splice_type = 0;
        DTS_next_AU0 = 0;
        marker_bit0 = 0;
        DTS_next_AU1 = 0;
        marker_bit1 = 0;
        DTS_next_AU2 = 0;
        marker_bit2 = 0;
        nb_af_ext_reserved = 0;
        nb_af_reserved = 0;
        
        const1_value0 = 0x3F;
        const1_value1 = 0x1F;
        const1_value2 = 0x3F;
    }
    
    public function decode(stream:ByteArray):void
    {
        if (stream.bytesAvailable < 2) {
            throw new Error("ts: demux af failed.");
        }
        adaption_field_length = stream.readUnsignedByte();
        
        // When the adaptation_field_control value is '11', the value of the adaptation_field_length shall
        // be in the range 0 to 182. 
        if (packet.adaption_field_control == SrsTsAdaptationFieldType.Both && adaption_field_length > 182) {
            throw new Error("ts: demux af length failed, must in [0, 182], actual=" + adaption_field_length);
        }
        // When the adaptation_field_control value is '10', the value of the adaptation_field_length shall
        // be 183.
        if (packet.adaption_field_control == SrsTsAdaptationFieldType.AdaptionOnly && adaption_field_length != 183) {
            throw new Error("ts: demux af length failed, must be 183, actual=" + adaption_field_length);
        }
        
        // no adaptation field.
        if (adaption_field_length == 0) {
            //info("ts: demux af empty.");
            return;
        }
        
        // the adaptation field start at here.
        var pos_af:uint = stream.position;
        var tmpv:int = stream.readUnsignedByte();
        
        discontinuity_indicator              =   (tmpv >> 7) & 0x01;
        random_access_indicator              =   (tmpv >> 6) & 0x01;
        elementary_stream_priority_indicator =   (tmpv >> 5) & 0x01;
        PCR_flag                             =   (tmpv >> 4) & 0x01;
        OPCR_flag                            =   (tmpv >> 3) & 0x01;
        splicing_point_flag                  =   (tmpv >> 2) & 0x01;
        transport_private_data_flag          =   (tmpv >> 1) & 0x01;
        adaptation_field_extension_flag      =   tmpv & 0x01;
        
        if (PCR_flag) {
            if (stream.bytesAvailable < 6) {
                throw new Error("ts: demux af PCR_flag failed.");
            }
            
            // @remark, for as, should never shift the Number object.
            // @remark, use pcr base and ignore the extension
            // @see https://github.com/winlinvip/simple-rtmp-server/issues/250#issuecomment-71349370
            // first 33bits is pcr base.
            program_clock_reference_base = (stream.readUnsignedInt() << 1) & 0x1fffffe;
            var ch:uint = stream.readUnsignedByte();
            program_clock_reference_base += (ch >> 7) & 0x01;
            // next 6bits is the const values
            const1_value0 = (ch >> 1) & 0x3f;
            // last 9bits is the pcr ext.
            program_clock_reference_extension = (ch << 8) & 0x1ff;
            program_clock_reference_extension |= stream.readUnsignedByte();
        }
        
        if (OPCR_flag) {
            if (stream.bytesAvailable < 6) {
                throw new Error("ts: demux af OPCR_flag failed.");
            }
            // @remark, for as, should never shift the Number object.
            // @remark, use pcr base and ignore the extension
            // @see https://github.com/winlinvip/simple-rtmp-server/issues/250#issuecomment-71349370
            // first 33bits is pcr base.
            original_program_clock_reference_base = (stream.readUnsignedInt() << 1) & 0x1fffffe;
            ch = stream.readUnsignedByte();
            original_program_clock_reference_base += (ch >> 7) & 0x01;
            // next 6bits is the const values
            const1_value2 = (ch >> 1) & 0x3f;
            // last 9bits is the pcr ext.
            original_program_clock_reference_extension = (ch << 8) & 0x1ff;
            original_program_clock_reference_extension |= stream.readUnsignedByte();
        }
        
        if (splicing_point_flag) {
            if (stream.bytesAvailable < 1) {
                throw new Error("ts: demux af splicing_point_flag failed.");
            }
            splice_countdown = stream.readUnsignedByte();
        }
        
        if (transport_private_data_flag) {
            if (stream.bytesAvailable < 1) {
                throw new Error("ts: demux af transport_private_data_flag failed.");
            }
            transport_private_data_length = stream.readUnsignedByte();
            
            if (transport_private_data_length> 0) {
                if (stream.bytesAvailable < transport_private_data_length) {
                    throw new Error("ts: demux af transport_private_data_flag failed.");
                }
                transport_private_data = new ByteArray();
                stream.readBytes(transport_private_data, 0, transport_private_data_length);
            }
        }
        
        if (adaptation_field_extension_flag) {
            var pos_af_ext:uint = stream.position;
            
            if (stream.bytesAvailable < 2) {
                throw new Error("ts: demux af adaptation_field_extension_flag failed.");
            }
            adaptation_field_extension_length = stream.readUnsignedByte();
            var ltwfv:int = stream.readUnsignedByte();
            
            piecewise_rate_flag = (ltwfv >> 6) & 0x01;
            seamless_splice_flag = (ltwfv >> 5) & 0x01;
            ltw_flag = (ltwfv >> 7) & 0x01;
            const1_value1 = ltwfv & 0x1F;
            
            if (ltw_flag) {
                if (stream.bytesAvailable < 2) {
                    throw new Error("ts: demux af ltw_flag failed.");
                }
                ltw_offset = stream.readUnsignedShort();
                
                ltw_valid_flag = (ltw_offset >> 15) &0x01;
                ltw_offset &= 0x7FFF;
            }
            
            if (piecewise_rate_flag) {
                if (stream.bytesAvailable < 3) {
                    throw new Error("ts: demux af piecewise_rate_flag failed.");
                }
                // skip -1 to read 4B for 3B actually
                stream.position -= 1;
                piecewise_rate = (stream.readUnsignedInt() & 0x00ffffff);
                
                piecewise_rate &= 0x3FFFFF;
            }
            
            if (seamless_splice_flag) {
                if (stream.bytesAvailable < 5) {
                    throw new Error("ts: demux af seamless_splice_flag failed");
                }
                marker_bit0 = stream.readUnsignedByte();
                DTS_next_AU1 = stream.readUnsignedShort();
                DTS_next_AU2 = stream.readUnsignedShort();
                
                splice_type = (marker_bit0 >> 4) & 0x0F;
                DTS_next_AU0 = (marker_bit0 >> 1) & 0x07;
                marker_bit0 &= 0x01;
                
                marker_bit1 = DTS_next_AU1 & 0x01;
                DTS_next_AU1 = (DTS_next_AU1 >> 1) & 0x7FFF;
                
                marker_bit2 = DTS_next_AU2 & 0x01;
                DTS_next_AU2 = (DTS_next_AU2 >> 1) & 0x7FFF;
            }
            
            nb_af_ext_reserved = adaptation_field_extension_length - (stream.position - pos_af_ext);
            stream.position += nb_af_ext_reserved;
        }
        
        nb_af_reserved = adaption_field_length - (stream.position - pos_af);
        stream.position += nb_af_reserved;
    }
};

/**
 * 2.4.4.4 Table_id assignments, hls-mpeg-ts-iso13818-1.pdf, page 62
 * The table_id field identifies the contents of a Transport Stream PSI section as shown in Table 2-26.
 */
class SrsTsPsiId extends SrsEnum
{
    public function SrsTsPsiId(v:int)
    {
        super(v);
    }
    public static function parse(v:int):SrsTsPsiId
    {
        switch (v) {
            case 0x00: return SrsTsPsiId.Pas;
            case 0x01: return SrsTsPsiId.Cas;
            case 0x02: return SrsTsPsiId.Pms;
            case 0x03: return SrsTsPsiId.Ds;
            case 0x04: return SrsTsPsiId.Sds;
            case 0x05: return SrsTsPsiId.Ods;
            case 0x06: return SrsTsPsiId.Iso138181Start;
            case 0x37: return SrsTsPsiId.Iso138181End;
            case 0x38: return SrsTsPsiId.Iso138186Start;
            case 0x3F: return SrsTsPsiId.Iso138186End;
            case 0x40: return SrsTsPsiId.UserStart;
            case 0xFE: return SrsTsPsiId.UserEnd;
            case 0xFF: return SrsTsPsiId.Forbidden;
            default: case 0x00: return SrsTsPsiId.Forbidden;
        }
    }
    
    // program_association_section
    public static const Pas:SrsTsPsiId = new SrsTsPsiId(0x00);
    // conditional_access_section (CA_section)
    public static const Cas:SrsTsPsiId = new SrsTsPsiId(0x01);
    // TS_program_map_section
    public static const Pms:SrsTsPsiId = new SrsTsPsiId(0x02);
    // TS_description_section
    public static const Ds:SrsTsPsiId = new SrsTsPsiId(0x03);
    // ISO_IEC_14496_scene_description_section
    public static const Sds:SrsTsPsiId = new SrsTsPsiId(0x04);
    // ISO_IEC_14496_object_descriptor_section
    public static const Ods:SrsTsPsiId = new SrsTsPsiId(0x05);
    // ITU-T Rec. H.222.0 | ISO/IEC 13818-1 reserved
    public static const Iso138181Start:SrsTsPsiId = new SrsTsPsiId(0x06);
    public static const Iso138181End:SrsTsPsiId = new SrsTsPsiId(0x37);
    // Defined in ISO/IEC 13818-6
    public static const Iso138186Start:SrsTsPsiId = new SrsTsPsiId(0x38);
    public static const Iso138186End:SrsTsPsiId = new SrsTsPsiId(0x3F);
    // User private
    public static const UserStart:SrsTsPsiId = new SrsTsPsiId(0x40);
    public static const UserEnd:SrsTsPsiId = new SrsTsPsiId(0xFE);
    // forbidden
    public static const Forbidden:SrsTsPsiId = new SrsTsPsiId(0xFF);
};

/**
 * the payload of ts packet, can be PES or PSI payload.
 */
class SrsTsPayload
{
    protected var packet:SrsTsPacket;
    
    public function SrsTsPayload(p:SrsTsPacket)
    {
        packet = p;
    }
    
    public function decode(stream:ByteArray):SrsTsMessage
    {
        throw new Error("not implements");
    }
};

/**
 * the PES payload of ts packet.
 * 2.4.3.6 PES packet, hls-mpeg-ts-iso13818-1.pdf, page 49
 */
class SrsTsPayloadPES extends SrsTsPayload
{
    // 3B
    /**
     * The packet_start_code_prefix is a 24-bit code. Together with the stream_id that follows it
     * constitutes a packet start code that identifies the beginning of a packet. The packet_start_code_prefix is the bit string
     * '0000 0000 0000 0000 0000 0001' (0x000001).
     */
    public var packet_start_code_prefix:int; //24bits
    // 1B
    /**
     * In Program Streams, the stream_id specifies the type and number of the elementary stream as defined by the
     * stream_id Table 2-18. In Transport Streams, the stream_id may be set to any valid value which correctly describes the
     * elementary stream type as defined in Table 2-18. In Transport Streams, the elementary stream type is specified in the
     * Program Specific Information as specified in 2.4.4.
     */
    // @see SrsTsPESStreamId, value can be SrsTsPESStreamIdAudioCommon or SrsTsPESStreamIdVideoCommon.
    public var stream_id:int; //8bits
    // 2B
    /**
     * A 16-bit field specifying the number of bytes in the PES packet following the last byte of the
     * field. A value of 0 indicates that the PES packet length is neither specified nor bounded and is allowed only in
     * PES packets whose payload consists of bytes from a video elementary stream contained in Transport Stream packets.
     */
    public var PES_packet_length:int; //16bits
    
    // 1B
    /**
     * 2bits const '10'
     */
    public var const2bits:int; //2bits
    /**
     * The 2-bit PES_scrambling_control field indicates the scrambling mode of the PES packet
     * payload. When scrambling is performed at the PES level, the PES packet header, including the optional fields when
     * present, shall not be scrambled (see Table 2-19).
     */
    public var PES_scrambling_control:int; //2bits
    /**
     * This is a 1-bit field indicating the priority of the payload in this PES packet. A '1' indicates a higher
     * priority of the payload of the PES packet payload than a PES packet payload with this field set to '0'. A multiplexor can
     * use the PES_priority bit to prioritize its data within an elementary stream. This field shall not be changed by the transport
     * mechanism.
     */
    public var PES_priority:int; //1bit
    /**
     * This is a 1-bit flag. When set to a value of '1' it indicates that the PES packet header is
     * immediately followed by the video start code or audio syncword indicated in the data_stream_alignment_descriptor
     * in 2.6.10 if this descriptor is present. If set to a value of '1' and the descriptor is not present, alignment as indicated in
     * alignment_type '01' in Table 2-47 and Table 2-48 is required. When set to a value of '0' it is not defined whether any such
     * alignment occurs or not.
     */
    public var data_alignment_indicator:int; //1bit
    /**
     * This is a 1-bit field. When set to '1' it indicates that the material of the associated PES packet payload is
     * protected by copyright. When set to '0' it is not defined whether the material is protected by copyright. A copyright
     * descriptor described in 2.6.24 is associated with the elementary stream which contains this PES packet and the copyright
     * flag is set to '1' if the descriptor applies to the material contained in this PES packet
     */
    public var copyright:int; //1bit
    /**
     * This is a 1-bit field. When set to '1' the contents of the associated PES packet payload is an original.
     * When set to '0' it indicates that the contents of the associated PES packet payload is a copy.
     */
    public var original_or_copy:int; //1bit
    
    // 1B
    /**
     * This is a 2-bit field. When the PTS_DTS_flags field is set to '10', the PTS fields shall be present in
     * the PES packet header. When the PTS_DTS_flags field is set to '11', both the PTS fields and DTS fields shall be present
     * in the PES packet header. When the PTS_DTS_flags field is set to '00' no PTS or DTS fields shall be present in the PES
     * packet header. The value '01' is forbidden.
     */
    public var PTS_DTS_flags:int; //2bits
    /**
     * A 1-bit flag, which when set to '1' indicates that ESCR base and extension fields are present in the PES
     * packet header. When set to '0' it indicates that no ESCR fields are present.
     */
    public var ESCR_flag:int; //1bit
    /**
     * A 1-bit flag, which when set to '1' indicates that the ES_rate field is present in the PES packet header.
     * When set to '0' it indicates that no ES_rate field is present.
     */
    public var ES_rate_flag:int; //1bit
    /**
     * A 1-bit flag, which when set to '1' it indicates the presence of an 8-bit trick mode field. When
     * set to '0' it indicates that this field is not present.
     */
    public var DSM_trick_mode_flag:int; //1bit
    /**
     * A 1-bit flag, which when set to '1' indicates the presence of the additional_copy_info field.
     * When set to '0' it indicates that this field is not present.
     */
    public var additional_copy_info_flag:int; //1bit
    /**
     * A 1-bit flag, which when set to '1' indicates that a CRC field is present in the PES packet. When set to
     * '0' it indicates that this field is not present.
     */
    public var PES_CRC_flag:int; //1bit
    /**
     * A 1-bit flag, which when set to '1' indicates that an extension field exists in this PES packet
     * header. When set to '0' it indicates that this field is not present.
     */
    public var PES_extension_flag:int; //1bit
    
    // 1B
    /**
     * An 8-bit field specifying the total number of bytes occupied by the optional fields and any
     * stuffing bytes contained in this PES packet header. The presence of optional fields is indicated in the byte that precedes
     * the PES_header_data_length field.
     */
    public var PES_header_data_length:int; //8bits
    
    // 5B
    /**
     * Presentation times shall be related to decoding times as follows: The PTS is a 33-bit
     * number coded in three separate fields. It indicates the time of presentation, tp n (k), in the system target decoder of a
     * presentation unit k of elementary stream n. The value of PTS is specified in units of the period of the system clock
     * frequency divided by 300 (yielding 90 kHz). The presentation time is derived from the PTS according to equation 2-11
     * below. Refer to 2.7.4 for constraints on the frequency of coding presentation timestamps.
     */
    // ===========1B
    // 4bits const
    // 3bits PTS [32..30]
    // 1bit const '1'
    // ===========2B
    // 15bits PTS [29..15]
    // 1bit const '1'
    // ===========2B
    // 15bits PTS [14..0]
    // 1bit const '1'
    public var pts:Number; // 33bits
    
    // 5B
    /**
     * The DTS is a 33-bit number coded in three separate fields. It indicates the decoding time,
     * td n (j), in the system target decoder of an access unit j of elementary stream n. The value of DTS is specified in units of
     * the period of the system clock frequency divided by 300 (yielding 90 kHz).
     */
    // ===========1B
    // 4bits const
    // 3bits DTS [32..30]
    // 1bit const '1'
    // ===========2B
    // 15bits DTS [29..15]
    // 1bit const '1'
    // ===========2B
    // 15bits DTS [14..0]
    // 1bit const '1'
    public var dts:Number; // 33bits
    
    // 6B
    /**
     * The elementary stream clock reference is a 42-bit field coded in two parts. The first
     * part, ESCR_base, is a 33-bit field whose value is given by ESCR_base(i), as given in equation 2-14. The second part,
     * ESCR_ext, is a 9-bit field whose value is given by ESCR_ext(i), as given in equation 2-15. The ESCR field indicates the
     * intended time of arrival of the byte containing the last bit of the ESCR_base at the input of the PES-STD for PES streams
     * (refer to 2.5.2.4).
     */
    // 2bits reserved
    // 3bits ESCR_base[32..30]
    // 1bit const '1'
    // 15bits ESCR_base[29..15]
    // 1bit const '1'
    // 15bits ESCR_base[14..0]
    // 1bit const '1'
    // 9bits ESCR_extension
    // 1bit const '1'
    public var ESCR_base:Number; //33bits
    public var ESCR_extension:int; //9bits
    
    // 3B
    /**
     * The ES_rate field is a 22-bit unsigned integer specifying the rate at which the
     * system target decoder receives bytes of the PES packet in the case of a PES stream. The ES_rate is valid in the PES
     * packet in which it is included and in subsequent PES packets of the same PES stream until a new ES_rate field is
     * encountered. The value of the ES_rate is measured in units of 50 bytes/second. The value 0 is forbidden. The value of the
     * ES_rate is used to define the time of arrival of bytes at the input of a P-STD for PES streams defined in 2.5.2.4. The
     * value encoded in the ES_rate field may vary from PES_packet to PES_packet.
     */
    // 1bit const '1'
    // 22bits ES_rate
    // 1bit const '1'
    public var ES_rate:int; //22bits
    
    // 1B
    /**
     * A 3-bit field that indicates which trick mode is applied to the associated video stream. In cases of
     * other types of elementary streams, the meanings of this field and those defined by the following five bits are undefined.
     * For the definition of trick_mode status, refer to the trick mode section of 2.4.2.3.
     */
    public var trick_mode_control:int; //3bits
    public var trick_mode_value:int; //5bits
    
    // 1B
    // 1bit const '1'
    /**
     * This 7-bit field contains private data relating to copyright information.
     */
    public var additional_copy_info:int; //7bits
    
    // 2B
    /**
     * The previous_PES_packet_CRC is a 16-bit field that contains the CRC value that yields
     * a zero output of the 16 registers in the decoder similar to the one defined in Annex A,
     */
    public var previous_PES_packet_CRC:int; //16bits
    
    // 1B
    /**
     * A 1-bit flag which when set to '1' indicates that the PES packet header contains private data.
     * When set to a value of '0' it indicates that private data is not present in the PES header.
     */
    public var PES_private_data_flag:int; //1bit
    /**
     * A 1-bit flag which when set to '1' indicates that an ISO/IEC 11172-1 pack header or a
     * Program Stream pack header is stored in this PES packet header. If this field is in a PES packet that is contained in a
     * Program Stream, then this field shall be set to '0'. In a Transport Stream, when set to the value '0' it indicates that no pack
     * header is present in the PES header.
     */
    public var pack_header_field_flag:int; //1bit
    /**
     * A 1-bit flag which when set to '1' indicates that the
     * program_packet_sequence_counter, MPEG1_MPEG2_identifier, and original_stuff_length fields are present in this
     * PES packet. When set to a value of '0' it indicates that these fields are not present in the PES header.
     */
    public var program_packet_sequence_counter_flag:int; //1bit
    /**
     * A 1-bit flag which when set to '1' indicates that the P-STD_buffer_scale and P-STD_buffer_size
     * are present in the PES packet header. When set to a value of '0' it indicates that these fields are not present in the
     * PES header.
     */
    public var P_STD_buffer_flag:int; //1bit
    /**
     * reverved value, must be '1'
     */
    public var const1_value0:int; //3bits
    /**
     * A 1-bit field which when set to '1' indicates the presence of the PES_extension_field_length
     * field and associated fields. When set to a value of '0' this indicates that the PES_extension_field_length field and any
     * associated fields are not present.
     */
    public var PES_extension_flag_2:int; //1bit
    
    // 16B
    /**
     * This is a 16-byte field which contains private data. This data, combined with the fields before and
     * after, shall not emulate the packet_start_code_prefix (0x000001).
     */
    public var PES_private_data:ByteArray; //128bits
    
    // (1+x)B
    /**
     * This is an 8-bit field which indicates the length, in bytes, of the pack_header_field().
     */
    public var pack_field_length:int; //8bits
    public var pack_field:ByteArray; //[pack_field_length] bytes
    
    // 2B
    // 1bit const '1'
    /**
     * The program_packet_sequence_counter field is a 7-bit field. It is an optional
     * counter that increments with each successive PES packet from a Program Stream or from an ISO/IEC 11172-1 Stream or
     * the PES packets associated with a single program definition in a Transport Stream, providing functionality similar to a
     * continuity counter (refer to 2.4.3.2). This allows an application to retrieve the original PES packet sequence of a Program
     * Stream or the original packet sequence of the original ISO/IEC 11172-1 stream. The counter will wrap around to 0 after
     * its maximum value. Repetition of PES packets shall not occur. Consequently, no two consecutive PES packets in the
     * program multiplex shall have identical program_packet_sequence_counter values.
     */
    public var program_packet_sequence_counter:int; //7bits
    // 1bit const '1'
    /**
     * A 1-bit flag which when set to '1' indicates that this PES packet carries information from
     * an ISO/IEC 11172-1 stream. When set to '0' it indicates that this PES packet carries information from a Program Stream.
     */
    public var MPEG1_MPEG2_identifier:int; //1bit
    /**
     * This 6-bit field specifies the number of stuffing bytes used in the original ITU-T
     * Rec. H.222.0 | ISO/IEC 13818-1 PES packet header or in the original ISO/IEC 11172-1 packet header.
     */
    public var original_stuff_length:int; //6bits
    
    // 2B
    // 2bits const '01'
    /**
     * The P-STD_buffer_scale is a 1-bit field, the meaning of which is only defined if this PES packet
     * is contained in a Program Stream. It indicates the scaling factor used to interpret the subsequent P-STD_buffer_size field.
     * If the preceding stream_id indicates an audio stream, P-STD_buffer_scale shall have the value '0'. If the preceding
     * stream_id indicates a video stream, P-STD_buffer_scale shall have the value '1'. For all other stream types, the value
     * may be either '1' or '0'.
     */
    public var P_STD_buffer_scale:int; //1bit
    /**
     * The P-STD_buffer_size is a 13-bit unsigned integer, the meaning of which is only defined if this
     * PES packet is contained in a Program Stream. It defines the size of the input buffer, BS n , in the P-STD. If
     * P-STD_buffer_scale has the value '0', then the P-STD_buffer_size measures the buffer size in units of 128 bytes. If
     * P-STD_buffer_scale has the value '1', then the P-STD_buffer_size measures the buffer size in units of 1024 bytes.
     */
    public var P_STD_buffer_size:int; //13bits
    
    // (1+x)B
    // 1bit const '1'
    /**
     * This is a 7-bit field which specifies the length, in bytes, of the data following this field in
     * the PES extension field up to and including any reserved bytes.
     */
    public var PES_extension_field_length:int; //7bits
    public var PES_extension_field:ByteArray; //[PES_extension_field_length] bytes
    
    // NB
    /**
     * This is a fixed 8-bit value equal to '1111 1111' that can be inserted by the encoder, for example to meet
     * the requirements of the channel. It is discarded by the decoder. No more than 32 stuffing bytes shall be present in one
     * PES packet header.
     */
    public var nb_stuffings:int;
    
    // NB
    /**
     * PES_packet_data_bytes shall be contiguous bytes of data from the elementary stream
     * indicated by the packet’s stream_id or PID. When the elementary stream data conforms to ITU-T
     * Rec. H.262 | ISO/IEC 13818-2 or ISO/IEC 13818-3, the PES_packet_data_bytes shall be byte aligned to the bytes of this
     * Recommendation | International Standard. The byte-order of the elementary stream shall be preserved. The number of
     * PES_packet_data_bytes, N, is specified by the PES_packet_length field. N shall be equal to the value indicated in the
     * PES_packet_length minus the number of bytes between the last byte of the PES_packet_length field and the first
     * PES_packet_data_byte.
     *
     * In the case of a private_stream_1, private_stream_2, ECM_stream, or EMM_stream, the contents of the
     * PES_packet_data_byte field are user definable and will not be specified by ITU-T | ISO/IEC in the future.
     */
    public var nb_bytes:int;
    
    // NB
    /**
     * This is a fixed 8-bit value equal to '1111 1111'. It is discarded by the decoder.
     */
    public var nb_paddings:int;
	
	private var _log:ILogger = new TraceLogger("HLS");
    
    public function SrsTsPayloadPES(p:SrsTsPacket)
    {
        super(p);
        
        PES_private_data = null;
        pack_field = null;
        PES_extension_field = null;
        nb_stuffings = 0;
        nb_bytes = 0;
        nb_paddings = 0;
        const2bits = 0x02;
        const1_value0 = 0x07;
    }
    
    override public function decode(stream:ByteArray):SrsTsMessage
    {
        // find the channel from chunk.
        var channel:SrsTsChannel = packet.context.getChannel(packet.pid);
        if (!channel) {
            throw new Error("ts: demux PES no channel, pid=" + packet.pid);
        }
        
        // init msg.
        var msg:SrsTsMessage = channel.msg;
        if (!msg) {
            msg = new SrsTsMessage(channel, packet);
            channel.msg = msg;
        }
		
		// we must cache the fresh state of msg,
		// for the PES_packet_length is 0, the first payload_unit_start_indicator always 1,
		// so should check for the fresh and not completed it.
		var is_fresh_msg:Boolean = msg.fresh();
        
        // check when fresh, the payload_unit_start_indicator
        // should be 1 for the fresh msg.
        if (is_fresh_msg && !packet.payload_unit_start_indicator) {
			_log.error("ts: PES fresh packet length={0}, us={1}, cc={2}", 
				msg.PES_packet_length, packet.payload_unit_start_indicator, packet.continuity_counter);
			throw new Error("ts: PES fresh packet invalid.");
        }
        
        // check when not fresh and PES_packet_length>0,
        // the payload_unit_start_indicator should never be 1 when not completed.
        if (!is_fresh_msg && msg.PES_packet_length > 0
            && !msg.completed(packet.payload_unit_start_indicator)
            && packet.payload_unit_start_indicator
        ) {
			_log.error("ts: PES packet length={0}, us={1}, cc={2}",
				msg.PES_packet_length, packet.payload_unit_start_indicator, packet.continuity_counter);
			throw new Error("ts: PES packet length invalid.");
            
            // reparse current msg.
            stream.position = 0;
            channel.msg = null;
            return null;
        }
        
        // check the continuity counter
        if (!is_fresh_msg) {
            // late-incoming or duplicated continuity, drop message.
            // @remark check overflow, the counter plus 1 should greater when invalid.
            if (msg.continuity_counter >= packet.continuity_counter
                && ((msg.continuity_counter + 1) & 0x0f) > packet.continuity_counter
            ) {
               _log.warn("ts: drop PES {0}B for duplicated cc={1}", 
				   stream.length, msg.continuity_counter);
                stream.position = 0;
                return null;
            }
            
            // when got partially message, the continous count must be continuous, or drop it.
            if (((msg.continuity_counter + 1) & 0x0f) != packet.continuity_counter) {
				_log.error("ts: continuity must be continous, msg={0}, packet={1}",
					msg.continuity_counter, packet.continuity_counter);
				throw new Error("ts: continuity must be continous.");
                
                // reparse current msg.
                stream.position = 0;
                channel.msg = null;
                return null;
            }
        }
        msg.continuity_counter = packet.continuity_counter;
        
        // for the PES_packet_length(0), reap when completed.
        if (!is_fresh_msg && msg.completed(packet.payload_unit_start_indicator)) {
            // reap previous PES packet.
            channel.msg = null;
            
            // reparse current msg.
            stream.position = 0;
            return msg;
        }
        
        // contious packet, append bytes for unit start is 0
        if (!packet.payload_unit_start_indicator) {
            nb_bytes = msg.dump(stream);
        }
        
        // when unit start, parse the fresh msg.
        if (packet.payload_unit_start_indicator) {
            // 6B fixed header.
            if (stream.bytesAvailable < 6) {
                throw new Error("ts: demux PSE failed.");
            }
            // 3B
            stream.position -= 1;
            packet_start_code_prefix = (stream.readUnsignedInt()) & 0x00ffffff;
            // 1B
            stream_id = stream.readUnsignedByte();
            // 2B
            PES_packet_length = stream.readUnsignedShort();
            
            // check the packet start prefix.
            packet_start_code_prefix &= 0xFFFFFF;
            if (packet_start_code_prefix != 0x01) {
				_log.error("ts: demux PES start code failed, expect=0x01, actual={0}",
					packet_start_code_prefix);
				throw new Error("ts: demux PES start code failed.");
            }
            var pos_packet:uint = stream.position;
            
            // @remark the sid indicates the elementary stream format.
            //      the SrsTsPESStreamIdAudio and SrsTsPESStreamIdVideo is start by 0b110 or 0b1110
            var sid:SrsTsPESStreamId = SrsTsPESStreamId.parse(stream_id);
            msg.sid = sid;
            
            if (sid.notEquals(SrsTsPESStreamId.ProgramStreamMap)
                && sid.notEquals(SrsTsPESStreamId.PaddingStream)
                && sid.notEquals(SrsTsPESStreamId.PrivateStream2)
                && sid.notEquals(SrsTsPESStreamId.EcmStream)
                && sid.notEquals(SrsTsPESStreamId.EmmStream)
                && sid.notEquals(SrsTsPESStreamId.ProgramStreamDirectory)
                && sid.notEquals(SrsTsPESStreamId.DsmccStream)
                && sid.notEquals(SrsTsPESStreamId.H2221TypeE)
            ) {
                // 3B flags.
                if (stream.bytesAvailable < 3) {
                    throw new Error("ts: demux PSE flags failed.");
                }
                // 1B
                var oocv:int = stream.readUnsignedByte();
                // 1B
                var pefv:int = stream.readUnsignedByte();
                // 1B
                PES_header_data_length = stream.readUnsignedByte();
                // position of header start.
                var pos_header:uint = stream.position;
                
                const2bits = (oocv >> 6) & 0x03;
                PES_scrambling_control = (oocv >> 4) & 0x03;
                PES_priority = (oocv >> 3) & 0x01;
                data_alignment_indicator = (oocv >> 2) & 0x01;
                copyright = (oocv >> 1) & 0x01;
                original_or_copy = oocv & 0x01;
                
                PTS_DTS_flags = (pefv >> 6) & 0x03;
                ESCR_flag = (pefv >> 5) & 0x01;
                ES_rate_flag = (pefv >> 4) & 0x01;
                DSM_trick_mode_flag = (pefv >> 3) & 0x01;
                additional_copy_info_flag = (pefv >> 2) & 0x01;
                PES_CRC_flag = (pefv >> 1) & 0x01;
                PES_extension_flag = pefv & 0x01;
                
                // check required together.
                var nb_required:int = 0;
                nb_required += (PTS_DTS_flags == 0x2)? 5:0;
                nb_required += (PTS_DTS_flags == 0x3)? 10:0;
                nb_required += ESCR_flag? 6:0;
                nb_required += ES_rate_flag? 3:0;
                nb_required += DSM_trick_mode_flag? 1:0;
                nb_required += additional_copy_info_flag? 1:0;
                nb_required += PES_CRC_flag? 2:0;
                nb_required += PES_extension_flag? 1:0;
                if (stream.bytesAvailable < nb_required) {
                    throw new Error("ts: demux PSE payload failed.");
                }
                
                // 5B
                if (PTS_DTS_flags == 0x2) {
                    pts = decode_33bits_dts_pts(stream);
                    dts = pts;
                    
                    // update the dts and pts of message.
                    msg.dts = dts;
                    msg.pts = pts;
                }
                
                // 10B
                if (PTS_DTS_flags == 0x3) {
                    pts = decode_33bits_dts_pts(stream);
                    dts = decode_33bits_dts_pts(stream);
                    
                    // check sync, the diff of dts and pts should never greater than 1s.
                    if (dts - pts > 90000 || pts - dts > 90000) {
						_log.warn("ts: sync dts={0}, pts={1}", dts, pts);
                    }
                    
                    // update the dts and pts of message.
                    msg.dts = dts;
                    msg.pts = pts;
                }
                
                // 6B
                if (ESCR_flag) {
                    ESCR_extension = 0;
                    ESCR_base = 0;
                    
                    stream.position += 6;
                    _log.warn("ts: demux PES, ignore the escr.");
                }
                
                // 3B
                if (ES_rate_flag) {
                    // skip -1 to read 4bytes for the ES_rate is 3B.
                    stream.position -= 1;
                    ES_rate = (stream.readUnsignedInt() & 0x00ffffff);
                    
                    ES_rate = ES_rate >> 1;
                    ES_rate &= 0x3FFFFF;
                }
                
                // 1B
                if (DSM_trick_mode_flag) {
                    trick_mode_control = stream.readUnsignedByte();
                    
                    trick_mode_value = trick_mode_control & 0x1f;
                    trick_mode_control = (trick_mode_control >> 5) & 0x03;
                }
                
                // 1B
                if (additional_copy_info_flag) {
                    additional_copy_info = stream.readUnsignedByte();
                    
                    additional_copy_info &= 0x7f;
                }
                
                // 2B
                if (PES_CRC_flag) {
                    previous_PES_packet_CRC = stream.readUnsignedShort();
                }
                
                // 1B
                if (PES_extension_flag) {
                    var efv:int = stream.readUnsignedByte();
                    
                    PES_private_data_flag = (efv >> 7) & 0x01;
                    pack_header_field_flag = (efv >> 6) & 0x01;
                    program_packet_sequence_counter_flag = (efv >> 5) & 0x01;
                    P_STD_buffer_flag = (efv >> 4) & 0x01;
                    const1_value0 = (efv >> 1) & 0x07;
                    PES_extension_flag_2 = efv & 0x01;
                    
                    nb_required = 0;
                    nb_required += PES_private_data_flag? 16:0;
                    nb_required += pack_header_field_flag? 1:0; // 1+x bytes.
                    nb_required += program_packet_sequence_counter_flag? 2:0;
                    nb_required += P_STD_buffer_flag? 2:0;
                    nb_required += PES_extension_flag_2? 1:0; // 1+x bytes.
                    if (stream.bytesAvailable < nb_required) {
                        throw new Error("ts: demux PSE ext payload failed.");
                    }
                    
                    // 16B
                    if (PES_private_data_flag) {
                        PES_private_data = new ByteArray();
                        stream.readBytes(PES_private_data, 0, 16);
                    }
                    
                    // (1+x)B
                    if (pack_header_field_flag) {
                        pack_field_length = stream.readUnsignedByte();
                        if (pack_field_length > 0) {
                            // the adjust required bytes.
                            nb_required = nb_required - 16 - 1 + pack_field_length;
                            if (stream.bytesAvailable < nb_required) {
                                throw new Error("ts: demux PSE ext pack failed.");
                            }
                            pack_field = new ByteArray();
                            stream.readBytes(pack_field, 0, pack_field_length);
                        }
                    }
                    
                    // 2B
                    if (program_packet_sequence_counter_flag) {
                        program_packet_sequence_counter = stream.readUnsignedByte();
                        program_packet_sequence_counter &= 0x7f;
                        
                        original_stuff_length = stream.readUnsignedByte();
                        MPEG1_MPEG2_identifier = (original_stuff_length >> 6) & 0x01;
                        original_stuff_length &= 0x3f;
                    }
                    
                    // 2B
                    if (P_STD_buffer_flag) {
                        P_STD_buffer_size = stream.readUnsignedShort();
                        
                        // '01'
                        //int8_t const2bits = (P_STD_buffer_scale >>14) & 0x03;
                        
                        P_STD_buffer_scale = (P_STD_buffer_scale >>13) & 0x01;
                        P_STD_buffer_size &= 0x1FFF;
                    }
                    
                    // (1+x)B
                    if (PES_extension_flag_2) {
                        PES_extension_field_length = stream.readUnsignedByte();
                        PES_extension_field_length &= 0x07;
                        
                        if (PES_extension_field_length > 0) {
                            if (stream.bytesAvailable < PES_extension_field_length) {
                                throw new Error("ts: demux PSE ext field failed.");
                            }
                            PES_extension_field = new ByteArray();
                            stream.readBytes(PES_extension_field, 0, PES_extension_field_length);
                        }
                    }
                }
                
                // stuffing_byte
                nb_stuffings = PES_header_data_length - (stream.position - pos_header);
                if (nb_stuffings > 0) {
                    if (stream.bytesAvailable < nb_stuffings) {
                        throw new Error("ts: demux PSE stuffings failed.");
                    }
                    stream.position += nb_stuffings;
                }
                
                // PES_packet_data_byte, page58.
                // the packet size contains the header size.
                // The number of PES_packet_data_bytes, N, is specified by the
                // PES_packet_length field. N shall be equal to the value
                // indicated in the PES_packet_length minus the number of bytes
                // between the last byte of the PES_packet_length field and the
                // first PES_packet_data_byte.
                /**
                 * when actual packet length > 0xffff(65535),
                 * which exceed the max u_int16_t packet length,
                 * use 0 packet length, the next unit start indicates the end of packet.
                 */
                if (PES_packet_length > 0) {
                    var nb_packet:uint = PES_packet_length - (stream.position - pos_packet);
                    msg.PES_packet_length = (uint)(Math.max(0, nb_packet));
                }
                
                // xB
                nb_bytes = msg.dump(stream);
            } else if (sid.equals(SrsTsPESStreamId.ProgramStreamMap)
                || sid.equals(SrsTsPESStreamId.PrivateStream2)
                || sid.equals(SrsTsPESStreamId.EcmStream)
                || sid.equals(SrsTsPESStreamId.EmmStream)
                || sid.equals(SrsTsPESStreamId.ProgramStreamDirectory)
                || sid.equals(SrsTsPESStreamId.DsmccStream)
                || sid.equals(SrsTsPESStreamId.H2221TypeE)
            ) {
                // for (i = 0; i < PES_packet_length; i++) {
                //         PES_packet_data_byte
                // }
                
                // xB
                nb_bytes = msg.dump(stream);
            } else if (sid.equals(SrsTsPESStreamId.PaddingStream)) {
                // for (i = 0; i < PES_packet_length; i++) {
                //         padding_byte
                // }
                nb_paddings = stream.length - stream.position;
                stream.position = stream.length;
                //info("ts: drop %dB padding bytes", nb_paddings);
            } else {
                var nb_drop:int = stream.length - stream.position;
                stream.position = stream.length;
                _log.warn("ts: drop the pes packet {0}B for stream_id={1}", nb_drop, stream_id);
            }
        }
		
		// when fresh and the PES_packet_length is 0,
		// the payload_unit_start_indicator always be 1,
		// the message should never EOF for the first packet.
		if (is_fresh_msg && msg.PES_packet_length == 0) {
			return null;
		}
        
        // check msg, reap when completed.
        if (msg.completed(packet.payload_unit_start_indicator)) {
            channel.msg = null;
            //info("ts: reap msg for completed.");
            return msg;
        }
        
        return null;
    }
    
    private function decode_33bits_dts_pts(stream:ByteArray):Number
    {
        if (stream.bytesAvailable < 5) {
            throw new Error("ts: demux PSE dts/pts failed");
        }
        
        // decode the 33bits schema.
        // ===========1B
        // 4bits const maybe '0001', '0010' or '0011'.
        // 3bits DTS/PTS [32..30]
        // 1bit const '1'
        var dts_pts_30_32:uint = stream.readUnsignedByte();
        if ((dts_pts_30_32 & 0x01) != 0x01) {
            throw new Error("ts: demux PSE dts/pts 30-32 failed.");
        }
        // @remark, we donot check the high 4bits, maybe '0001', '0010' or '0011'.
        //      so we just ensure the high 4bits is not 0x00.
        if (((dts_pts_30_32 >> 4) & 0x0f) == 0x00) {
            throw new Error("ts: demux PSE dts/pts 30-32 failed.");
        }
        dts_pts_30_32 = (dts_pts_30_32 >> 1) & 0x07;
        
        // ===========2B
        // 15bits DTS/PTS [29..15]
        // 1bit const '1'
        var dts_pts_15_29:uint = stream.readUnsignedShort();
        if ((dts_pts_15_29 & 0x01) != 0x01) {
            throw new Error("ts: demux PSE dts/pts 15-29 failed.");
        }
        dts_pts_15_29 = (dts_pts_15_29 >> 1) & 0x7fff;
        
        // ===========2B
        // 15bits DTS/PTS [14..0]
        // 1bit const '1'
        var dts_pts_0_14:uint = stream.readUnsignedShort();
        if ((dts_pts_0_14 & 0x01) != 0x01) {
            throw new Error("ts: demux PSE dts/pts 0-14 failed.");
        }
        dts_pts_0_14 = (dts_pts_0_14 >> 1) & 0x7fff;
        
        var v:uint = 0x00;
        v += (dts_pts_30_32 << 30) & 0x1c0000000;
        v += (dts_pts_15_29 << 15) & 0x3fff8000;
        v += dts_pts_0_14 & 0x7fff;
        return v;
    }
};

/**
 * the PSI payload of ts packet.
 * 2.4.4 Program specific information, hls-mpeg-ts-iso13818-1.pdf, page 59
 */
class SrsTsPayloadPSI extends SrsTsPayload
{
    // 1B
    /**
     * This is an 8-bit field whose value shall be the number of bytes, immediately following the pointer_field
     * until the first byte of the first section that is present in the payload of the Transport Stream packet (so a value of 0x00 in
     * the pointer_field indicates that the section starts immediately after the pointer_field). When at least one section begins in
     * a given Transport Stream packet, then the payload_unit_start_indicator (refer to 2.4.3.2) shall be set to 1 and the first
     * byte of the payload of that Transport Stream packet shall contain the pointer. When no section begins in a given
     * Transport Stream packet, then the payload_unit_start_indicator shall be set to 0 and no pointer shall be sent in the
     * payload of that packet.
     */
    public var pointer_field:int;
    // 1B
    /**
     * This is an 8-bit field, which shall be set to 0x00 as shown in Table 2-26.
     */
    public var table_id:SrsTsPsiId; //8bits
    
    // 2B
    /**
     * The section_syntax_indicator is a 1-bit field which shall be set to '1'.
     */
    public var section_syntax_indicator:int; //1bit
    /**
     * const value, must be '0'
     */
    public var const0_value:int; //1bit
    /**
     * reverved value, must be '1'
     */
    public var const1_value:int; //2bits
    /**
     * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the number
     * of bytes of the section, starting immediately following the section_length field, and including the CRC. The value in this
     * field shall not exceed 1021 (0x3FD).
     */
    public var section_length:int; //12bits
    
    // the specified psi info, for example, PAT fields.
    // 4B
    /**
     * This is a 32-bit field that contains the CRC value that gives a zero output of the registers in the decoder
     * defined in Annex A after processing the entire section.
     * @remark crc32(bytes without pointer field, before crc32 field)
     */
    public var CRC_32:int; //32bits
    
    public function SrsTsPayloadPSI(p:SrsTsPacket)
    {
        super(p);
        
        pointer_field = 0;
        const0_value = 0;
        const1_value = 3;
        CRC_32 = 0;
    }
    
    override public function decode(stream:ByteArray):SrsTsMessage
    {
        /**
         * When the payload of the Transport Stream packet contains PSI data, the payload_unit_start_indicator has the following
         * significance: if the Transport Stream packet carries the first byte of a PSI section, the payload_unit_start_indicator value
         * shall be '1', indicating that the first byte of the payload of this Transport Stream packet carries the pointer_field. If the
         * Transport Stream packet does not carry the first byte of a PSI section, the payload_unit_start_indicator value shall be '0',
         * indicating that there is no pointer_field in the payload. Refer to 2.4.4.1 and 2.4.4.2. This also applies to private streams of
         * stream_type 5 (refer to Table 2-29).
         */
        if (packet.payload_unit_start_indicator) {
            if (stream.bytesAvailable < 1) {
                throw new Error("ts: demux PSI failed.");
            }
            pointer_field = stream.readUnsignedByte();
        }
        
        // to calc the crc32
        var pat_pos:uint = stream.position;
        
        // atleast 3B for all psi.
        if (stream.bytesAvailable < 3) {
            throw new Error("ts: demux PSI failed.");
        }
        // 1B
        table_id = SrsTsPsiId.parse(stream.readUnsignedByte());
        
        // 2B
        var slv:int = stream.readUnsignedShort();
        
        section_syntax_indicator = (slv >> 15) & 0x01;
        const0_value = (slv >> 14) & 0x01;
        const1_value = (slv >> 12) & 0x03;
        section_length = slv & 0x0FFF;
        
        // no section, ignore.
        if (section_length == 0) {
            // "ts: demux PAT ignore empty section"
            return null;
        }
        
        if (stream.bytesAvailable < section_length) {
            throw new Error("ts: demux PAT section failed.");
        }
        
        // call the virtual method of actual PSI.
        psi_decode(stream);
        
        // 4B
        if (stream.bytesAvailable < 4) {
            throw new Error("ts: demux PSI crc32 failed.");
        }
        CRC_32 = stream.readUnsignedInt();
        
        // verify crc32.
        var pos:uint = stream.position;
        
        var crc32Bytes:ByteArray = new ByteArray();
        stream.position = pat_pos;
        stream.readBytes(crc32Bytes, 0, pos - pat_pos - 4);
        stream.position = pos;
        
        var expetCrc32:int = (int)(SrsUtils.srs_crc32(crc32Bytes));
        if (expetCrc32 != CRC_32) {
            throw new Error("ts: verify PSI crc32 failed.");
        }
        stream.position = pos;
        
        // consume left stuffings
        // TODO: FIXME: all stuffing must be 0xff.
        stream.position = stream.length;
        
        return null;
    }
    
    protected function psi_decode(stream:ByteArray):void
    {
        throw new Error("not implements");
    }
};

/**
 * the program of PAT of PSI ts packet.
 */
class SrsTsPayloadPATProgram
{
    // 4B
    /**
     * Program_number is a 16-bit field. It specifies the program to which the program_map_PID is
     * applicable. When set to 0x0000, then the following PID reference shall be the network PID. For all other cases the value
     * of this field is user defined. This field shall not take any single value more than once within one version of the Program
     * Association Table.
     */
    public var number:int; // 16bits
    /**
     * reverved value, must be '1'
     */
    public var const1_value:int; //3bits
    /**
     * program_map_PID/network_PID 13bits
     * network_PID - The network_PID is a 13-bit field, which is used only in conjunction with the value of the
     * program_number set to 0x0000, specifies the PID of the Transport Stream packets which shall contain the Network
     * Information Table. The value of the network_PID field is defined by the user, but shall only take values as specified in
     * Table 2-3. The presence of the network_PID is optional.
     */
    public var pid:int; //13bits
    
    public function SrsTsPayloadPATProgram(n:int, p:int)
    {
        number = n;
        pid = p;
        const1_value = 0x07;
    }
    
    public function decode(stream:ByteArray):void
    {
        // atleast 4B for PAT program specified
        if (stream.bytesAvailable < 4) {
            throw new Error("ts: demux PAT failed.");
        }
        
        var tmpv:int = stream.readUnsignedInt();
        number = ((tmpv >> 16) & 0xFFFF);
        const1_value = ((tmpv >> 13) & 0x07);
        pid = (tmpv & 0x1FFF);
        
        return;
    }
};

/**
 * the PAT payload of PSI ts packet.
 * 2.4.4.3 Program association Table, hls-mpeg-ts-iso13818-1.pdf, page 61
 * The Program Association Table provides the correspondence between a program_number and the PID value of the
 * Transport Stream packets which carry the program definition. The program_number is the numeric label associated with
 * a program.
 */
class SrsTsPayloadPAT extends SrsTsPayloadPSI
{
    // 2B
    /**
     * This is a 16-bit field which serves as a label to identify this Transport Stream from any other
     * multiplex within a network. Its value is defined by the user.
     */
    public var transport_stream_id:int; //16bits
    
    // 1B
    /**
     * reverved value, must be '1'
     */
    public var const3_value:int; //2bits
    /**
     * This 5-bit field is the version number of the whole Program Association Table. The version number
     * shall be incremented by 1 modulo 32 whenever the definition of the Program Association Table changes. When the
     * current_next_indicator is set to '1', then the version_number shall be that of the currently applicable Program Association
     * Table. When the current_next_indicator is set to '0', then the version_number shall be that of the next applicable Program
     * Association Table.
     */
    public var version_number:int; //5bits
    /**
     * A 1-bit indicator, which when set to '1' indicates that the Program Association Table sent is
     * currently applicable. When the bit is set to '0', it indicates that the table sent is not yet applicable and shall be the next
     * table to become valid.
     */
    public var current_next_indicator:int; //1bit
    
    // 1B
    /**
     * This 8-bit field gives the number of this section. The section_number of the first section in the
     * Program Association Table shall be 0x00. It shall be incremented by 1 with each additional section in the Program
     * Association Table.
     */
    public var section_number:int; //8bits
    
    // 1B
    /**
     * This 8-bit field specifies the number of the last section (that is, the section with the highest
     * section_number) of the complete Program Association Table.
     */
    public var last_section_number:int; //8bits
    
    // multiple 4B program data, elem is SrsTsPayloadPATProgram
    public var programs:Array;
    
    public function SrsTsPayloadPAT(p:SrsTsPacket)
    {
        super(p);
        
        const3_value = 3;
        programs = new Array();
    }
    
    override protected function psi_decode(stream:ByteArray):void
    {
        // atleast 5B for PAT specified
        if (stream.bytesAvailable < 5) {
            throw new Error("ts: demux PAT failed.");
        }
        
        var pos:uint = stream.position;
        
        // 2B
        transport_stream_id = stream.readUnsignedShort();
        
        // 1B
        var cniv:int = stream.readUnsignedByte();
        
        const3_value = (cniv >> 6) & 0x03;
        version_number = (cniv >> 1) & 0x1F;
        current_next_indicator = cniv & 0x01;
        
        // TODO: FIXME: check the indicator.
        
        // 1B
        section_number = stream.readUnsignedByte();
        // 1B
        last_section_number = stream.readUnsignedByte();
        
        // multiple 4B program data.
        var program_bytes:int = section_length - 4 - (stream.position - pos);
        for (var i:int = 0; i < program_bytes; i += 4) {
            var program:SrsTsPayloadPATProgram = new SrsTsPayloadPATProgram(0, 0);
            program.decode(stream);
            
            // update the apply pid table.
            packet.context.setChannel(program.pid, SrsTsPidApply.PMT, SrsTsStream.Reserved);
            
            programs.push(program);
        }
        
        // update the apply pid table.
        packet.context.setChannel(packet.pid, SrsTsPidApply.PAT, SrsTsStream.Reserved);
        
        return;
    }
};

/**
 * the esinfo for PMT program.
 */
class SrsTsPayloadPMTESInfo
{
    // 1B
    /**
     * This is an 8-bit field specifying the type of program element carried within the packets with the PID
     * whose value is specified by the elementary_PID. The values of stream_type are specified in Table 2-29.
     */
    public var stream_type:SrsTsStream; //8bits
    
    // 2B
    /**
     * reverved value, must be '1'
     */
    public var const1_value0:int; //3bits
    /**
     * This is a 13-bit field specifying the PID of the Transport Stream packets which carry the associated
     * program element.
     */
    public var elementary_PID:int; //13bits
    
    // (2+x)B
    /**
     * reverved value, must be '1'
     */
    public var const1_value1:int; //4bits
    /**
     * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the number
     * of bytes of the descriptors of the associated program element immediately following the ES_info_length field.
     */
    public var ES_info_length:int; //12bits
    public var ES_info:ByteArray; //[ES_info_length] bytes.
    
    public function SrsTsPayloadPMTESInfo(st:SrsTsStream, epid:int)
    {
        stream_type = st;
        elementary_PID = epid;
        
        const1_value0 = 7;
        const1_value1 = 0x0f;
        ES_info_length = 0;
        ES_info = null;
    }
    
    public function decode(stream:ByteArray):void
    {   
        // 5B
        if (stream.bytesAvailable < 5) {
            throw new Error("ts: demux PMT es info failed.");
        }
        
        stream_type = SrsTsStream.parse(stream.readUnsignedByte());
        
        var epv:int = stream.readUnsignedShort();
        const1_value0 = (epv >> 13) & 0x07;
        elementary_PID = epv & 0x1FFF;
        
        var eilv:int = stream.readUnsignedShort();
        const1_value1 = (epv >> 12) & 0x0f;
        ES_info_length = eilv & 0x0FFF;
        
        if (ES_info_length > 0) {
            if (stream.bytesAvailable < ES_info_length) {
                throw new Error("ts: demux PMT es info data failed.");
            }
            ES_info = new ByteArray();
            stream.readBytes(ES_info, 0, ES_info_length);
        }
    }
};

/**
 * the PMT payload of PSI ts packet.
 * 2.4.4.8 Program Map Table, hls-mpeg-ts-iso13818-1.pdf, page 64
 * The Program Map Table provides the mappings between program numbers and the program elements that comprise
 * them. A single instance of such a mapping is referred to as a "program definition". The program map table is the
 * complete collection of all program definitions for a Transport Stream. This table shall be transmitted in packets, the PID
 * values of which are selected by the encoder. More than one PID value may be used, if desired. The table is contained in
 * one or more sections with the following syntax. It may be segmented to occupy multiple sections. In each section, the
 * section number field shall be set to zero. Sections are identified by the program_number field.
 */
class SrsTsPayloadPMT extends SrsTsPayloadPSI
{
    // 2B
    /**
     * program_number is a 16-bit field. It specifies the program to which the program_map_PID is
     * applicable. One program definition shall be carried within only one TS_program_map_section. This implies that a
     * program definition is never longer than 1016 (0x3F8). See Informative Annex C for ways to deal with the cases when
     * that length is not sufficient. The program_number may be used as a designation for a broadcast channel, for example. By
     * describing the different program elements belonging to a program, data from different sources (e.g. sequential events)
     * can be concatenated together to form a continuous set of streams using a program_number. For examples of applications
     * refer to Annex C.
     */
    public var program_number:int; //16bits
    
    // 1B
    /**
     * reverved value, must be '1'
     */
    public var const1_value0:int; //2bits
    /**
     * This 5-bit field is the version number of the TS_program_map_section. The version number shall be
     * incremented by 1 modulo 32 when a change in the information carried within the section occurs. Version number refers
     * to the definition of a single program, and therefore to a single section. When the current_next_indicator is set to '1', then
     * the version_number shall be that of the currently applicable TS_program_map_section. When the current_next_indicator
     * is set to '0', then the version_number shall be that of the next applicable TS_program_map_section.
     */
    public var version_number:int; //5bits
    /**
     * A 1-bit field, which when set to '1' indicates that the TS_program_map_section sent is
     * currently applicable. When the bit is set to '0', it indicates that the TS_program_map_section sent is not yet applicable
     * and shall be the next TS_program_map_section to become valid.
     */
    public var current_next_indicator:int; //1bit
    
    // 1B
    /**
     * The value of this 8-bit field shall be 0x00.
     */
    public var section_number:int; //8bits
    
    // 1B
    /**
     * The value of this 8-bit field shall be 0x00.
     */
    public var last_section_number:int; //8bits
    
    // 2B
    /**
     * reverved value, must be '1'
     */
    public var const1_value1:int; //3bits
    /**
     * This is a 13-bit field indicating the PID of the Transport Stream packets which shall contain the PCR fields
     * valid for the program specified by program_number. If no PCR is associated with a program definition for private
     * streams, then this field shall take the value of 0x1FFF. Refer to the semantic definition of PCR in 2.4.3.5 and Table 2-3
     * for restrictions on the choice of PCR_PID value.
     */
    public var PCR_PID:int; //13bits
    
    // 2B
    public var const1_value2:int; //4bits
    /**
     * This is a 12-bit field, the first two bits of which shall be '00'. The remaining 10 bits specify the
     * number of bytes of the descriptors immediately following the program_info_length field.
     */
    public var program_info_length:int; //12bits
    public var program_info_desc:ByteArray; //[program_info_length]bytes
    
    // array of TSPMTESInfo, elem is SrsTsPayloadPMTESInfo object.
    public var infos:Array;
    
    public function SrsTsPayloadPMT(p:SrsTsPacket)
    {
        super(p);
        
        const1_value0 = 3;
        const1_value1 = 7;
        const1_value2 = 0x0f;
        program_info_length = 0;
        program_info_desc = null;
        infos = new Array();
    }
    
    override protected function psi_decode(stream:ByteArray):void
    {
        // atleast 9B for PMT specified
        if (stream.bytesAvailable < 9) {
            throw new Error("ts: demux PMT failed.");
        }
        
        // 2B
        program_number = stream.readUnsignedShort();
        
        // 1B
        var cniv:int = stream.readUnsignedByte();
        
        const1_value0 = (cniv >> 6) & 0x03;
        version_number = (cniv >> 1) & 0x1F;
        current_next_indicator = cniv & 0x01;
        
        // 1B
        section_number = stream.readUnsignedByte();
        
        // 1B
        last_section_number = stream.readUnsignedByte();
        
        // 2B
        var ppv:int = stream.readUnsignedShort();
        const1_value1 = (ppv >> 13) & 0x07;
        PCR_PID = ppv & 0x1FFF;
        
        // 2B
        var pilv:int = stream.readUnsignedShort();
        const1_value2 = (pilv >> 12) & 0x0F;
        program_info_length = pilv & 0xFFF;
        
        if (program_info_length > 0) {
            if (stream.bytesAvailable < program_info_length) {
                throw new Error("ts: demux PMT program info failed.");
            }
            
            program_info_desc = new ByteArray();
            stream.readBytes(program_info_desc, 0, program_info_length);
        }
        
        // [section_length] - 4(CRC) - 9B - [program_info_length]
        var ES_EOF_pos:uint = stream.position + section_length - 4 - 9 - program_info_length;
        while (stream.position < ES_EOF_pos) {
            var info:SrsTsPayloadPMTESInfo = new SrsTsPayloadPMTESInfo(SrsTsStream.Reserved, 0);
            infos.push(info);
            
            info.decode(stream);
            
            // update the apply pid table
            switch (info.stream_type) {
                case SrsTsStream.VideoH264:
                case SrsTsStream.VideoMpeg4:
                    packet.context.setChannel(info.elementary_PID, SrsTsPidApply.Video, info.stream_type);
                    break;
                case SrsTsStream.AudioAAC:
                case SrsTsStream.AudioAC3:
                case SrsTsStream.AudioDTS:
                case SrsTsStream.AudioMp3:
                    packet.context.setChannel(info.elementary_PID, SrsTsPidApply.Audio, info.stream_type);
                    break;
                default:
                    // warn("ts: drop pid=%#x, stream=%#x", info->elementary_PID, info->stream_type);
                    break;
            }
        }
        
        // update the apply pid table.
        packet.context.setChannel(packet.pid, SrsTsPidApply.PMT, SrsTsStream.Reserved);
        packet.context.on_pmt_parsed();
    }
};

/**
 * the m3u8 parser.
 */
class M3u8
{
    private var _hls:HlsCodec;
    private var _log:ILogger = new TraceLogger("HLS");
    
    private var _tses:Array;
    private var _duration:Number;
    private var _seq_no:Number;
    private var _url:String;
    // when variant, all ts url is sub m3u8 url.
    private var _variant:Boolean;
    
    public function M3u8(hls:HlsCodec)
    {
        _hls = hls;
        _tses = new Array();
        _duration = 0;
        _seq_no = 0;
        _variant = false;
    }
    
    public function getTsUrl(index:Number):String
    {
        if (index >= _tses.length) {
            throw new Error("ts index overflow, index=" + index + ", max=" + _tses.length);
        }
        
        var url:String =  _tses[index].url;
        
        // if absolute url, return.
        if (Utility.stringStartswith(url, "http://")) {
            return url;
        }
        
        // add prefix of m3u8.
        if (_url.lastIndexOf("/") >= 0) {
            url = _url.substr(0, _url.lastIndexOf("/") + 1) + url;
        }
        return url;
    }
    
    public function parse(url:String, v:String):void
    {
        // TODO: FIXME: reset the m3u8 when parse.
        _tses = new Array();
        _duration = 0;
        _url = url;
        _variant = false;
        
        var ptl:String = null;
        var td:Number = 0;
        var seq_no:Number = 0;
        
        var lines:Array = v.split("\n");
        for (var i:int = 0; i < lines.length; i++) {
            var line:String = String(lines[i]).replace("\r", "").replace(" ", "");
            
            // #EXT-X-VERSION:3
            // the version must be 3.0
            if (Utility.stringStartswith(line, "#EXT-X-VERSION:")) {
                if (!Utility.stringEndswith(line, ":3")) {
                    _log.warn("m3u8 3.0 required, actual is {0}", line);
                }
                continue;
            }

            // #EXT-X-STREAM-INF:BANDWIDTH=3000000
            if (Utility.stringStartswith(line, "#EXT-X-STREAM-INF:")) {
                _variant = true;
                var sub_m3u8_url:String = lines[++i];
                _tses.push({
                    duration: 0,
                    url: sub_m3u8_url
                });
                continue;
            }
            
            // #EXT-X-PLAYLIST-TYPE:VOD
            // the playlist type, vod or nothing.
            if (Utility.stringStartswith(line, "#EXT-X-PLAYLIST-TYPE:")) {
                ptl = line;
                continue;
            }

            // #EXT-X-MEDIA-SEQUENCE:55
            // the seq no of first ts.
            if (Utility.stringStartswith(line, "#EXT-X-MEDIA-SEQUENCE:")) {
                seq_no = Number(line.substr("#EXT-X-MEDIA-SEQUENCE:".length));
                continue;
            }

            // #EXT-X-TARGETDURATION:12
            // the target duration is required.
            if (Utility.stringStartswith(line, "#EXT-X-TARGETDURATION:")) {
                td = Number(line.substr("#EXT-X-TARGETDURATION:".length));
                continue;
            }
            
            // #EXT-X-ENDLIST
            // parse completed.
            if (line == "#EXT-X-ENDLIST") {
                break;
            }
            
            // #EXTINF:11.401,
            // livestream-5.ts
            // parse each ts entry, expect current line is inf.
            if (!Utility.stringStartswith(line, "#EXTINF:")) {
                continue;
            }
            // expect next line is url.
            if (i >= lines.length - 1) {
                _log.warn("ts entry unexpected eof, inf={0}", line);
                break;
            }
            if (line.indexOf(",") >= 0) {
                line = line.split(",")[0];
            }
            var ts_duration:Number = Number(line.substr("#EXTINF:".length).replace(",", ""));
            var ts_url:String = lines[++i];
            
            _duration += ts_duration;
            _tses.push({
                duration: ts_duration,
                url: ts_url
            });
        }
        _seq_no = seq_no;
        
        // for vod, must specifies the playlist type to vod.
        if (false) {
            _log.warn("vod required the playlist type, actual is {0}", ptl);
        }
        
		if (false) {
			_log.info("hls got {0}B {1}L m3u8, td={2}, ts={3}", v.length, lines.length, td, _tses.length);
		} else {
			_log.debug("hls got {0}B {1}L m3u8, td={2}, ts={3}", v.length, lines.length, td, _tses.length);
		}
    }
    
    public function get tsCount():Number
    {
        return _tses.length;
    }
    
    public function get duration():Number
    {
        return _duration;
    }

    public function get seq_no():Number
    {
        return _seq_no;
    }

    public function get variant():Boolean
    {
        return _variant;
    }
}