//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
/**
* padding the output.
* padding(3, 5, '0') is 00003
* padding(3, 5, 'x') is xxxx3
* @see http://blog.csdn.net/win_lin/article/details/12065413
*/
function padding(number, length, prefix) {
    if(String(number).length >= length){
        return String(number);
    }
    return padding(prefix+number, length, prefix);
}

/**
* update the navigator, add same query string.
*/
function update_nav() {
    $("#nav_srs_player").attr("href", "srs_player.html" + window.location.search);
    $("#nav_srs_publisher").attr("href", "srs_publisher.html" + window.location.search);
    $("#nav_srs_chat").attr("href", "srs_chat.html" + window.location.search);
    $("#nav_srs_bwt").attr("href", "srs_bwt.html" + window.location.search);
    $("#nav_jwplayer6").attr("href", "jwplayer6.html" + window.location.search);
    $("#nav_osmf").attr("href", "osmf.html" + window.location.search);
    $("#nav_vlc").attr("href", "vlc.html" + window.location.search);
}

/**
* log specified, there must be a log element as:
    <!-- for the log -->
    <div class="alert alert-info fade in" id="txt_log">
        <button type="button" class="close" data-dismiss="alert">×</button>
        <strong><span id="txt_log_title">Usage:</span></strong>
        <span id="txt_log_msg">创建会议室，或者加入会议室</span>
    </div>
*/
function info(desc) {
    $("#txt_log").addClass("alert-info").removeClass("alert-error").removeClass("alert-warn");
    $("#txt_log_title").text("Info:");
    $("#txt_log_msg").text(desc);
}
function warn(code, desc) {
    $("#txt_log").removeClass("alert-info").removeClass("alert-error").addClass("alert-warn");
    $("#txt_log_title").text("Warn:");
    $("#txt_log_msg").text("code: " + code + ", " + desc);
}
function error(code, desc) {
    $("#txt_log").removeClass("alert-info").addClass("alert-error").removeClass("alert-warn");
    $("#txt_log_title").text("Error:");
    $("#txt_log_msg").text("code: " + code + ", " + desc);
}

/**
* parse the query string to object.
*/
function parse_query_string(){
    var obj = {};
    
    // parse the host(hostname:http_port), pathname(dir/filename)
    obj.host = window.location.host;
    obj.hostname = window.location.hostname;
    obj.http_port = (window.location.port == "")? 80:window.location.port;
    obj.pathname = window.location.pathname;
    if (obj.pathname.lastIndexOf("/") <= 0) {
        obj.dir = "/";
        obj.filename = "";
    } else {
        obj.dir = obj.pathname.substr(0, obj.pathname.lastIndexOf("/"));
        obj.filename = obj.pathname.substr(obj.pathname.lastIndexOf("/"));
    }
    
    // parse the query string.
    var query_string = String(window.location.search).replace(" ", "").split("?")[1];
    if(query_string == undefined){
        return obj;
    }
    
    var queries = query_string.split("&");
    $(queries).each(function(){
        var query = this.split("=");
        obj[query[0]] = query[1];
    });
    
    return obj;
}

/**
@param server the ip of server. default to window.location.hostname
@param vhost the vhost of rtmp. default to window.location.hostname
@param port the port of rtmp. default to 1935
@param app the app of rtmp. default to live.
@param stream the stream of rtmp. default to livestream.
*/
function build_default_rtmp_url() {
    var query = parse_query_string();

    var server = (query.server == undefined)? window.location.hostname:query.server;
    var port = (query.port == undefined)? 1935:query.port;
    var vhost = (query.vhost == undefined)? window.location.hostname:query.vhost;
    var app = (query.app == undefined)? "live":query.app;
    var stream = (query.stream == undefined)? "livestream":query.stream;

    if (server == vhost || vhost == "") {
        return "rtmp://" + server + ":" + port + "/" + app + "/" + stream;
    } else {
        return "rtmp://" + server + ":" + port + "/" + app + "...vhost..." + vhost + "/" + stream;
    }
}
// for the chat to init the publish url.
function build_default_publish_rtmp_url() {
    var query = parse_query_string();

    var server = (query.server == undefined)? window.location.hostname:query.server;
    var port = (query.port == undefined)? 1935:query.port;
    var vhost = (query.vhost == undefined)? window.location.hostname:query.vhost;
    var app = (query.app == undefined)? "live":query.app;
    var stream = (query.stream == undefined)? "livestream":query.stream;

    if (server == vhost || vhost == "") {
        return "rtmp://" + server + ":" + port + "/" + app + "/" + stream;
    } else {
        vhost = srs_get_player_publish_vhost(vhost);
        return "rtmp://" + server + ":" + port + "/" + app + "...vhost..." + vhost + "/" + stream;
    }
}

