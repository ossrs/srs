//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

// to query the swf anti cache.
function srs_get_version_code() { return "1.33"; }

/**
* player specified size.
*/
function srs_get_player_modal() { return 740; }
function srs_get_player_width() { return srs_get_player_modal() - 30; }
function srs_get_player_height() { return srs_get_player_width() * 9 / 19; }

// get the default vhost for players.
function srs_get_player_vhost() { return "players"; }
// the api server port, for chat room.
function srs_get_api_server_port() { return 8085; }
// the srs http server port
function srs_get_srs_http_server_port() { return 8080; }

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

/**
* update the navigator, add same query string.
*/
function update_nav() {
    $("#srs_index").attr("href", "index.html" + window.location.search);
    $("#nav_srs_player").attr("href", "srs_player.html" + window.location.search);
    $("#nav_rtc_player").attr("href", "rtc_player.html" + window.location.search);
    $("#nav_srs_publisher").attr("href", "srs_publisher.html" + window.location.search);
    $("#nav_srs_chat").attr("href", "srs_chat.html" + window.location.search);
    $("#nav_srs_bwt").attr("href", "srs_bwt.html" + window.location.search);
    $("#nav_jwplayer6").attr("href", "jwplayer6.html" + window.location.search);
    $("#nav_osmf").attr("href", "osmf.html" + window.location.search);
    $("#nav_vlc").attr("href", "vlc.html" + window.location.search);
}

// Special extra params, such as auth_key.
function user_extra_params(query, params) {
    var queries = params || [];
    var server = (query.server == undefined)? window.location.hostname:query.server;
    var vhost = (query.vhost == undefined)? window.location.hostname:query.vhost;

    // Note that ossrs.net provides only web service,
    // that is migrating to r.ossrs.net
    if (vhost == "ossrs.net") {
        vhost = "r.ossrs.net";
    }
    if (server == "ossrs.net") {
        server = "r.ossrs.net";
    }

    for (var key in query.user_query) {
        if (key === 'app' || key === 'autostart' || key === 'dir'
            || key === 'filename' || key === 'host' || key === 'hostname'
            || key === 'http_port' || key === 'pathname' || key === 'port'
            || key === 'server' || key === 'stream' || key === 'buffer'
            || key === 'schema' || key === 'vhost' || key === 'api'
        ) {
            continue;
        }

        if (query[key]) {
            queries.push(key + '=' + query[key]);
        }
    }

    return queries;
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

    var schema = (!query.schema)? "rtmp":query.schema;
    var server = (!query.server)? window.location.hostname:query.server;
    var port = (!query.port)? schema=="http"?80:1935:query.port;
    var vhost = (!query.vhost)? window.location.hostname:query.vhost;
    var app = (!query.app)? "live":query.app;
    var stream = (!query.stream)? "livestream":query.stream;

    // Note that ossrs.net provides only web service,
    // that is migrating to r.ossrs.net
    if (vhost == "ossrs.net") {
        vhost = "r.ossrs.net";
    }
    if (server == "ossrs.net") {
        server = "r.ossrs.net";
    }

    var queries = [];
    if (server != vhost && vhost != "__defaultVhost__") {
        queries.push("vhost=" + vhost);
    }
    queries = user_extra_params(query, queries);

    var uri = schema + "://" + server + ":" + port + "/" + app + "/" + stream + "?" + queries.join('&');
    while (uri.indexOf("?") == uri.length - 1) {
        uri = uri.substr(0, uri.length - 1);
    }

    return uri;
}
// for the chat to init the publish url.
function build_default_publish_rtmp_url() {
    var query = parse_query_string();

    var schema = (!query.schema)? "rtmp":query.schema;
    var server = (!query.server)? window.location.hostname:query.server;
    var port = (!query.port)? schema=="http"?80:1935:query.port;
    var vhost = (!query.vhost)? window.location.hostname:query.vhost;
    var app = (!query.app)? "live":query.app;
    var stream = (!query.stream)? "demo":query.stream;

    // Note that ossrs.net provides only web service,
    // that is migrating to r.ossrs.net
    if (vhost == "ossrs.net") {
        vhost = "r.ossrs.net";
    }
    if (server == "ossrs.net") {
        server = "r.ossrs.net";
    }

    var queries = [];
    if (server != vhost && vhost != "__defaultVhost__") {
        queries.push("vhost=" + vhost);
    }
    if (query.shp_identify) {
        queries.push("shp_identify=" + query.shp_identify);
    }

    var uri = schema + "://" + server + ":" + port + "/" + app + "/" + stream + "?" + queries.join('&');
    while (uri.indexOf("?") == uri.length - 1) {
        uri = uri.substr(0, uri.length - 1);
    }

    return uri;
}
// for the bandwidth tool to init page
function build_default_bandwidth_rtmp_url() {
    var query = parse_query_string();

    var server = (!query.server)? window.location.hostname:query.server;
    var port = (!query.port)? 1935:query.port;
    var vhost = "bandcheck.srs.com";
    var app = (!query.app)? "app":query.app;
    var key = (!query.key)? "35c9b402c12a7246868752e2878f7e0e":query.key;

    // Note that ossrs.net provides only web service,
    // that is migrating to r.ossrs.net
    if (vhost == "ossrs.net") {
        vhost = "r.ossrs.net";
    }
    if (server == "ossrs.net") {
        server = "r.ossrs.net";
    }

    return "rtmp://" + server + ":" + port + "/" + app + "?key=" + key + "&vhost=" + vhost;
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

    // Note that ossrs.net provides only web service,
    // that is migrating to r.ossrs.net
    if (query.hls_vhost == "ossrs.net") {
        query.hls_vhost = "r.ossrs.net";
    }

    // for http, use hls_vhost to override server if specified.
    var server = window.location.hostname;
    if (query.server != undefined) {
        server = query.server;
    } else if (query.hls_vhost != undefined) {
        server = query.hls_vhost;
    }
    
    var port = (!query.hls_port)? window.location.port:query.hls_port;
    var app = (!query.app)? "live":query.app;
    var stream = (!query.stream)? "demo":query.stream;

    if (!port) {
        port = 8080;
    }
    
    if (stream.indexOf(".flv") >= 0) {
        return "http://" + server + ":" + port + "/" + app + "/" + stream;
    }
    return "http://" + server + ":" + port + "/" + app + "/" + stream + ".m3u8";
}

