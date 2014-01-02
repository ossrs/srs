<<<<<<< HEAD
// for bw to init url
// url: scheme://host:port/path?query#fragment
function srs_init_bwt(rtmp_url, hls_url) {
    update_nav();

    if (rtmp_url) {
		//var query = parse_query_string();
		var search_filed = String(window.location.search).replace(" ", "").split("?")[1];
        $(rtmp_url).val("rtmp://" + window.location.host + ":" + 1935 + "/app?" + search_filed);
    }
    if (hls_url) {
        $(hls_url).val(build_default_hls_url());
    }
}

function srs_bwt_check_url(url) {
	if (url.indexOf("key") != -1 && url.indexOf("vhost") != -1) {
		return true;
	}
	
	return false;
}

function srs_bwt_build_default_url() {
	var url_default = "rtmp://" + window.location.host + ":" + 1935 + "/app?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com";
	return url_default;
=======
/**
* the SrsBandwidth object.
* @param container the html container id.
* @param width a float value specifies the width of bandwidth.
* @param height a float value specifies the height of bandwidth.
* @param private_object [optional] an object that used as private object, 
*       for example, the logic chat object which owner this bandwidth.
*/
function SrsBandwidth(container, width, height, private_object) {
    if (!SrsBandwidth.__id) {
        SrsBandwidth.__id = 100;
    }
    if (!SrsBandwidth.__bandwidths) {
        SrsBandwidth.__bandwidths = [];
    }
    
    SrsBandwidth.__bandwidths.push(this);
    
    this.private_object = private_object;
    this.container = container;
    this.width = width;
    this.height = height;
    this.id = SrsBandwidth.__id++;
    this.stream_url = null;
    this.callbackObj = null;
    
    // the callback set data.
    this.percent = 0;
    this.status = "";
}
/**
* user can set some callback, then start the bandwidth.
* @param url the bandwidth test url.
* callbacks:
*      on_bandwidth_ready():void, when srs bandwidth ready, user can play.
*      on_update_progress(percent:Number):void, when srs bandwidth update the progress.
*           percent:Number 100 means 100%.
*      on_update_status(status:String):void, when srs bandwidth update the status.
*           status:String the human readable status text.
*/
SrsBandwidth.prototype.start = function(url) {
    if (url) {
        this.stream_url = url;
    }
    
    // embed the flash.
    var flashvars = {};
    flashvars.id = this.id;
    flashvars.on_bandwidth_ready = "__srs_on_bandwidth_ready";
    flashvars.on_update_progress = "__srs_on_update_progress";
    flashvars.on_update_status = "__srs_on_update_status";
    
    var params = {};
    params.wmode = "opaque";
    params.allowFullScreen = "true";
    params.allowScriptAccess = "always";
    
    var attributes = {};
    
    var self = this;
    
    swfobject.embedSWF(
        "srs_bwt/release/srs_bwt.swf?_version="+srs_get_version_code(), 
        this.container,
        this.width, this.height,
        "11.1.0", "js/AdobeFlashbandwidthInstall.swf",
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
SrsBandwidth.prototype.check_bandwidth = function(url) {
    this.stop();
    SrsBandwidth.__bandwidths.push(this);
    
    if (url) {
        this.stream_url = url;
    }
    
    this.callbackObj.ref.__check_bandwidth(this.stream_url);
}
SrsBandwidth.prototype.stop = function(url) {
    for (var i = 0; i < SrsBandwidth.__bandwidths.length; i++) {
        var bandwidth = SrsBandwidth.__bandwidths[i];
        
        if (bandwidth.id != this.id) {
            continue;
        }
        
        SrsBandwidth.__bandwidths.splice(i, 1);
        break;
    }
    
    this.callbackObj.ref.__stop();
}
SrsBandwidth.prototype.on_bandwidth_ready = function() {
}
SrsBandwidth.prototype.on_update_progress = function(percent) {
}
SrsBandwidth.prototype.on_update_status = function(status) {
}
function __srs_find_bandwidth(id) {
    for (var i = 0; i < SrsBandwidth.__bandwidths.length; i++) {
        var bandwidth = SrsBandwidth.__bandwidths[i];
        
        if (bandwidth.id != id) {
            continue;
        }
        
        return bandwidth;
    }
    
    throw new Error("bandwidth not found. id=" + id);
}
function __srs_on_bandwidth_ready(id) {
    var bandwidth = __srs_find_bandwidth(id);
    bandwidth.on_bandwidth_ready();
}
function __srs_on_update_progress(id, percent) {
    var bandwidth = __srs_find_bandwidth(id);
    bandwidth.percent = percent;
    bandwidth.on_update_progress(percent);
}
function __srs_on_update_status(id, status) {
    var bandwidth = __srs_find_bandwidth(id);
    bandwidth.status = status;
    bandwidth.on_update_status(status);
>>>>>>> upstream/master
}