/**
@param server the ip of server. default to window.location.hostname
@param vhost the vhost of hls. default to window.location.hostname
@param hls_vhost the vhost of hls. override the server if specified.
@param hls_port the port of hls. default to window.location.port
@param app the app of hls. default to live.
@param stream the stream of hls. default to livestream.
*/
function build_default_hls_url() {
    var query = parse_query_string();

    // for http, use hls_vhost to override server if specified.
    var server = window.location.hostname;
    if (query.server != undefined) {
        server = query.server;
    } else if (query.hls_vhost != undefined) {
        server = query.hls_vhost;
    }
    
    var port = (query.hls_port == undefined)? window.location.port:query.hls_port;
    var app = (query.app == undefined)? "live":query.app;
    var stream = (query.stream == undefined)? "livestream":query.stream;

    if (port == "" || port == null || port == undefined) {
        port = 80;
    }
    
    return "http://" + server + ":" + port + "/" + app + "/" + stream + ".m3u8";
}

/**
* parse the rtmp url,
* for example: rtmp://demo.srs.com:1935/live...vhost...players/livestream
* @return object {server, port, vhost, app, stream}
*/
function srs_parse_rtmp_url(rtmp_url) {
    // @see: http://stackoverflow.com/questions/10469575/how-to-use-location-object-to-parse-url-without-redirecting-the-page-in-javascri
    var a = document.createElement("a");
    a.href = rtmp_url.replace("rtmp://", "http://");
    
    var vhost = a.hostname;
    var port = (a.port == "")? "1935":a.port;
    var app = a.pathname.substr(1, a.pathname.lastIndexOf("/") - 1);
    var stream = a.pathname.substr(a.pathname.lastIndexOf("/") + 1);

    // parse the vhost in the params of app, that srs supports.
    app = app.replace("...vhost...", "?vhost=");
    if (app.indexOf("?") >= 0) {
        var params = app.substr(app.indexOf("?"));
        app = app.substr(0, app.indexOf("?"));
        
        if (params.indexOf("vhost=") > 0) {
            vhost = params.substr(params.indexOf("vhost=") + "vhost=".length);
            if (vhost.indexOf("&") > 0) {
                vhost = vhost.substr(0, vhost.indexOf("&"));
            }
        }
    }
    
    var ret = {
        server: a.hostname, port: port, 
        vhost: vhost, app: app, stream: stream
    };
    
    return ret;
}

/**
* player specified size.
*/
function srs_get_player_modal() { return 740; }
function srs_get_player_width() { return srs_get_player_modal() - 30; }
function srs_get_player_height() { return srs_get_player_width() * 9 / 19; }

// to query the swf anti cache.
function srs_get_version_code() { return "1.5"; }
// get the default vhost for players.
function srs_get_player_vhost() { return "players"; }
// the api server port, for chat room.
function srs_get_api_server_port() { return 8085; }
// get the stream published to vhost,
// generally we need to transcode the stream to support HLS and filters.
// for example, src_vhost is "players", we transcode stream to vhost "players_pub".
// if not equals to the player vhost, return the orignal vhost.
function srs_get_player_publish_vhost(src_vhost) { return (src_vhost != srs_get_player_vhost())? src_vhost:(src_vhost + "_pub"); }

/**
* initialize the page.
* @param rtmp_url the div id contains the rtmp stream url to play
* @param hls_url the div id contains the hls stream url to play
* @param modal_player the div id contains the modal player
*/
function srs_init(rtmp_url, hls_url, modal_player) {
    update_nav();
    
    if (rtmp_url) {
        $(rtmp_url).val(build_default_rtmp_url());
    }
    if (hls_url) {
        $(hls_url).val(build_default_hls_url());
    }
    if (modal_player) {
        $(modal_player).width(srs_get_player_modal() + "px");
        $(modal_player).css("margin-left", "-" + srs_get_player_modal() / 2 +"px");
    }
}
// for the chat to init the publish url.
function srs_init_publish(rtmp_url) {
    update_nav();
    
    if (rtmp_url) {
        $(rtmp_url).val(build_default_publish_rtmp_url());
    }
}

