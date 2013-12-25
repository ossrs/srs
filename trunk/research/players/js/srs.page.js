//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

/**
* player specified size.
*/
function srs_get_player_modal() { return 740; }
function srs_get_player_width() { return srs_get_player_modal() - 30; }
function srs_get_player_height() { return srs_get_player_width() * 9 / 19; }

// to query the swf anti cache.
function srs_get_version_code() { return "1.13"; }
// get the default vhost for players.
function srs_get_player_vhost() { return "players"; }
// the api server port, for chat room.
function srs_get_api_server_port() { return 8085; }
// get the stream published to vhost,
// generally we need to transcode the stream to support HLS and filters.
// for example, src_vhost is "players", we transcode stream to vhost "players_pub".
// if not equals to the player vhost, return the orignal vhost.
function srs_get_player_publish_vhost(src_vhost) { return (src_vhost != srs_get_player_vhost())? src_vhost:(src_vhost + "_pub"); }
// for chat, use rtmp only vhost, low latecy, without gop cache.
function srs_get_player_chat_vhost(src_vhost) { return (src_vhost != srs_get_player_vhost())? src_vhost:(src_vhost + "_pub_rtmp"); }

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

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
        vhost = srs_get_player_chat_vhost(vhost);
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

// without default values set.
function srs_initialize_codec_page(
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
    // optional: select the first "integrated" signed.
    for (var i = 0; i < cameras.length; i++) {
        if (cameras[i].toLowerCase().indexOf("integrated") >= 0) {
            $(sl_cameras + " option[value='" + i + "']").attr("selected", true);
            break;
        }
    }
    
    $(sl_microphones).empty();
    for (var i = 0; i < microphones.length; i++) {
        $(sl_microphones).append("<option value='" + i + "'>" + microphones[i] + "</option");
    }
    // optional: select the first no "default" signed.
    for (var i = 0; i < microphones.length; i++) {
        if (microphones[i].toLowerCase().indexOf("default") == -1) {
            $(sl_microphones + " option[value='" + i + "']").attr("selected", true);
            break;
        }
    }
    // optional: select the first "realtek" signed.
    for (var i = 0; i < microphones.length; i++) {
        if (microphones[i].toLowerCase().indexOf("realtek") >= 0) {
            $(sl_microphones + " option[value='" + i + "']").attr("selected", true);
            break;
        }
    }
    
    $(sl_vcodec).empty();
    var vcodecs = ["h264", "vp6"];
    vcodecs = ["h264"]; // h264 only.
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
    
    $(sl_gop).empty();
    var gops = ["0.3", "0.5", "1", "2", "3", "4", 
        "5", "6", "7", "8", "9", "10", "15", "20"];
    for (var i = 0; i < gops.length; i++) {
        $(sl_gop).append("<option value='" + gops[i] + "'>" + gops[i] + "秒</option");
    }
    
    $(sl_size).empty();
    var sizes = ["176x144", "320x240", "352x240", 
        "352x288", "460x240", "640x480", "720x480", "720x576", "800x600", 
        "1024x768", "1280x720", "1360x768", "1920x1080"];
    for (i = 0; i < sizes.length; i++) {
        $(sl_size).append("<option value='" + sizes[i] + "'>" + sizes[i] + "</option");
    }
    
    $(sl_fps).empty();
    var fpses = ["5", "10", "15", "20", "24", "25", "29.97", "30"];
    for (i = 0; i < fpses.length; i++) {
        $(sl_fps).append("<option value='" + fpses[i] + "'>" + Number(fpses[i]).toFixed(2) + " 帧/秒</option");
    }
    
    $(sl_bitrate).empty();
    var bitrates = ["50", "200", "350", "500", "650", "800", 
        "950", "1000", "1200", "1500", "1800", "2000", "3000", "5000"];
    for (i = 0; i < bitrates.length; i++) {
        $(sl_bitrate).append("<option value='" + bitrates[i] + "'>" + bitrates[i] + " kbps</option");
    }
}
/**
* when publisher ready, init the page elements.
*/
function srs_publisher_initialize_page(
    cameras, microphones,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate
) {
    srs_initialize_codec_page(
        cameras, microphones,
        sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate
    );
    
    //var profiles = ["baseline", "main"];
    $(sl_profile + " option[value='main']").attr("selected", true);
    
    //var levels = ["1", "1b", "1.1", "1.2", "1.3", 
    //    "2", "2.1", "2.2", "3", "3.1", "3.2", "4", "4.1", "4.2", "5", "5.1"];
    $(sl_level + " option[value='4.1']").attr("selected", true);
    
    //var gops = ["0.3", "0.5", "1", "2", "3", "4", 
    //    "5", "6", "7", "8", "9", "10", "15", "20"];
    $(sl_gop + " option[value='10']").attr("selected", true);
    
    //var sizes = ["176x144", "320x240", "352x240", 
    //    "352x288", "460x240", "640x480", "720x480", "720x576", "800x600", 
    //    "1024x768", "1280x720", "1360x768", "1920x1080"];
    $(sl_size + " option[value='640x480']").attr("selected", true);
    
    //var fpses = ["5", "10", "15", "20", "24", "25", "29.97", "30"];
    $(sl_fps + " option[value='20']").attr("selected", true);
    
    //var bitrates = ["50", "200", "350", "500", "650", "800", 
    //    "950", "1000", "1200", "1500", "1800", "2000", "3000", "5000"];
    $(sl_bitrate + " option[value='500']").attr("selected", true);
}
/**
* for chat, use low latecy settings.
*/
function srs_chat_initialize_page(
    cameras, microphones,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate
) {
    srs_initialize_codec_page(
        cameras, microphones,
        sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate
    );
    
    //var profiles = ["baseline", "main"];
    $(sl_profile + " option[value='baseline']").attr("selected", true);
    
    //var levels = ["1", "1b", "1.1", "1.2", "1.3", 
    //    "2", "2.1", "2.2", "3", "3.1", "3.2", "4", "4.1", "4.2", "5", "5.1"];
    $(sl_level + " option[value='2.1']").attr("selected", true);
    
    //var gops = ["0.3", "0.5", "1", "2", "3", "4", 
    //    "5", "6", "7", "8", "9", "10", "15", "20"];
    $(sl_gop + " option[value='1']").attr("selected", true);
    
    //var sizes = ["176x144", "320x240", "352x240", 
    //    "352x288", "460x240", "640x480", "720x480", "720x576", "800x600", 
    //    "1024x768", "1280x720", "1360x768", "1920x1080"];
    $(sl_size + " option[value='640x480']").attr("selected", true);
    
    //var fpses = ["5", "10", "15", "20", "24", "25", "29.97", "30"];
    $(sl_fps + " option[value='15']").attr("selected", true);
    
    //var bitrates = ["50", "200", "350", "500", "650", "800", 
    //    "950", "1000", "1200", "1500", "1800", "2000", "3000", "5000"];
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
