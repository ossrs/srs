// winlin.utility.js

/**
 * common utilities
 * depends: jquery1.10
 * https://gitee.com/winlinvip/codes/rpn0c2ewbomj81augzk4y59
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * v 1.0.23
 */

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
 * extends system array, to remove all specified elem.
 * @param arr the array to remove elem from.
 * @param elem the elem to remove.
 * @remark all elem will be removed.
 * for example,
 *      arr = [10, 15, 20, 30, 20, 40]
 *      system_array_remove(arr, 10) // arr=[15, 20, 30, 20, 40]
 *      system_array_remove(arr, 20) // arr=[15, 30, 40]
 */
function system_array_remove(arr, elem) {
    if (!arr) {
        return;
    }

    var removed = true;
    var i = 0;
    while (removed) {
        removed = false;
        for (; i < arr.length; i++) {
            if (elem == arr[i]) {
                arr.splice(i, 1);
                removed = true;
                break;
            }
        }
    }
}

/**
 * whether the array contains specified element.
 * @param arr the array to find.
 * @param elem_or_function the element value or compare function.
 * @returns true contains elem; otherwise false.
 * for example,
 *      arr = [10, 15, 20, 30, 20, 40]
 *      system_array_contains(arr, 10) // true
 *      system_array_contains(arr, 11) // false
 *      system_array_contains(arr, function(elem){return elem == 30;}); // true
 *      system_array_contains(arr, function(elem){return elem == 60;}); // false
 */
function system_array_contains(arr, elem_or_function) {
    return system_array_get(arr, elem_or_function) != null;
}

/**
 * get the specified element from array
 * @param arr the array to find.
 * @param elem_or_function the element value or compare function.
 * @returns the matched elem; otherwise null.
 * for example,
 *      arr = [10, 15, 20, 30, 20, 40]
 *      system_array_get(arr, 10) // 10
 *      system_array_get(arr, 11) // null
 *      system_array_get(arr, function(elem){return elem == 30;}); // 30
 *      system_array_get(arr, function(elem){return elem == 60;}); // null
 */
function system_array_get(arr, elem_or_function) {
    for (var i = 0; i < arr.length; i++) {
        if (typeof elem_or_function == "function") {
            if (elem_or_function(arr[i])) {
                return arr[i];
            }
        } else {
            if (elem_or_function == arr[i]) {
                return arr[i];
            }
        }
    }
    return null;
}

/**
 * to iterate on array.
 * @param arr the array to iterate on.
 * @param pfn the function to apply on it. return false to break loop.
 * for example,
 *      arr = [10, 15, 20, 30, 20, 40]
 *      system_array_foreach(arr, function(elem, index){
 *          console.log('index=' + index + ',elem=' + elem);
 *      });
 * @return true when iterate all elems.
 */
function system_array_foreach(arr, pfn) {
    if (!pfn) {
        return false;
    }

    for (var i = 0; i < arr.length; i++) {
        if (!pfn(arr[i], i)) {
            return false;
        }
    }

    return true;
}

/**
 * whether the str starts with flag.
 */
function system_string_startswith(str, flag) {
    if (typeof flag == "object" && flag.constructor == Array) {
        for (var i = 0; i < flag.length; i++) {
            if (system_string_startswith(str, flag[i])) {
                return true;
            }
        }
    }

    return str && flag && str.length >= flag.length && str.indexOf(flag) == 0;
}

/**
 * whether the str ends with flag.
 */
function system_string_endswith(str, flag) {
    if (typeof flag == "object" && flag.constructor == Array) {
        for (var i = 0; i < flag.length; i++) {
            if (system_string_endswith(str, flag[i])) {
                return true;
            }
        }
    }

    return str && flag && str.length >= flag.length && str.indexOf(flag) == str.length - flag.length;
}

/**
 * trim the start and end of flag in str.
 * @param flag a string to trim.
 */
function system_string_trim(str, flag) {
    if (!flag || !flag.length || typeof flag != "string") {
        return str;
    }

    while (system_string_startswith(str, flag)) {
        str = str.substr(flag.length);
    }

    while (system_string_endswith(str, flag)) {
        str = str.substr(0, str.length - flag.length);
    }

    return str;
}