/**
* when publisher ready, init the page elements.
*/
function srs_publisher_initialize_page(
    cameras, microphones,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate
) {
    $(sl_cameras).empty();
    for (var i = 0; i < cameras.length; i++) {
        $(sl_cameras).append("<option value='" + i + "'>" + cameras[i] + "</option");
    }
    // optional: select the first no "virtual" signed.
    for (var i = 0; i < cameras.length; i++) {
        if (cameras[i].toLowerCase().indexOf("virtual") == -1) {
            $(sl_cameras + " option[value='" + i + "']").attr("selected", true);
            break;
        }
    }
    
    $(sl_microphones).empty();
    for (var i = 0; i < microphones.length; i++) {
        $(sl_microphones).append("<option value='" + i + "'>" + microphones[i] + "</option");
    }
    
    $(sl_vcodec).empty();
    var vcodecs = ["h264", "vp6"];
    for (var i = 0; i < vcodecs.length; i++) {
        $(sl_vcodec).append("<option value='" + vcodecs[i] + "'>" + vcodecs[i] + "</option");
    }
    
    $(sl_profile).empty();
    var profiles = ["baseline", "main"];
    for (var i = 0; i < profiles.length; i++) {
        $(sl_profile).append("<option value='" + profiles[i] + "'>" + profiles[i] + "</option");
    }
    
    $(sl_level).empty();
    var levels = ["1", "1b", "1.1", "1.2", "1.3", 
        "2", "2.1", "2.2", "3", "3.1", "3.2", "4", "4.1", "4.2", "5", "5.1"];
    for (var i = 0; i < levels.length; i++) {
        $(sl_level).append("<option value='" + levels[i] + "'>" + levels[i] + "</option");
    }
    $(sl_level + " option[value='4.1']").attr("selected", true);
    
    $(sl_gop).empty();
    var gops = ["0.3", "0.5", "1", "2", "3", "4", 
        "5", "6", "7", "8", "9", "10", "15", "20"];
    for (var i = 0; i < gops.length; i++) {
        $(sl_gop).append("<option value='" + gops[i] + "'>" + gops[i] + "秒</option");
    }
    $(sl_gop + " option[value='5']").attr("selected", true);
    
    $(sl_size).empty();
    var sizes = ["176x144", "320x240", "352x240", 
        "352x288", "460x240", "640x480", "720x480", "720x576", "800x600", 
        "1024x768", "1280x720", "1360x768", "1920x1080"];
    for (i = 0; i < sizes.length; i++) {
        $(sl_size).append("<option value='" + sizes[i] + "'>" + sizes[i] + "</option");
    }
    $(sl_size + " option[value='460x240']").attr("selected", true);
    
    $(sl_fps).empty();
    var fpses = ["5", "10", "15", "20", "24", "25", "29.97", "30"];
    for (i = 0; i < fpses.length; i++) {
        $(sl_fps).append("<option value='" + fpses[i] + "'>" + Number(fpses[i]).toFixed(2) + " 帧/秒</option");
    }
    $(sl_fps + " option[value='15']").attr("selected", true);
    
    $(sl_bitrate).empty();
    var bitrates = ["50", "200", "350", "500", "650", "800", 
        "950", "1000", "1200", "1500", "1800", "2000", "3000", "5000"];
    for (i = 0; i < bitrates.length; i++) {
        $(sl_bitrate).append("<option value='" + bitrates[i] + "'>" + bitrates[i] + " kbps</option");
    }
    $(sl_bitrate + " option[value='350']").attr("selected", true);
}
/**
* get the vcodec and acodec.
*/
function srs_publiser_get_codec(
    vcodec, acodec,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate
) {
    acodec.device_code = $(sl_microphones).val();
    acodec.device_name = $(sl_microphones).text();
    
    vcodec.device_code = $(sl_cameras).find("option:selected").val();
    vcodec.device_name = $(sl_cameras).find("option:selected").text();
    
    vcodec.codec    = $(sl_vcodec).find("option:selected").val();
    vcodec.profile  = $(sl_profile).find("option:selected").val();
    vcodec.level    = $(sl_level).find("option:selected").val();
    vcodec.fps      = $(sl_fps).find("option:selected").val();
    vcodec.gop      = $(sl_gop).find("option:selected").val();
    vcodec.size     = $(sl_size).find("option:selected").val();
    vcodec.bitrate  = $(sl_bitrate).find("option:selected").val();
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
/**
* the SrsPlayer object.
* @param container the html container id.
* @param width a float value specifies the width of player.
* @param height a float value specifies the height of player.
*/
function SrsPlayer(container, width, height) {
    if (!SrsPlayer.__id) {
        SrsPlayer.__id = 100;
    }
    if (!SrsPlayer.__players) {
        SrsPlayer.__players = [];
    }
    
    SrsPlayer.__players.push(this);
    
    this.container = container;
    this.width = width;
    this.height = height;
    this.id = SrsPlayer.__id++;
    this.stream_url = null;
    this.buffer_time = 0.8; // default to 0.8
    this.callbackObj = null;
    
    // callback set the following values.
    this.meatadata = {}; // for on_player_metadata
    this.time = 0; // current stream time.
    this.buffer_length = 0; // current stream buffer length.
}
/**
* user can set some callback, then start the player.
* @param url the default url.
* callbacks:
*      on_player_ready():int, when srs player ready, user can play.
*      on_player_metadata(metadata:Object):int, when srs player get metadata.
*/
SrsPlayer.prototype.start = function(url) {
    if (url) {
        this.stream_url = url;
    }
    
    // embed the flash.
    var flashvars = {};
    flashvars.id = this.id;
    flashvars.on_player_ready = "__srs_on_player_ready";
    flashvars.on_player_metadata = "__srs_on_player_metadata";
    flashvars.on_player_timer = "__srs_on_player_timer";
    
    var params = {};
    params.wmode = "opaque";
    params.allowFullScreen = "true";
    params.allowScriptAccess = "always";
    
    var attributes = {};
    
    var self = this;
    
    swfobject.embedSWF(
        "srs_player/release/srs_player.swf?_version="+srs_get_version_code(), 
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
* play the stream.
* @param stream_url the url of stream, rtmp or http.
*/
SrsPlayer.prototype.play = function(url) {
    if (url) {
        this.stream_url = url;
    }
    this.callbackObj.ref.__play(this.stream_url, this.width, this.height, this.buffer_time);
}
SrsPlayer.prototype.stop = function() {
    for (var i = 0; i < SrsPlayer.__players.length; i++) {
        var player = SrsPlayer.__players[i];
        
        if (player.id != this.id) {
            continue;
        }
        
        SrsPlayer.__players.splice(i, 1);
        break;
    }
    
    this.callbackObj.ref.__stop();
}
SrsPlayer.prototype.pause = function() {
    this.callbackObj.ref.__pause();
}
SrsPlayer.prototype.resume = function() {
    this.callbackObj.ref.__resume();
}
/**
* to set the DAR, for example, DAR=16:9
* @param num, for example, 9. 
*       use metadata height if 0.
*       use user specified height if -1.
* @param den, for example, 16. 
*       use metadata width if 0.
*       use user specified width if -1.
*/
SrsPlayer.prototype.dar = function(num, den) {
    this.callbackObj.ref.__dar(num, den);
}
/**
* set the fullscreen size data.
* @refer the refer fullscreen mode. it can be:
*       video: use video orignal size.
*       screen: use screen size to rescale video.
* @param percent, the rescale percent, where
*       100 means 100%.
*/
SrsPlayer.prototype.set_fs = function(refer, percent) {
    this.callbackObj.ref.__set_fs(refer, percent);
}
/**
* set the stream buffer time in seconds.
* @buffer_time the buffer time in seconds.
*/
SrsPlayer.prototype.set_bt = function(buffer_time) {
    this.buffer_time = buffer_time;
    this.callbackObj.ref.__set_bt(buffer_time);
}
SrsPlayer.prototype.on_player_ready = function() {
}
SrsPlayer.prototype.on_player_metadata = function(metadata) {
    // ignore.
}
SrsPlayer.prototype.on_player_timer = function(time, buffer_length) {
    // ignore.
}
function __srs_find_player(id) {
    for (var i = 0; i < SrsPlayer.__players.length; i++) {
        var player = SrsPlayer.__players[i];
        
        if (player.id != id) {
            continue;
        }
        
        return player;
    }
    
    throw new Error("player not found. id=" + id);
}
function __srs_on_player_ready(id) {
    var player = __srs_find_player(id);
    player.on_player_ready();
}
function __srs_on_player_metadata(id, metadata) {
    var player = __srs_find_player(id);
    
    // user may override the on_player_metadata, 
    // so set the data before invoke it.
    player.metadata = metadata;
    
    player.on_player_metadata(metadata);
}
function __srs_on_player_timer(id, time, buffer_length) {
    var player = __srs_find_player(id);
    
    buffer_length = Math.max(0, buffer_length);
    buffer_length = Math.min(player.buffer_time, buffer_length);
    
    time = Math.max(0, time);
    
    // user may override the on_player_timer, 
    // so set the data before invoke it.
    player.time = time;
    player.buffer_length = buffer_length;
    
    player.on_player_timer(time, buffer_length);
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
/**
* the SrsPublisher object.
* @param container the html container id.
* @param width a float value specifies the width of publisher.
* @param height a float value specifies the height of publisher.
*/
function SrsPublisher(container, width, height) {
    if (!SrsPublisher.__id) {
        SrsPublisher.__id = 100;
    }
    if (!SrsPublisher.__publishers) {
        SrsPublisher.__publishers = [];
    }
    
    SrsPublisher.__publishers.push(this);
    
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
        "100": "无法获取指定的摄像头", //error_camera_get 
        "101": "无法获取指定的麦克风", //error_microphone_get 
        "102": "摄像头为禁用状态，推流时请允许flash访问摄像头", //error_camera_muted 
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
    this.url = url;
    this.vcodec = vcodec;
    this.acodec = acodec;
    
    this.callbackObj.ref.__publish(url, this.width, this.height, vcodec, acodec);
}
SrsPublisher.prototype.stop = function() {
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

