/*
The MIT License (MIT)

Copyright (c) 2013-2014 winlin

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
/**
* the SrsBandwidth library for js to do bandwidth test.
* @param container the html container id.
* @param width a float value specifies the width of bandwidth.
* @param height a float value specifies the height of bandwidth.
* @param private_object [optional] an object that used as private object, 
*       for example, the logic chat object which owner this bandwidth.
* Usage:
    var bandwidth = new SrsBandwidth("container_id", 100, 1);
    bandwidth.on_bandwidth_ready = function() {
        // auto start check bandwidth when tool is ready.
        this.check_bandwidth(url);
    }
    bandwidth.on_update_progress = function(percent) {
        // console.log(percent + "%");
    }
    bandwidth.on_update_status = function(status) {
        // console.log(status);
    }
    bandwidth.on_srs_info = function(srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip) {
        // console.log(
        //    "server:" + srs_server + ", primary:" + srs_primary + ", authors:" + srs_authors +
        //    ", srs_id:" + srs_id + ", srs_pid:" + srs_pid + ", ip:" + srs_server_ip
        //);
    }
    bandwidth.render("rtmp://dev:1935/app?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com");
* where the HTML page must contains an element:
    <div id="container_id"></div>
* this js will directly erase the container by swfobject.
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
    this.server = "";
    this.completed = false;
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
SrsBandwidth.prototype.render = function(url) {
    if (url) {
        this.stream_url = url;
    }
    
    // embed the flash.
    var flashvars = {};
    flashvars.id = this.id;
    flashvars.on_bandwidth_ready = "__srs_on_bandwidth_ready";
    flashvars.on_update_progress = "__srs_on_update_progress";
    flashvars.on_update_status = "__srs_on_update_status";
    flashvars.on_srs_info = "__srs_on_srs_info";
    flashvars.on_complete = "__srs_on_complete";
    
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
*/
SrsBandwidth.prototype.check_bandwidth = function(url) {
    this.stop();
    SrsBandwidth.__bandwidths.push(this);
    
    // the callback set data.
    this.percent = 0;
    this.status = "";
    this.server = "";
    this.completed = false;
    
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
SrsBandwidth.prototype.on_srs_info = function(srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip) {
}
SrsBandwidth.prototype.on_complete = function(start_time, end_time, play_kbps, publish_kbps, play_bytes, publish_bytes, play_time, publish_time) {
}
SrsBandwidth.prototype.on_error = function(code) {
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
function __srs_on_update_status(id, code, data) {
    var bandwidth = __srs_find_bandwidth(id);
    
    var status = "";
    switch(code){
        case "NetConnection.Connect.Failed":
            if (bandwidth.completed) {
                return;
            }
            bandwidth.on_error(code);
            status = "连接服务器失败！";
            break;
        case "NetConnection.Connect.Rejected":
            if (bandwidth.completed) {
                return;
            }
            bandwidth.completed = true;
            bandwidth.on_update_progress(100);
            bandwidth.on_error(code);
            status = "服务器拒绝连接，测速过于频繁！";
            break;
        case "NetConnection.Connect.Success":
            status = "连接服务器成功!";
            break;
        case "NetConnection.Connect.Closed":
            if (bandwidth.completed) {
                return;
            }
            bandwidth.on_error(code);
            status = "连接已断开!";
            break;
        case "srs.bwtc.play.start":
            status = "开始测试下行带宽";
            break;
        case "srs.bwtc.play.stop":
            bandwidth.completed = true;
            status = "下行带宽测试完毕，" + data + "kbps，开始测试上行带宽。";
            break;
        default:
            return;
    }
    
    bandwidth.status = status;
    bandwidth.on_update_status(status);
}
function __srs_on_srs_info(id, srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip) {
    var bandwidth = __srs_find_bandwidth(id);
    bandwidth.status = status;
    bandwidth.server = srs_server_ip;
    bandwidth.on_srs_info(srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip);
}
function __srs_on_complete(id, start_time, end_time, play_kbps, publish_kbps, play_bytes, publish_bytes, play_time, publish_time) {
    var bandwidth = __srs_find_bandwidth(id);
    
    var status = "检测结束: " + bandwidth.server + " 上行: " + publish_kbps + " kbps" + " 下行: " + play_kbps + " kbps"
                + " 测试时间: " + Number((end_time - start_time) / 1000).toFixed(1) + " 秒";
    bandwidth.status = status;
    bandwidth.on_update_status(status);
    
    bandwidth.on_complete(start_time, end_time, play_kbps, publish_kbps, play_bytes, publish_bytes, play_time, publish_time);
}