/**
 * array sort asc, for example:
 * [a, b] in [10, 11, 9]
 * then sort to: [9, 10, 11]
 * Usage, for example:
 obj.data.data.sort(function(a, b){
            return array_sort_asc(a.metadata.meta_id, b.metadata.meta_id);
        });
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * @remark, if need desc, use -1*array_sort_asc(a,b)
 */
function array_sort_asc(elem_a, elem_b) {
    if (elem_a > elem_b) {
        return 1;
    }
    return (elem_a < elem_b)? -1 : 0;
}
function array_sort_desc(elem_a, elem_b) {
    return -1 * array_sort_asc(elem_a, elem_b);
}
function system_array_sort_asc(elem_a, elem_b) {
    return array_sort_asc(elem_a, elem_b);
}
function system_array_sort_desc(elem_a, elem_b) {
    return -1 * array_sort_asc(elem_a, elem_b);
}

/**
 * parse the query string to object.
 * parse the url location object as: host(hostname:http_port), pathname(dir/filename)
 * for example, url http://192.168.1.168:1980/ui/players.html?vhost=player.vhost.com&app=test&stream=livestream
 * parsed to object:
 {
     host        : "192.168.1.168:1980",
     hostname    : "192.168.1.168",
     http_port   : 1980,
     pathname    : "/ui/players.html",
     dir         : "/ui",
     filename    : "/players.html",

     vhost       : "player.vhost.com",
     app         : "test",
     stream      : "livestream"
 }
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 */
function parse_query_string(){
    var obj = {};

    // add the uri object.
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

    // pure user query object.
    obj.user_query = {};

    // parse the query string.
    var query_string = String(window.location.search).replace(" ", "").split("?")[1];
    if(query_string === undefined){
        query_string = String(window.location.hash).replace(" ", "").split("#")[1];
        if(query_string === undefined){
            return obj;
        }
    }

    __fill_query(query_string, obj);

    return obj;
}

function __fill_query(query_string, obj) {
    // pure user query object.
    obj.user_query = {};

    if (query_string.length === 0) {
        return;
    }

    // split again for angularjs.
    if (query_string.indexOf("?") >= 0) {
        query_string = query_string.split("?")[1];
    }

    var queries = query_string.split("&");
    for (var i = 0; i < queries.length; i++) {
        var elem = queries[i];

        var query = elem.split("=");
        obj[query[0]] = query[1];
        obj.user_query[query[0]] = query[1];
    }

    // alias domain for vhost.
    if (obj.domain) {
        obj.vhost = obj.domain;
    }
}

/**
 * parse the rtmp url,
 * for example: rtmp://demo.srs.com:1935/live...vhost...players/livestream
 * @return object {server, port, vhost, app, stream}
 * for exmaple, rtmp_url is rtmp://demo.srs.com:1935/live...vhost...players/livestream
 * parsed to object:
 {
    server: "demo.srs.com",
    port: 1935,
    vhost: "players",
    app: "live",
    stream: "livestream"
 }
 */
function parse_rtmp_url(rtmp_url) {
    // @see: http://stackoverflow.com/questions/10469575/how-to-use-location-object-to-parse-url-without-redirecting-the-page-in-javascri
    var a = document.createElement("a");
    a.href = rtmp_url.replace("rtmp://", "http://")
        .replace("webrtc://", "http://")
        .replace("rtc://", "http://");

    var vhost = a.hostname;
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

    // when vhost equals to server, and server is ip,
    // the vhost is __defaultVhost__
    if (a.hostname === vhost) {
        var re = /^(\d+)\.(\d+)\.(\d+)\.(\d+)$/;
        if (re.test(a.hostname)) {
            vhost = "__defaultVhost__";
        }
    }
    
    // parse the schema
    var schema = "rtmp";
    if (rtmp_url.indexOf("://") > 0) {
        schema = rtmp_url.substr(0, rtmp_url.indexOf("://"));
    }

    var port = a.port;
    if (!port) {
        if (schema === 'http') {
            port = 80;
        } else if (schema === 'https') {
            port = 443;
        } else if (schema === 'rtmp') {
            port = 1935;
        }
    }

    var ret = {
        url: rtmp_url,
        schema: schema,
        server: a.hostname, port: port,
        vhost: vhost, app: app, stream: stream
    };
    __fill_query(a.search, ret);

    // For webrtc API, we use 443 if page is https, or schema specified it.
    if (!ret.port) {
        if (schema === 'webrtc' || schema === 'rtc') {
            if (ret.user_query.schema === 'https') {
                ret.port = 443;
            } else if (window.location.href.indexOf('https://') === 0) {
                ret.port = 443;
            } else {
                // For WebRTC, SRS use 1985 as default API port.
                ret.port = 1985;
            }
        }
    }

    return ret;
}