function build_default_rtc_url(query) {
    // Use target to overwrite server, vhost and eip.
    console.log('?target=x.x.x.x to overwrite server, vhost and eip.');
    if (query.target) {
        query.server = query.vhost = query.eip = query.target;
        query.user_query.eip = query.target;
        delete query.target;
    }

    var server = (!query.server)? window.location.hostname:query.server;
    var vhost = (!query.vhost)? window.location.hostname:query.vhost;
    var app = (!query.app)? "live":query.app;
    var stream = (!query.stream)? "livestream":query.stream;
    var api = query.api? ':'+query.api : '';

    // Note that ossrs.net provides only web service,
    // that is migrating to r.ossrs.net
    if (vhost == "ossrs.net") {
        vhost = "r.ossrs.net";
    }
    if (server == "ossrs.net") {
        server = "r.ossrs.net";
    }

    var queries = [];
    if (server != vhost && vhost != "__defaultVhost__") {
        queries.push("vhost=" + vhost);
    }
    queries = user_extra_params(query, queries);

    var uri = "webrtc://" + server + api + "/" + app + "/" + stream + "?" + queries.join('&');
    while (uri.lastIndexOf("?") == uri.length - 1) {
        uri = uri.substr(0, uri.length - 1);
    }

    return uri;
};

/**
* initialize the page.
* @param rtmp_url the div id contains the rtmp stream url to play
* @param hls_url the div id contains the hls stream url to play
* @param modal_player the div id contains the modal player
*/
function srs_init_rtmp(rtmp_url, modal_player) {
    srs_init(rtmp_url, null, modal_player);
}
function srs_init_hls(hls_url, modal_player) {
    srs_init(null, hls_url, modal_player);
}
function srs_init_rtc(id, query) {
    update_nav();
    $(id).val(build_default_rtc_url(query));
}
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
// for bw to init url
// url: scheme://host:port/path?query#fragment
function srs_init_bwt(rtmp_url, hls_url) {
    update_nav();
    
    if (rtmp_url) {
        $(rtmp_url).val(build_default_bandwidth_rtmp_url());
    }
}

// check whether can republish
function srs_can_republish() {
    var browser = get_browser_agents();
    
    if (browser.Chrome || browser.Firefox) {
        return true;
    }
    
    if (browser.MSIE || browser.QQBrowser) {
        return false;
    }
    
    return false;
}

