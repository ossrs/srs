
// to query the swf anti cache.
function srs_get_version_code() { return "1.33"; }

/**
* player specified size.
*/
function srs_get_player_modal() { return 740; }
function srs_get_player_width() { return srs_get_player_modal() - 30; }
function srs_get_player_height() { return srs_get_player_width() * 9 / 19; }

/**
* update the navigator, add same query string.
*/
function update_nav() {
    $("#srs_index").attr("href", "index.html" + window.location.search);
    $("#nav_srs_player").attr("href", "srs_player.html" + window.location.search);
    $("#nav_rtc_player").attr("href", "rtc_player.html" + window.location.search);
    $("#nav_rtc_publisher").attr("href", "rtc_publisher.html" + window.location.search);
    $("#nav_srs_publisher").attr("href", "srs_publisher.html" + window.location.search);
    $("#nav_srs_chat").attr("href", "srs_chat.html" + window.location.search);
    $("#nav_srs_bwt").attr("href", "srs_bwt.html" + window.location.search);
    $("#nav_vlc").attr("href", "vlc.html" + window.location.search);
}

// Special extra params, such as auth_key.
function user_extra_params(query, params) {
    var queries = params || [];

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

function is_default_port(schema, port) {
    return (schema === 'http' && port === 80)
        || (schema === 'https' && port === 443)
        || (schema === 'webrtc' && port === 1985)
        || (schema === 'rtmp' && port === 1935);
}

/**
@param server the ip of server. default to window.location.hostname
@param vhost the vhost of HTTP-FLV. default to window.location.hostname
@param port the port of HTTP-FLV. default to 1935
@param app the app of HTTP-FLV. default to live.
@param stream the stream of HTTP-FLV. default to livestream.flv
*/
function build_default_flv_url() {
    var query = parse_query_string();

    var schema = (!query.schema)? "http":query.schema;
    var server = (!query.server)? window.location.hostname:query.server;
    var port = (!query.port)? (schema==="http"? 8080:1935) : Number(query.port);
    var vhost = (!query.vhost)? window.location.hostname:query.vhost;
    var app = (!query.app)? "live":query.app;
    var stream = (!query.stream)? "livestream.flv":query.stream;

    var queries = [];
    if (server !== vhost && vhost !== "__defaultVhost__") {
        queries.push("vhost=" + vhost);
    }
    queries = user_extra_params(query, queries);

    var uri = schema + "://" + server;
    if (!is_default_port(schema, port)) {
        uri += ":" + port;
    }
    uri += "/" + app + "/" + stream + "?" + queries.join('&');
    while (uri.indexOf("?") === uri.length - 1) {
        uri = uri.substr(0, uri.length - 1);
    }

    return uri;
}

function build_default_rtc_url(query) {
    // The format for query string to overwrite configs of server.
    console.log('?eip=x.x.x.x to overwrite candidate. 覆盖服务器candidate(外网IP)配置');

    var server = (!query.server)? window.location.hostname:query.server;
    var vhost = (!query.vhost)? window.location.hostname:query.vhost;
    var app = (!query.app)? "live":query.app;
    var stream = (!query.stream)? "livestream":query.stream;
    var api = query.api? ':'+query.api : '';

    var queries = [];
    if (server !== vhost && vhost !== "__defaultVhost__") {
        queries.push("vhost=" + vhost);
    }
    if (query.schema && window.location.protocol !== query.schema + ':') {
        queries.push('schema=' + query.schema);
    }
    queries = user_extra_params(query, queries);

    var uri = "webrtc://" + server + api + "/" + app + "/" + stream + "?" + queries.join('&');
    while (uri.lastIndexOf("?") === uri.length - 1) {
        uri = uri.substr(0, uri.length - 1);
    }

    return uri;
};

/**
* initialize the page.
* @param flv_url the div id contains the flv stream url to play
* @param hls_url the div id contains the hls stream url to play
* @param modal_player the div id contains the modal player
*/
function srs_init_flv(flv_url, modal_player) {
    update_nav();
    if (flv_url) {
        $(flv_url).val(build_default_flv_url());
    }
    if (modal_player) {
        $(modal_player).width(srs_get_player_modal() + "px");
        $(modal_player).css("margin-left", "-" + srs_get_player_modal() / 2 +"px");
    }
}
function srs_init_rtc(id, query) {
    update_nav();
    $(id).val(build_default_rtc_url(query));
}
