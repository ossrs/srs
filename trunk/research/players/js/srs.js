function update_nav() {
    $("#nav_srs_player").attr("href", "srs_player.html" + window.location.search);
    $("#nav_srs_publisher").attr("href", "srs_publisher.html" + window.location.search);
    $("#nav_srs_bwt").attr("href", "srs_bwt.html" + window.location.search);
    $("#nav_jwplayer6").attr("href", "jwplayer6.html" + window.location.search);
    $("#nav_osmf").attr("href", "osmf.html" + window.location.search);
    $("#nav_vlc").attr("href", "vlc.html" + window.location.search);
}

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

function build_default_url() {
    var query = parse_query_string();
    
    var schema = (query.schema == undefined)? "rtmp":query.schema;
    var port = (query.port == undefined)? 1935:query.port;
    var vhost = (query.vhost == undefined)? window.location.hostname:query.vhost;
    var app = (query.app == undefined)? "live":query.app;
    var stream = (query.stream == undefined)? "livestream":query.stream;
    
   return schema + "://" + vhost + ":" + port + "/" + app + "/" + stream;
}

function srs_init(url_obj) {
    update_nav();
    
    if (url_obj) {
        $(url_obj).val(build_default_url());
    }
}
