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

/**
* get the agent.
* @return an object specifies some browser.
*   for example, get_browser_agents().MSIE
*/
function get_browser_agents() {
    var agent = navigator.userAgent;
    
    /**
    WindowsPC platform, Win7:
        chrome 31.0.1650.63:
            Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 
            (KHTML, like Gecko) Chrome/31.0.1650.63 Safari/537.36
        firefox 23.0.1:
            Mozilla/5.0 (Windows NT 6.1; WOW64; rv:23.0) Gecko/20100101 
            Firefox/23.0
        safari 5.1.7(7534.57.2):
            Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/534.57.2 
            (KHTML, like Gecko) Version/5.1.7 Safari/534.57.2
        opera 15.0.1147.153:
            Mozilla/5.0 (Windows NT 6.1; WOW64) AppleWebKit/537.36 
            (KHTML, like Gecko) Chrome/28.0.1500.95 Safari/537.36 
            OPR/15.0.1147.153
        360 6.2.1.272: 
            Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; WOW64; 
            Trident/6.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; 
            .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; .NET4.0C; 
            .NET4.0E)
        IE 10.0.9200.16750(update: 10.0.12):
            Mozilla/4.0 (compatible; MSIE 7.0; Windows NT 6.1; WOW64; 
            Trident/6.0; SLCC2; .NET CLR 2.0.50727; .NET CLR 3.5.30729; 
            .NET CLR 3.0.30729; Media Center PC 6.0; InfoPath.2; .NET4.0C; 
            .NET4.0E)
    */
    
    return {
        // platform
        Android: agent.indexOf("Android") != -1,
        Windows: agent.indexOf("Windows") != -1,
        iPhone: agent.indexOf("iPhone") != -1,
        // Windows Browsers
        Chrome: agent.indexOf("Chrome") != -1,
        Firefox: agent.indexOf("Firefox") != -1,
        QQBrowser: agent.indexOf("QQBrowser") != -1,
        MSIE: agent.indexOf("MSIE") != -1, 
        // Android Browsers
        Opera: agent.indexOf("Presto") != -1,
        MQQBrowser: agent.indexOf("MQQBrowser") != -1
    };
}