/**
 * get the agent.
 * @return an object specifies some browser.
 *   for example, get_browser_agents().MSIE
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
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

/**
 * format relative seconds to HH:MM:SS,
 * for example, 210s formated to 00:03:30
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * @usage relative_seconds_to_HHMMSS(210)
 */
function relative_seconds_to_HHMMSS(seconds){
    var date = new Date();
    date.setTime(Number(seconds) * 1000);

    var ret = padding(date.getUTCHours(), 2, '0')
        + ":" + padding(date.getUTCMinutes(), 2, '0')
        + ":" + padding(date.getUTCSeconds(), 2, '0');

    return ret;
}

/**
 * format absolute seconds to HH:MM:SS,
 * for example, 1389146480s (2014-01-08 10:01:20 GMT+0800) formated to 10:01:20
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * @usage absolute_seconds_to_HHMMSS(new Date().getTime() / 1000)
 */
function absolute_seconds_to_HHMMSS(seconds){
    var date = new Date();
    date.setTime(Number(seconds) * 1000);

    var ret = padding(date.getHours(), 2, '0')
        + ":" + padding(date.getMinutes(), 2, '0')
        + ":" + padding(date.getSeconds(), 2, '0');

    return ret;
}

/**
 * format absolute seconds to YYYY-mm-dd,
 * for example, 1389146480s (2014-01-08 10:01:20 GMT+0800) formated to 2014-01-08
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * @usage absolute_seconds_to_YYYYmmdd(new Date().getTime() / 1000)
 */
function absolute_seconds_to_YYYYmmdd(seconds) {
    var date = new Date();
    date.setTime(Number(seconds) * 1000);

    var ret = date.getFullYear()
        + "-" + padding(date.getMonth() + 1, 2, '0')
        + "-" + padding(date.getDate(), 2, '0');

    return ret;
}

/**
 * parse the date in str to Date object.
 * @param str the date in str, format as "YYYY-mm-dd", for example, 2014-12-11
 * @returns a date object.
 * @usage YYYYmmdd_parse("2014-12-11")
 */
function YYYYmmdd_parse(str) {
    var date = new Date();
    date.setTime(Date.parse(str));
    return date;
}

/**
 * async refresh function call. to avoid multiple call.
 * @remark AsyncRefresh is for jquery to refresh the speicified pfn in a page;
 *      if angularjs, use AsyncRefresh2 to change pfn, cancel previous request for angularjs use singleton object.
 * @param refresh_interval the default refresh interval ms.
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * the pfn can be implements as following:
 var async_refresh = new AsyncRefresh(pfn, 3000);
 function pfn() {
            if (!async_refresh.refresh_is_enabled()) {
                async_refresh.request(100);
                return;
            }
            $.ajax({
                type: 'GET', async: true, url: 'xxxxx',
                complete: function(){
                    if (!async_refresh.refresh_is_enabled()) {
                        async_refresh.request(0);
                    } else {
                        async_refresh.request(async_refresh.refresh_interval);
                    }
                },
                success: function(res){
                    // if donot allow refresh, directly return.
                    if (!async_refresh.refresh_is_enabled()) {
                        return;
                    }

                    // render the res.
                }
            });
        }
 */
function AsyncRefresh(pfn, refresh_interval) {
    this.refresh_interval = refresh_interval;

    this.__handler = null;
    this.__pfn = pfn;

    this.__enabled = true;
}
/**
 * disable the refresher, the pfn must check the refresh state.
 */
