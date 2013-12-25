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
