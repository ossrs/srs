/**
* the SrsPlayer object.
* @param container the html container id.
* @param width a float value specifies the width of player.
* @param height a float value specifies the height of player.
* @param private_object [optional] an object that used as private object, 
*       for example, the logic chat object which owner this player.
*/
function SrsPlayer(container, width, height, private_object) {
    if (!SrsPlayer.__id) {
        SrsPlayer.__id = 100;
    }
    if (!SrsPlayer.__players) {
        SrsPlayer.__players = [];
    }
    
    SrsPlayer.__players.push(this);
    
    this.private_object = private_object;
    this.container = container;
    this.width = width;
    this.height = height;
    this.id = SrsPlayer.__id++;
    this.stream_url = null;
    this.buffer_time = 0.8; // default to 0.8
    this.volume = 1.0; // default to 100%
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
        "11.1.0", "js/AdobeFlashPlayerInstall.swf",
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
* @param volume the volume, 0 is mute, 1 is 100%, 2 is 200%.
*/
SrsPlayer.prototype.play = function(url, volume) {
    this.stop();
    SrsPlayer.__players.push(this);
    
    if (url) {
        this.stream_url = url;
    }
    
    // volume maybe 0, so never use if(volume) to check its value.
    if (volume != null && volume != undefined) {
        this.volume = volume;
    }
    
    this.callbackObj.ref.__play(this.stream_url, this.width, this.height, this.buffer_time, this.volume);
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
* to set the DAR, for example, DAR=16:9 where num=16,den=9.
* @param num, for example, 16. 
*       use metadata width if 0.
*       use user specified width if -1.
* @param den, for example, 9. 
*       use metadata height if 0.
*       use user specified height if -1.
*/
SrsPlayer.prototype.set_dar = function(num, den) {
    this.callbackObj.ref.__set_dar(num, den);
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