AsyncRefresh.prototype.refresh_disable = function() {
    this.__enabled = false;
}
AsyncRefresh.prototype.refresh_enable = function() {
    this.__enabled = true;
}
AsyncRefresh.prototype.refresh_is_enabled = function() {
    return this.__enabled;
}
/**
 * start new async request
 * @param timeout the timeout in ms.
 *      user can use the refresh_interval of the AsyncRefresh object,
 *      which initialized in constructor.
 */
AsyncRefresh.prototype.request = function(timeout) {
    if (this.__handler) {
        clearTimeout(this.__handler);
    }

    this.__handler = setTimeout(this.__pfn, timeout);
}

/**
 * async refresh v2, support cancellable refresh, and change the refresh pfn.
 * @remakr for angularjs. if user only need jquery, maybe AsyncRefresh is better.
 * @see: http://blog.csdn.net/win_lin/article/details/17994347
 * Usage:
 bsmControllers.controller('CServers', ['$scope', 'MServer', function($scope, MServer){
            async_refresh2.refresh_change(function(){
                // 获取服务器列表
                MServer.servers_load({}, function(data){
                    $scope.servers = data.data.servers;
                    async_refresh2.request();
                });
            }, 3000);

            async_refresh2.request(0);
        }]);
 bsmControllers.controller('CStreams', ['$scope', 'MStream', function($scope, MStream){
            async_refresh2.refresh_change(function(){
                // 获取流列表
                MStream.streams_load({}, function(data){
                    $scope.streams = data.data.streams;
                    async_refresh2.request();
                });
            }, 3000);

            async_refresh2.request(0);
        }]);
 */
function AsyncRefresh2() {
    /**
     * the function callback before call the pfn.
     * the protype is function():bool, which return true to invoke, false to abort the call.
     * null to ignore this callback.
     *
     * for example, user can abort the refresh by find the class popover:
     *      async_refresh2.on_before_call_pfn = function() {
     *          if ($(".popover").length > 0) {
     *              async_refresh2.request();
     *              return false;
     *          }
     *          return true;
     *      };
     */
    this.on_before_call_pfn = null;

    // use a anonymous function to call, and check the enabled when actually invoke.
    this.__call = {
        pfn: null,
        timeout: 0,
        __enabled: false,
        __handler: null
    };
}
// singleton
var async_refresh2 = new AsyncRefresh2();
/**
 * initialize or refresh change. cancel previous request, setup new request.
 * @param pfn a function():void to request after timeout. null to disable refresher.
 * @param timeout the timeout in ms, to call pfn. null to disable refresher.
 */
AsyncRefresh2.prototype.initialize = function(pfn, timeout) {
    this.refresh_change(pfn, timeout);
}
/**
 * stop refresh, the refresh pfn is set to null.
 */
AsyncRefresh2.prototype.stop = function() {
    this.__call.__enabled = false;
}
/**
 * restart refresh, use previous config.
 */
AsyncRefresh2.prototype.restart = function() {
    this.__call.__enabled = true;
    this.request(0);
}
/**
 * change refresh pfn, the old pfn will set to disabled.
 */
AsyncRefresh2.prototype.refresh_change = function(pfn, timeout) {
    // cancel the previous call.
    if (this.__call.__handler) {
        clearTimeout(this.__handler);
    }
    this.__call.__enabled = false;

    // setup new call.
    this.__call = {
        pfn: pfn,
        timeout: timeout,
        __enabled: true,
        __handler: null
    };
}
/**
 * start new request, we never auto start the request,
 * user must start new request when previous completed.
 * @param timeout [optional] if not specified, use the timeout in initialize or refresh_change.
 */
AsyncRefresh2.prototype.request = function(timeout) {
    var self = this;
    var this_call = this.__call;

    // clear previous timeout.
    if (this_call.__handler) {
        clearTimeout(this_call.__handler);
    }

    // override the timeout
    if (timeout == undefined) {
        timeout = this_call.timeout;
    }

    // if user disabled refresher.
    if (this_call.pfn == null || timeout == null) {
        return;
    }

    this_call.__handler = setTimeout(function(){
        // cancelled by refresh_change, ignore.
        if (!this_call.__enabled) {
            return;
        }

        // callback if the handler installled.
        if (self.on_before_call_pfn) {
            if (!self.on_before_call_pfn()) {
                return;
            }
        }

        // do the actual call.
        this_call.pfn();
    }, timeout);
}

