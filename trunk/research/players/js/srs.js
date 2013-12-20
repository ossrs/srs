/**
* update the navigator, add same query string.
*/
function update_nav() {
    $("#nav_srs_player").attr("href", "srs_player.html" + window.location.search);
    $("#nav_srs_publisher").attr("href", "srs_publisher.html" + window.location.search);
    $("#nav_srs_bwt").attr("href", "srs_bwt.html" + window.location.search);
    $("#nav_jwplayer6").attr("href", "jwplayer6.html" + window.location.search);
    $("#nav_osmf").attr("href", "osmf.html" + window.location.search);
    $("#nav_vlc").attr("href", "vlc.html" + window.location.search);
}

/**
* parse the query string to object.
*/
function parse_query_string(){
    var query_string = String(window.location.search).replace(" ", "").split("?")[1];
    if(query_string == undefined){
        return {};
    }
    
    var queries = query_string.split("&");
    var obj = {};
    $(queries).each(function(){
        var query = this.split("=");
        obj[query[0]] = query[1];
    });
    
    return obj;
}

/**
@param vhost the vhost of rtmp. default to window.location.hostname
@param port the port of rtmp. default to 1935
@param app the app of rtmp. default to live.
@param stream the stream of rtmp. default to livestream.
*/
function build_default_rtmp_url() {
    var query = parse_query_string();

    var port = (query.port == undefined)? 1935:query.port;
    var vhost = (query.vhost == undefined)? window.location.hostname:query.vhost;
    var app = (query.app == undefined)? "live":query.app;
    var stream = (query.stream == undefined)? "livestream":query.stream;

    return "rtmp://" + vhost + ":" + port + "/" + app + "/" + stream;
}

/**
@param vhost the vhost of hls. default to window.location.hostname
@param hls_port the port of hls. default to window.location.port
@param app the app of hls. default to live.
@param stream the stream of hls. default to livestream.
*/
function build_default_hls_url() {
    var query = parse_query_string();

    var vhost = (query.vhost == undefined)? window.location.hostname:query.vhost;
    var port = (query.hls_port == undefined)? window.location.port:query.hls_port;
    var app = (query.app == undefined)? "live":query.app;
    var stream = (query.stream == undefined)? "livestream":query.stream;

    if (port == "" || port == null || port == undefined) {
        port = 80;
    }
    return "http://" + vhost + ":" + port + "/" + app + "/" + stream + ".m3u8";
}

/**
* initialize the page.
* @param rtmp_url the rtmp stream url to play
* @param hls_url the hls stream url to play
*/
function srs_init(rtmp_url, hls_url) {
    update_nav();
    
    if (rtmp_url) {
        $(rtmp_url).val(build_default_rtmp_url());
    }
    if (hls_url) {
        $(hls_url).val(build_default_hls_url());
    }
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
/**
* the SrsPlayer object.
*/
function SrsPlayer(container, stream_url, width, height, buffer_time) {
    if (!SrsPlayer.__id) {
        SrsPlayer.__id = 100;
    }
    if (!SrsPlayer.__players) {
        SrsPlayer.__players = [];
    }
    
    SrsPlayer.__players.push(this);
    
    this.container = container;
    this.stream_url = stream_url;
    this.width = width;
    this.height = height;
    this.buffer_time = buffer_time;
    this.id = SrsPlayer.__id++;
    this.callbackObj = null;
    
    // callback set the following values.
    this.meatadata = {}; // for on_player_metadata
}
/**
* user can set some callback, then start the player.
* callbacks:
*      on_player_ready():int, when srs player ready, user can play.
*      on_player_metadata(metadata:Object):int, when srs player get metadata.
*/
SrsPlayer.prototype.start = function() {
    // embed the flash.
    var flashvars = {};
    flashvars.id = this.id;
    flashvars.on_player_ready = "__srs_on_player_ready";
    flashvars.on_player_metadata = "__srs_on_player_metadata";
    
    var params = {};
    params.wmode = "opaque";
    params.allowFullScreen = "true";
    params.allowScriptAccess = "always";
    
    var attributes = {};
    
    var self = this;
    
    swfobject.embedSWF(
        "srs_player/release/srs_player.swf", this.container,
        this.width, this.height,
        "11.1", "js/AdobeFlashPlayerInstall.swf",
        flashvars, params, attributes,
        function(callbackObj){
            self.callbackObj = callbackObj;
        }
    );
    
    return this;
}
SrsPlayer.prototype.play = function() {
    return this.callbackObj.ref.__play(this.stream_url, this.width, this.height, this.buffer_time);
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
    return this.callbackObj.ref.__stop();
}
SrsPlayer.prototype.pause = function() {
    return this.callbackObj.ref.__pause();
}
SrsPlayer.prototype.resume = function() {
    return this.callbackObj.ref.__resume();
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
    return this.callbackObj.ref.__dar(num, den);
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
    return this.callbackObj.ref.__set_fs(refer, percent);
}
SrsPlayer.prototype.on_player_ready = function() {
    return this.play();
}
SrsPlayer.prototype.on_player_metadata = function(metadata) {
    return 0;
}
function __srs_on_player_ready(id) {
    for (var i = 0; i < SrsPlayer.__players.length; i++) {
        var player = SrsPlayer.__players[i];
        
        if (player.id != id) {
            continue;
        }
        
        return player.on_player_ready();
    }
    
    throw new Error("player not found. id=" + id);
}
function __srs_on_player_metadata(id, metadata) {
    for (var i = 0; i < SrsPlayer.__players.length; i++) {
        var player = SrsPlayer.__players[i];
        
        if (player.id != id) {
            continue;
        }
        
        // user may override the on_player_metadata, 
        // so set the data before invoke it.
        player.metadata = metadata;
        
        return player.on_player_metadata(metadata);
    }
    
    throw new Error("player not found. id=" + id);
}
