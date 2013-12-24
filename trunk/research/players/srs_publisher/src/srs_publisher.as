package
{
    import flash.display.Sprite;
    import flash.display.StageAlign;
    import flash.display.StageScaleMode;
    import flash.events.Event;
    import flash.events.NetStatusEvent;
    import flash.external.ExternalInterface;
    import flash.media.Camera;
    import flash.media.H264Profile;
    import flash.media.H264VideoStreamSettings;
    import flash.media.Microphone;
    import flash.media.Video;
    import flash.net.NetConnection;
    import flash.net.NetStream;
    import flash.ui.ContextMenu;
    import flash.ui.ContextMenuItem;
    import flash.utils.setTimeout;
    
    public class srs_publisher extends Sprite
    {
        // user set id.
        private var js_id:String = null;
        // user set callback
        private var js_on_publisher_ready:String = null;
        private var js_on_publisher_error:String = null;
        private var js_on_publisher_warn:String = null;
        
        // publish param url.
        private var user_url:String = null;
        // play param, user set width and height
        private var user_w:int = 0;
        private var user_h:int = 0;
        private var user_vcodec:Object = {};
        private var user_acodec:Object = {};
        
        // media specified.
        private var media_conn:NetConnection = null;
        private var media_stream:NetStream = null;
        private var media_video:Video = null;
        private var media_camera:Camera = null;
        private var media_microphone:Microphone = null;
        
        // error code.
        private const error_camera_get:int = 100;
        private const error_microphone_get:int = 101;
        private const error_camera_muted:int = 102;
        
        public function srs_publisher()
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
            
            this.contextMenu = new ContextMenu();
            this.contextMenu.hideBuiltInItems();
            
            var flashvars:Object = this.root.loaderInfo.parameters;
            
            if (!flashvars.hasOwnProperty("id")) {
                throw new Error("must specifies the id");
            }
            
            this.js_id = flashvars.id;
            this.js_on_publisher_ready = flashvars.on_publisher_ready;
            this.js_on_publisher_error = flashvars.on_publisher_error;
            this.js_on_publisher_warn = flashvars.on_publisher_warn;
            
            flash.utils.setTimeout(this.system_on_js_ready, 0);
        }
        
        /**
         * system callack event, when js ready, register callback for js.
         * the actual main function.
         */
        private function system_on_js_ready():void {
            if (!flash.external.ExternalInterface.available) {
                trace("js not ready, try later.");
                flash.utils.setTimeout(this.system_on_js_ready, 100);
                return;
            }
            
            flash.external.ExternalInterface.addCallback("__publish", this.js_call_publish);
            flash.external.ExternalInterface.addCallback("__stop", this.js_call_stop);
            
            var cameras:Array = Camera.names;
            var microphones:Array = Microphone.names;
            trace("retrieve system cameras(" + cameras + ") and microphones(" + microphones + ")");
            
            flash.external.ExternalInterface.call(this.js_on_publisher_ready, this.js_id, cameras, microphones);
        }
        
        /**
        * notify the js an error occur.
        */
        private function system_error(code:int, desc:String):void {
            trace("system error, code=" + code + ", error=" + desc);
            flash.external.ExternalInterface.call(this.js_on_publisher_error, this.js_id, code);
        }
        private function system_warn(code:int, desc:String):void {
            trace("system warn, code=" + code + ", error=" + desc);
            flash.external.ExternalInterface.call(this.js_on_publisher_warn, this.js_id, code);
        }
        
        /**
         * publish stream to server.
         * @param url a string indicates the rtmp url to publish.
         * @param _width, the player width.
         * @param _height, the player height.
         * @param vcodec an object contains the video codec info.
         * @param acodec an object contains the audio codec info.
         */
        private function js_call_publish(url:String, _width:int, _height:int, vcodec:Object, acodec:Object):void {
            trace("start to publish to " + url + ", vcodec " + JSON.stringify(vcodec) + ", acodec " + JSON.stringify(acodec));
            
            this.user_url = url;
            this.user_w = _width;
            this.user_h = _height;
            this.user_vcodec = vcodec;
            this.user_acodec = acodec;
            
            this.js_call_stop();
            
            // microphone and camera
            var m:Microphone = Microphone.getMicrophone(acodec.device_code);
            if(m == null){
                this.system_error(this.error_microphone_get, "failed to open microphone " + acodec.device_code + "(" + acodec.device_name + ")");
                return;
            }
            // ignore muted, for flash will require user to access it.
            
            // Remark: the name is the index!
            var c:Camera = Camera.getCamera(vcodec.device_code);
            if(c == null){
                this.system_error(this.error_camera_get, "failed to open camera " + vcodec.device_code + "(" + vcodec.device_name + ")");
                return;
            }
            // ignore muted, for flash will require user to access it.
            // but we still warn user.
            if(c && c.muted){
                this.system_warn(this.error_camera_muted, "Access Denied, camera " + vcodec.device_code + "(" + vcodec.device_name + ") is muted");
            }
            
            this.media_camera = c;
            this.media_microphone = m;
            
            this.media_conn = new NetConnection();
            this.media_conn.client = {};
            this.media_conn.client.onBWDone = function():void {};
            this.media_conn.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                trace ("NetConnection: code=" + evt.info.code);
                
                if (evt.info.hasOwnProperty("data") && evt.info.data) {
                    // for context menu
                    var customItems:Array = [new ContextMenuItem("SrsPlayer")];
                    if (evt.info.data.hasOwnProperty("srs_server")) {
                        customItems.push(new ContextMenuItem("Server: " + evt.info.data.srs_server));
                    }
                    if (evt.info.data.hasOwnProperty("srs_contributor")) {
                        customItems.push(new ContextMenuItem("Contributor: " + evt.info.data.srs_contributor));
                    }
                    contextMenu.customItems = customItems;
                }
                
                // TODO: FIXME: failed event.
                if (evt.info.code != "NetConnection.Connect.Success") {
                    return;
                }
                
                media_stream = new NetStream(media_conn);
                media_stream.client = {};
                media_stream.addEventListener(NetStatusEvent.NET_STATUS, function(evt:NetStatusEvent):void {
                    trace ("NetStream: code=" + evt.info.code);
                    
                    // TODO: FIXME: failed event.
                });
                
                __build_video_codec(media_stream, c, vcodec);
                __build_audio_codec(media_stream, m, acodec);
                
                if (media_microphone) {
                    media_stream.attachAudio(m);
                }
                if (media_camera) {
                    media_stream.attachCamera(c);
                }
                
                var streamName:String = url.substr(url.lastIndexOf("/"));
                media_stream.publish(streamName);
                
                media_video = new Video();
                media_video.width = _width;
                media_video.height = _height;
                media_video.attachCamera(media_camera);
                media_video.smoothing = true;
                addChild(media_video);
                
                //__draw_black_background(_width, _height);
                
                // lowest layer, for mask to cover it.
                setChildIndex(media_video, 0);
            });
            
            var tcUrl:String = this.user_url.substr(0, this.user_url.lastIndexOf("/"));
            this.media_conn.connect(tcUrl);
        }
        
        /**
         * function for js to call: to stop the stream. ignore if not publish.
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
        }
        
        private function __build_audio_codec(stream:NetStream, m:Microphone, acodec:Object):void {
            if (!m) {
                return;
            }
            
            // if no microphone, donot set the params.
            if(m == null){
                return;
            }
            
            // use default values.
            var microEncodeQuality:int = 8;
            var microRate:int = 22; // 22 === 22050 Hz
            
            trace("[Publish] audio encoding parameters: " 
                + "audio(microphone) encodeQuality=" + microEncodeQuality
                + ", rate=" + microRate + "(22050Hz)"
            );
            
            // The encoded speech quality when using the Speex codec. Possible values are from 0 to 10. The default value is 6. Higher numbers 
            // represent higher quality but require more bandwidth, as shown in the following table. The bit rate values that are listed represent 
            // net bit rates and do not include packetization overhead.
            m.encodeQuality = microEncodeQuality;
            
            // The rate at which the microphone is capturing sound, in kHz. Acceptable values are 5, 8, 11, 22, and 44. The default value is 8 kHz 
            // if your sound capture device supports this value. Otherwise, the default value is the next available capture level above 8 kHz that 
            // your sound capture device supports, usually 11 kHz.
            m.rate = microRate;
        }
        private function __build_video_codec(stream:NetStream, c:Camera, vcodec:Object):void {
            if (!c) {
                return;
            }
            
            if(vcodec.codec == "vp6"){
                trace("use VP6, donot use H.264 publish encoding.");
                return;
            }

            var x264profile:String = (vcodec.profile == "main") ? H264Profile.MAIN : H264Profile.BASELINE;
            var x264level:String = vcodec.level;
            var cameraFps:Number = Number(vcodec.fps);
            var x264KeyFrameInterval:int = int(vcodec.gop * cameraFps);
            var cameraWidth:int = String(vcodec.size).split("x")[0];
            var cameraHeight:int = String(vcodec.size).split("x")[1];
            var cameraBitrate:int = int(vcodec.bitrate);
            
            // use default values.
            var cameraQuality:int = 85;
            
            trace("[Publish] video h.264(x264) encoding parameters: " 
                + "profile=" + x264profile 
                + ", level=" + x264level
                + ", keyFrameInterval(gop)=" + x264KeyFrameInterval
                + "; video(camera) width=" + cameraWidth
                + ", height=" + cameraHeight
                + ", fps=" + cameraFps
                + ", bitrate=" + cameraBitrate
                + ", quality=" + cameraQuality
            );
            
            var h264Settings:H264VideoStreamSettings = new H264VideoStreamSettings();
            // we MUST set its values first, then set the NetStream.videoStreamSettings, or it will keep the origin values.
            h264Settings.setProfileLevel(x264profile, x264level); 
            stream.videoStreamSettings = h264Settings;
            // the setKeyFrameInterval/setMode/setQuality use the camera settings.
            // http://help.adobe.com/en_US/FlashPlatform/reference/actionscript/3/flash/media/VideoStreamSettings.html
            // Note This feature will be supported in future releases of Flash Player and AIR, for now, Camera parameters are used.
            //
            //h264Settings.setKeyFrameInterval(4);
            //h264Settings.setMode(800, 600, 15);
            //h264Settings.setQuality(500, 0);
            
            // set the camera and microphone.
            
            // setKeyFrameInterval(keyFrameInterval:int):void
            // 	keyFrameInterval:int — A value that specifies which video frames are transmitted in full (as keyframes) instead of being 
            //		interpolated by the video compression algorithm. A value of 1 means that every frame is a keyframe, a value of 3 means 
            //		that every third frame is a keyframe, and so on. Acceptable values are 1 through 48.
            c.setKeyFrameInterval(x264KeyFrameInterval);
            
            // setMode(width:int, height:int, fps:Number, favorArea:Boolean = true):void
            //  width:int — The requested capture width, in pixels. The default value is 160.
            //  height:int — The requested capture height, in pixels. The default value is 120.
            //  fps:Number — The requested rate at which the camera should capture data, in frames per second. The default value is 15.
            c.setMode(cameraWidth, cameraHeight, cameraFps);
            
            // setQuality(bandwidth:int, quality:int):void
            //  bandwidth:int — Specifies the maximum amount of bandwidth that the current outgoing video feed can use, in bytes per second. 
            //		To specify that the video can use as much bandwidth as needed to maintain the value of quality, pass 0 for bandwidth. 
            //		The default value is 16384.
            //  quality:int — An integer that specifies the required level of picture quality, as determined by the amount of compression 
            // 		being applied to each video frame. Acceptable values range from 1 (lowest quality, maximum compression) to 100 
            //		(highest quality, no compression). To specify that picture quality can vary as needed to avoid exceeding bandwidth, 
            //		pass 0 for quality.
            //  winlin:
            //		bandwidth is in bps not kbps. 500*1000 = 500kbps.
            //		quality=1 is lowest quality, 100 is highest quality.
            c.setQuality(cameraBitrate * 1000, cameraQuality);
        }
    }
}