// without default values set.
function srs_initialize_codec_page(
    cameras, microphones,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate,
    sl_acodec
) {
    $(sl_cameras).empty();
    for (var i = 0; i < cameras.length; i++) {
        $(sl_cameras).append("<option value='" + i + "'>" + cameras[i] + "</option");
    }
    // optional: select the except matches
    matchs = ["virtual"];
    for (var i = 0; i < cameras.length; i++) {
        for (var j = 0; j < matchs.length; j++) {
            if (cameras[i].toLowerCase().indexOf(matchs[j]) == -1) {
                $(sl_cameras + " option[value='" + i + "']").attr("selected", true);
                break;
            }
        }
        if (j < matchs.length) {
            break;
        }
    }
    // optional: select the first matched.
    matchs = ["truevision", "integrated"];
    for (var i = 0; i < cameras.length; i++) {
        for (var j = 0; j < matchs.length; j++) {
            if (cameras[i].toLowerCase().indexOf(matchs[j]) >= 0) {
                $(sl_cameras + " option[value='" + i + "']").attr("selected", true);
                break;
            }
        }
        if (j < matchs.length) {
            break;
        }
    }
    
    $(sl_microphones).empty();
    for (var i = 0; i < microphones.length; i++) {
        $(sl_microphones).append("<option value='" + i + "'>" + microphones[i] + "</option");
    }
    // optional: select the except matches
    matchs = ["default"];
    for (var i = 0; i < microphones.length; i++) {
        for (var j = 0; j < matchs.length; j++) {
            if (microphones[i].toLowerCase().indexOf(matchs[j]) == -1) {
                $(sl_microphones + " option[value='" + i + "']").attr("selected", true);
                break;
            }
        }
        if (j < matchs.length) {
            break;
        }
    }
    // optional: select the first matched.
    matchs = ["realtek", "内置式麦克风"];
    for (var i = 0; i < microphones.length; i++) {
        for (var j = 0; j < matchs.length; j++) {
            if (microphones[i].toLowerCase().indexOf(matchs[j]) >= 0) {
                $(sl_microphones + " option[value='" + i + "']").attr("selected", true);
                break;
            }
        }
        if (j < matchs.length) {
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
        "352x288", "480x360", "640x480", "720x480", "720x576", "800x600", 
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
    
    $(sl_acodec).empty();
    var bitrates = ["speex", "nellymoser", "pcma", "pcmu"];
    for (i = 0; i < bitrates.length; i++) {
        $(sl_acodec).append("<option value='" + bitrates[i] + "'>" + bitrates[i] + "</option");
    }
}
/**
* when publisher ready, init the page elements.
*/
function srs_publisher_initialize_page(
    cameras, microphones,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate,
    sl_acodec
) {
    srs_initialize_codec_page(
        cameras, microphones,
        sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate,
        sl_acodec
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
    //    "352x288", "480x360", "640x480", "720x480", "720x576", "800x600", 
    //    "1024x768", "1280x720", "1360x768", "1920x1080"];
    $(sl_size + " option[value='640x480']").attr("selected", true);
    
    //var fpses = ["5", "10", "15", "20", "24", "25", "29.97", "30"];
    $(sl_fps + " option[value='20']").attr("selected", true);
    
    //var bitrates = ["50", "200", "350", "500", "650", "800", 
    //    "950", "1000", "1200", "1500", "1800", "2000", "3000", "5000"];
    $(sl_bitrate + " option[value='500']").attr("selected", true);
    
    // speex
    $(sl_acodec + " option[value='speex']").attr("selected", true);
}
/**
* for chat, use low latecy settings.
*/
function srs_chat_initialize_page(
    cameras, microphones,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate,
    sl_acodec
) {
    srs_initialize_codec_page(
        cameras, microphones,
        sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate,
        sl_acodec
    );
    
    //var profiles = ["baseline", "main"];
    $(sl_profile + " option[value='baseline']").attr("selected", true);
    
    //var levels = ["1", "1b", "1.1", "1.2", "1.3", 
    //    "2", "2.1", "2.2", "3", "3.1", "3.2", "4", "4.1", "4.2", "5", "5.1"];
    $(sl_level + " option[value='3.1']").attr("selected", true);
    
    //var gops = ["0.3", "0.5", "1", "2", "3", "4", 
    //    "5", "6", "7", "8", "9", "10", "15", "20"];
    $(sl_gop + " option[value='2']").attr("selected", true);
    
    //var sizes = ["176x144", "320x240", "352x240", 
    //    "352x288", "480x360", "640x480", "720x480", "720x576", "800x600", 
    //    "1024x768", "1280x720", "1360x768", "1920x1080"];
    $(sl_size + " option[value='480x360']").attr("selected", true);
    
    //var fpses = ["5", "10", "15", "20", "24", "25", "29.97", "30"];
    $(sl_fps + " option[value='15']").attr("selected", true);
    
    //var bitrates = ["50", "200", "350", "500", "650", "800", 
    //    "950", "1000", "1200", "1500", "1800", "2000", "3000", "5000"];
    $(sl_bitrate + " option[value='350']").attr("selected", true);
    
    // speex
    $(sl_acodec + " option[value='speex']").attr("selected", true);
}
/**
* get the vcodec and acodec.
*/
function srs_publiser_get_codec(
    vcodec, acodec,
    sl_cameras, sl_microphones, sl_vcodec, sl_profile, sl_level, sl_gop, sl_size, sl_fps, sl_bitrate,
    sl_acodec
) {
    acodec.codec       = $(sl_acodec).val();
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
