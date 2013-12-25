/**
* the SrsPublisher object.
* @param container the html container id.
* @param width a float value specifies the width of publisher.
* @param height a float value specifies the height of publisher.
* @param private_object [optional] an object that used as private object, 
*       for example, the logic chat object which owner this publisher.
*/
function SrsPublisher(container, width, height, private_object) {
    if (!SrsPublisher.__id) {
        SrsPublisher.__id = 100;
    }
    if (!SrsPublisher.__publishers) {
        SrsPublisher.__publishers = [];
    }
    
    SrsPublisher.__publishers.push(this);
    
    this.private_object = private_object;
    this.container = container;
    this.width = width;
    this.height = height;
    this.id = SrsPublisher.__id++;
    this.callbackObj = null;
    
    // set the values when publish.
    this.url = null;
    this.vcodec = {};
    this.acodec = {};
    
    // callback set the following values.
    this.cameras = [];
    this.microphones = [];
    this.code = 0;
    
    // error code defines.
    this.errors = {
        "100": "无法获取指定的摄像头。", //error_camera_get 
        "101": "无法获取指定的麦克风。", //error_microphone_get 
        "102": "摄像头为禁用状态，推流时请允许flash访问摄像头。", //error_camera_muted 
        "103": "服务器关闭了连接。", //error_connection_closed 
        "104": "服务器连接失败。", //error_connection_failed 
        "199": "未知错误。"
    };
}
/**
* user can set some callback, then start the publisher.
* callbacks:
*      on_publisher_ready(cameras, microphones):int, when srs publisher ready, user can publish.
*      on_publisher_error(code, desc):int, when srs publisher error, callback this method.
*      on_publisher_warn(code, desc):int, when srs publisher warn, callback this method.
*/
SrsPublisher.prototype.start = function() {
    // embed the flash.
    var flashvars = {};
    flashvars.id = this.id;
    flashvars.on_publisher_ready = "__srs_on_publisher_ready";
    flashvars.on_publisher_error = "__srs_on_publisher_error";
    flashvars.on_publisher_warn = "__srs_on_publisher_warn";
    
    var params = {};
    params.wmode = "opaque";
    params.allowFullScreen = "true";
    params.allowScriptAccess = "always";
    
    var attributes = {};
    
    var self = this;
    
    swfobject.embedSWF(
        "srs_publisher/release/srs_publisher.swf?_version="+srs_get_version_code(), 
        this.container,
        this.width, this.height,
        "11.1", "js/AdobeFlashPlayerInstall.swf",
        flashvars, params, attributes,
        function(callbackObj){
            self.callbackObj = callbackObj;
        }
    );
    
    return this;
}
/**
* publish stream to server.
* @param url a string indicates the rtmp url to publish.
* @param vcodec an object contains the video codec info.
* @param acodec an object contains the audio codec info.
*/
SrsPublisher.prototype.publish = function(url, vcodec, acodec) {
    this.stop();
    SrsPublisher.__publishers.push(this);
    
    if (url) {
        this.url = url;
    }
    if (vcodec) {
        this.vcodec = vcodec;
    }
    if (acodec) {
        this.acodec = acodec;
    }
    
    this.callbackObj.ref.__publish(this.url, this.width, this.height, this.vcodec, this.acodec);
}
SrsPublisher.prototype.stop = function() {
    for (var i = 0; i < SrsPublisher.__publishers.length; i++) {
        var player = SrsPublisher.__publishers[i];
        
        if (player.id != this.id) {
            continue;
        }
        
        SrsPublisher.__publishers.splice(i, 1);
        break;
    }
    
    this.callbackObj.ref.__stop();
}
/**
* when publisher ready.
* @param cameras a string array contains the names of cameras.
* @param microphones a string array contains the names of microphones.
*/
SrsPublisher.prototype.on_publisher_ready = function(cameras, microphones) {
}
/**
* when publisher error.
* @code the error code.
* @desc the error desc message.
*/
SrsPublisher.prototype.on_publisher_error = function(code, desc) {
    throw new Error("publisher error. code=" + code + ", desc=" + desc);
}
SrsPublisher.prototype.on_publisher_warn = function(code, desc) {
    throw new Error("publisher warn. code=" + code + ", desc=" + desc);
}
function __srs_find_publisher(id) {
    for (var i = 0; i < SrsPublisher.__publishers.length; i++) {
        var publisher = SrsPublisher.__publishers[i];
        
        if (publisher.id != id) {
            continue;
        }
        
        return publisher;
    }
    
    throw new Error("publisher not found. id=" + id);
}
function __srs_on_publisher_ready(id, cameras, microphones) {
    var publisher = __srs_find_publisher(id);
    
    publisher.cameras = cameras;
    publisher.microphones = microphones;
    
    publisher.on_publisher_ready(cameras, microphones);
}
function __srs_on_publisher_error(id, code) {
    var publisher = __srs_find_publisher(id);
    
    publisher.code = code;
    
    publisher.on_publisher_error(code, publisher.errors[""+code]);
}
function __srs_on_publisher_warn(id, code) {
    var publisher = __srs_find_publisher(id);
    
    publisher.code = code;
    
    publisher.on_publisher_warn(code, publisher.errors[""+code]);
}
