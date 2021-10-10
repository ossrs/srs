
scApp.controller("CSCMain", ["$scope", "$interval", "$location", "MSCApi", "$sc_utility", "$sc_server", '$sc_nav', function($scope, $interval, $location, MSCApi, $sc_utility, $sc_server, $sc_nav){
    $scope.logs = [];
    // remove expired alert.
    $interval(function(){
        for (var i = 0; i < $scope.logs.length; i++) {
            var log = $scope.logs[i];
            if (log.create + 10000 < new Date().getTime()) {
                $scope.logs.splice(i, 1);
                break;
            }
        }
    }, 3000);
    // handler system log event, from $sc_utility service.
    $scope.$on("$sc_utility_log", function(event, level, msg){
        var log = {
            level:level, msg:msg, create:new Date().getTime()
        };
        // only show 3 msgs.
        while ($scope.logs.length > 2) {
            $scope.logs.splice(0, 1);
        }
        $scope.logs.push(log);
    });

    // handle system error event, from $sc_utility service.
    $scope.$on("$sc_utility_http_error", function(event, status, response){
        var code = response.code;
        if (status !== 200) {
            if (!status && !response) {
                response = "无法访问服务器";
            } else {
                response = "HTTP/" + status + ", " + response;
            }
        } else {
            var map = {
                1061: "RAW API被禁用",
                1062: "服务器不允许这个操作",
                1063: "RawApi参数不符合要求"
            };
            if (map[response.code]) {
                response = "code=" + response.code + ", " + map[response.code];
            } else {
                resonse = "code=" + response.code + ", 系统错误";
            }
        }

        if (code === 1061) {
            $sc_utility.log("trace", response);
            return;
        }
        $sc_utility.log("warn", response);
    });

    $scope.gogogo = function (path) {
        $location.path(path);
    };
    $scope.goto = function (path) {
        var absUrl = $sc_server.buildNavUrl();
        var url = absUrl.replace($location.$$path, path);
        var win = window.open('', '_self');
        win.location = url; // For safari security reason, we should set the location instead.
    };
    $scope.redirect = function (from, to) {
        var absUrl = $sc_server.buildNavUrl();
        var url = absUrl.replace(from, to);
        var win = window.open('', '_self');
        win.location = url; // For safari security reason, we should set the location instead.
    };

    // init the server and port for api.
    $sc_server.init($location, MSCApi);

    //$sc_utility.log("trace", "set baseurl to " + $sc_server.baseurl());
}]);

scApp.controller("CSCConnect", ["$scope", "$location", "MSCApi", "$sc_utility", "$sc_nav", "$sc_server", function($scope, $location, MSCApi, $sc_utility, $sc_nav, $sc_server){
    $sc_nav.in_control();
    $scope.server = {
        schema: $sc_server.schema,
        ip: $sc_server.host,
        port: $sc_server.port
    };
    $scope.connect = function(){
        $sc_server.schema = $scope.server.schema;
        $sc_server.host = $scope.server.ip;
        $sc_server.port = $scope.server.port;

        MSCApi.versions_get(function(data){
            $sc_utility.log("trace", "连接到SRS" + $scope.server.ip + "成功, SRS/" + data.data.version);
            $scope.goto('/summaries');
        });
    };

    $sc_utility.refresh.stop();
}]);

scApp.controller("CSCSummary", ["$scope", "MSCApi", "$sc_utility", "$sc_nav", function($scope, MSCApi, $sc_utility, $sc_nav){
    $sc_nav.in_summary();

    $scope.pre_kbps = null;

    $sc_utility.refresh.refresh_change(function(){
        MSCApi.summaries_get(function(data){
            var kbps = {
                initialized: false,
                in: { srs: 0, sys: 0, inner: 0 },
                out: { srs: 0, sys: 0, inner: 0 }
            };
            if ($scope.pre_kbps) {
                kbps.initialized = true;

                var diff = data.data.system.net_sample_time - $scope.pre_kbps.system.net_sample_time;
                if (diff > 0) {
                    kbps.in.sys = (data.data.system.net_recv_bytes - $scope.pre_kbps.system.net_recv_bytes) * 8 / diff;
                    kbps.in.inner = (data.data.system.net_recvi_bytes - $scope.pre_kbps.system.net_recvi_bytes) * 8 / diff;

                    kbps.out.sys = (data.data.system.net_send_bytes - $scope.pre_kbps.system.net_send_bytes) * 8 / diff;
                    kbps.out.inner = (data.data.system.net_sendi_bytes - $scope.pre_kbps.system.net_sendi_bytes) * 8 / diff;
                }

                diff = data.data.system.srs_sample_time - $scope.pre_kbps.system.srs_sample_time;
                if (diff > 0) {
                    kbps.in.srs = (data.data.system.srs_recv_bytes - $scope.pre_kbps.system.srs_recv_bytes) * 8 / diff;
                    kbps.out.srs = (data.data.system.srs_send_bytes - $scope.pre_kbps.system.srs_send_bytes) * 8 / diff;
                }

                diff = data.data.now_ms - $scope.pre_kbps.now_ms;
                if (!$scope.kbps.initialized || diff >= 20 * 1000) {
                    $scope.pre_kbps = data.data;
                    $scope.kbps = kbps;
                }
            }
            if (!$scope.kbps) {
                $scope.pre_kbps = data.data;
                $scope.kbps = kbps;
            }

            $scope.global = data.data;
            $scope.server = data.data.self;
            $scope.system = data.data.system;

            $sc_utility.refresh.request();
        });
    }, 3000);

    $sc_utility.log("trace", "Retrieve summary from SRS.");
    $sc_utility.refresh.request(0);
}]);

scApp.controller("CSCVhosts", ["$scope", "MSCApi", "$sc_nav", "$sc_utility", function($scope, MSCApi, $sc_nav, $sc_utility){
    $sc_nav.in_vhosts();

    $sc_utility.refresh.refresh_change(function(){
        MSCApi.vhosts_get(function(data){
            $scope.vhosts = data.vhosts;

            $sc_utility.refresh.request();
        });
    }, 3000);

    $sc_utility.log("trace", "Retrieve vhosts from SRS");
    $sc_utility.refresh.request(0);
}]);

scApp.controller("CSCVhost", ["$scope", "$routeParams", "MSCApi", "$sc_nav", "$sc_utility", function($scope, $routeParams, MSCApi, $sc_nav, $sc_utility){
    $sc_nav.in_vhosts();

    $sc_utility.refresh.stop();

    MSCApi.vhosts_get2($routeParams.id, function(data){
        $scope.vhost = data.vhost;
    });

    $sc_utility.log("trace", "Retrieve vhost info from SRS");
}]);

scApp.controller("CSCStreams", ["$scope", "$location", "MSCApi", "$sc_nav", "$sc_utility", function($scope, $location, MSCApi, $sc_nav, $sc_utility){
    $sc_nav.in_streams();

    $scope.kickoff = function(stream) {
        MSCApi.clients_delete(stream.publish.cid, function(){
            $sc_utility.log("warn", "Kickoff stream ok.");
        });
    };

    $scope.dvr = function(stream){
        $location.path($sc_utility.dvr_id(stream));
    };

    $scope.support_raw_api = false;

    MSCApi.configs_raw(function(data) {
        $scope.support_raw_api = $sc_utility.raw_api_enabled(data);
    });

    MSCApi.vhosts_get(function(data){
        var vhosts = data.vhosts;

        $sc_utility.refresh.refresh_change(function(){
            MSCApi.streams_get(function(data){
                for (var k in data.streams) {
                    var stream = data.streams[k];
                    stream.owner = system_array_get(vhosts, function(vhost) {return vhost.id === stream.vhost; });
                }

                $scope.streams = data.streams;

                $sc_utility.refresh.request();
            });
        }, 3000);

        $sc_utility.log("trace", "Retrieve streams from SRS");
        $sc_utility.refresh.request(0);
    });

    $sc_utility.log("trace", "Retrieve vhost info from SRS");
}]);

scApp.controller("CSCStream", ["$scope", '$location', "$routeParams", "MSCApi", "$sc_nav", "$sc_utility", function($scope, $location, $routeParams, MSCApi, $sc_nav, $sc_utility){
    $sc_nav.in_streams();

    $scope.kickoff = function(stream) {
        MSCApi.clients_delete(stream.publish.cid, function(){
            $sc_utility.log("warn", "Kickoff stream ok.");
        });
    };

    $sc_utility.refresh.stop();

    $scope.dvr = function(stream){
        $location.path($sc_utility.dvr_id(stream));
    };

    $scope.support_raw_api = false;

    MSCApi.configs_raw(function(data) {
        $scope.support_raw_api = $sc_utility.raw_api_enabled(data);
    });

    MSCApi.streams_get2($routeParams.id, function(data){
        var stream = data.stream;
        if (!$scope.owner) {
            MSCApi.vhosts_get2(stream.vhost, function(data) {
                var vhost = data.vhost;

                stream.owner = $scope.owner = vhost;
                $scope.stream = stream;
            });
            $sc_utility.log("trace", "Retrieve vhost info from SRS");
        } else {
            stream.owner = $scope.owner;
            $scope.stream = stream;
        }
    });

    $sc_utility.log("trace", "Retrieve stream info from SRS");
}]);

scApp.controller("CSCClients", ["$scope", "MSCApi", "$sc_nav", "$sc_utility", function($scope, MSCApi, $sc_nav, $sc_utility){
    $sc_nav.in_clients();

    $scope.kickoff = function(client) {
        MSCApi.clients_delete(client.id, function(){
            $sc_utility.log("warn", "Kickoff client ok.");
        });
    };

    $sc_utility.refresh.refresh_change(function(){
        MSCApi.clients_get(function(data){
            $scope.clients = data.clients;

            $sc_utility.refresh.request();
        });
    }, 3000);

    $sc_utility.log("trace", "Retrieve clients from SRS");
    $sc_utility.refresh.request(0);
}]);

scApp.controller("CSCClient", ["$scope", "$routeParams", "MSCApi", "$sc_nav", "$sc_utility", function($scope, $routeParams, MSCApi, $sc_nav, $sc_utility){
    $sc_nav.in_clients();

    $scope.kickoff = function(client) {
        MSCApi.clients_delete(client.id, function(){
            $sc_utility.log("warn", "Kickoff client ok.");
        });
    };

    $sc_utility.refresh.stop();

    MSCApi.clients_get2($routeParams.id, function(data){
        $scope.client = data.client;
    });

    $sc_utility.log("trace", "Retrieve client info from SRS");
}]);

scApp.controller("CSCConfigs", ["$scope", "$location", "MSCApi", "$sc_nav", "$sc_utility", "$sc_server", function($scope, $location, MSCApi, $sc_nav, $sc_utility, $sc_server){
    $sc_nav.in_configs();

    $sc_utility.refresh.stop();

    MSCApi.configs_raw(function(data){
        $scope.http_api = data.http_api;
    });

    $sc_utility.log("trace", "Retrieve config info from SRS");
}]);

scApp.factory("MSCApi", ["$http", "$sc_server", function($http, $sc_server){
    return {
        versions_get: function(success) {
            var url = $sc_server.jsonp("/api/v1/versions");
            $http.jsonp(url).success(success);
        },
        summaries_get: function(success) {
            var url = $sc_server.jsonp("/api/v1/summaries");
            $http.jsonp(url).success(success);
        },
        vhosts_get: function(success) {
            var url = $sc_server.jsonp("/api/v1/vhosts/");
            $http.jsonp(url).success(success);
        },
        vhosts_get2: function(id, success) {
            var url = $sc_server.jsonp("/api/v1/vhosts/" + id);
            $http.jsonp(url).success(success);
        },
        streams_get: function(success) {
            var url = $sc_server.jsonp("/api/v1/streams/");
            $http.jsonp(url).success(success);
        },
        streams_get2: function(id, success) {
            var url = $sc_server.jsonp("/api/v1/streams/" + id);
            $http.jsonp(url).success(success);
        },
        clients_get: function(success) {
            var url = $sc_server.jsonp("/api/v1/clients/");
            $http.jsonp(url).success(success);
        },
        clients_get2: function(id, success) {
            var url = $sc_server.jsonp("/api/v1/clients/" + id);
            $http.jsonp(url).success(success);
        },
        clients_delete: function(id, success) {
            var url = $sc_server.jsonp_delete("/api/v1/clients/" + id);
            $http.jsonp(url).success(success);
        },
        configs_raw: function(success, error) {
            var url = $sc_server.jsonp_query("/api/v1/raw", "rpc=raw");
            var obj = $http.jsonp(url).success(success);
            if (error) {
                obj.error(error);
            }
        },
        configs_get: function(success) {
            var url = $sc_server.jsonp_query("/api/v1/raw", "rpc=query&scope=global");
            $http.jsonp(url).success(success);
        },
    };
}]);

scApp.filter("sc_filter_log_level", function(){
    return function(v) {
        return (v === "warn" || v === "error")? "alert-warn":"alert-success";
    };
});

scApp.filter("sc_filter_nav_active", ["$sc_nav", function($sc_nav){
    return function(v){
        return $sc_nav.is_selected(v)? "active":"";
    };
}]);

scApp.filter("sc_unsafe", ['$sce', function($sce){
    return function(v) {
        return $sce.trustAsHtml(v);
    };
}]);

scApp.filter("sc_filter_filesize_k", function(){
    return function(v){
        // PB
        if (v > 1024 * 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024 / 1024).toFixed(2) + "PB";
        }
        // TB
        if (v > 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024).toFixed(2) + "TB";
        }
        // GB
        if (v > 1024 * 1024) {
            return Number(v / 1024.0 / 1024).toFixed(2) + "GB";
        }
        // MB
        if (v > 1024) {
            return Number(v / 1024.0).toFixed(2) + "MB";
        }
        return Number(v).toFixed(2) + "KB";
    };
});

scApp.filter("sc_filter_filesize_k2", function(){
    return function(v){
        // PB
        if (v > 1024 * 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024 / 1024).toFixed(0) + "PB";
        }
        // TB
        if (v > 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024).toFixed(0) + "TB";
        }
        // GB
        if (v > 1024 * 1024) {
            return Number(v / 1024.0 / 1024).toFixed(0) + "GB";
        }
        // MB
        if (v > 1024) {
            return Number(v / 1024.0).toFixed(0) + "MB";
        }
        return Number(v).toFixed(0) + "KB";
    };
});

scApp.filter("sc_filter_filerate_k", function(){
    return function(v){
        // PB
        if (v > 1024 * 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024 / 1024).toFixed(2) + "PBps";
        }
        // TB
        if (v > 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024).toFixed(2) + "TBps";
        }
        // GB
        if (v > 1024 * 1024) {
            return Number(v / 1024.0 / 1024).toFixed(2) + "GBps";
        }
        // MB
        if (v > 1024) {
            return Number(v / 1024.0).toFixed(2) + "MBps";
        }
        return Number(v).toFixed(2) + "KBps";
    };
});

scApp.filter("sc_filter_filerate_k2", function(){
    return function(v){
        // PB
        if (v > 1024 * 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024 / 1024).toFixed(0) + "PBps";
        }
        // TB
        if (v > 1024 * 1024 * 1024) {
            return Number(v / 1024.0 / 1024 / 1024).toFixed(0) + "TBps";
        }
        // GB
        if (v > 1024 * 1024) {
            return Number(v / 1024.0 / 1024).toFixed(0) + "GBps";
        }
        // MB
        if (v > 1024) {
            return Number(v / 1024.0).toFixed(0) + "MBps";
        }
        return Number(v).toFixed(0) + "KBps";
    };
});

scApp.filter("sc_filter_bitrate_k", function(){
    return function(v){
        // PB
        if (v > 1000 * 1000 * 1000 * 1000) {
            return Number(v / 1000.0 / 1000 / 1000 / 1000).toFixed(2) + "Pbps";
        }
        // TB
        if (v > 1000 * 1000 * 1000) {
            return Number(v / 1000.0 / 1000 / 1000).toFixed(2) + "Tbps";
        }
        // GB
        if (v > 1000 * 1000) {
            return Number(v / 1000.0 / 1000).toFixed(2) + "Gbps";
        }
        // MB
        if (v > 1000) {
            return Number(v / 1000.0).toFixed(2) + "Mbps";
        }
        return Number(v).toFixed(2) + "Kbps";
    };
});

scApp.filter("sc_filter_bitrate_k2", function(){
    return function(v){
        // PB
        if (v > 1000 * 1000 * 1000 * 1000) {
            return Number(v / 1000.0 / 1000 / 1000 / 1000).toFixed(0) + "Pbps";
        }
        // TB
        if (v > 1000 * 1000 * 1000) {
            return Number(v / 1000.0 / 1000 / 1000).toFixed(0) + "Tbps";
        }
        // GB
        if (v > 1000 * 1000) {
            return Number(v / 1000.0 / 1000).toFixed(0) + "Gbps";
        }
        // MB
        if (v > 1000) {
            return Number(v / 1000.0).toFixed(0) + "Mbps";
        }
        return Number(v).toFixed(0) + "Kbps";
    };
});

scApp.filter("sc_filter_percent", function(){
    return function(v){
        return Number(v).toFixed(2) + "%";
    };
});

scApp.filter("sc_filter_percent2", function(){
    return function(v){
        return Number(v).toFixed(0) + "%";
    };
});

scApp.filter("sc_filter_percentf", function(){
    return function(v){
        return Number(v * 100).toFixed(2) + "%";
    };
});

scApp.filter("sc_filter_percentf2", function(){
    return function(v){
        return Number(v * 100).toFixed(0) + "%";
    };
});

scApp.filter("sc_filter_video", function(){
    return function(v){
        // set default value for SRS2.
        v.width = v.width? v.width : 0;
        v.height = v.height? v.height : 0;

        return v? v.codec + "/" + v.profile + "/" + v.level + "/" + v.width + "x" + v.height : "无视频";
    };
});

scApp.filter("sc_filter_audio", function(){
    return function(v){
        return v? v.codec + "/" + v.sample_rate + "/" + (v.channel === 2? "Stereo":"Mono") + "/" + v.profile : "无音频";
    };
});

scApp.filter("sc_filter_number", function(){
    return function(v){
        return Number(v).toFixed(2);
    };
});

scApp.filter("sc_filter_less", function(){
    return function(v) {
        return v? (v.length > 15? v.substr(0, 15) + "...":v):v;
    };
});

scApp.filter('sc_filter_style_error', function(){
    return function(v){
        return v? 'alert-danger':'';
    };
});

scApp.filter('sc_filter_preview_url', ['$sc_server', function($sc_server){
    return function(v){
        var page = $sc_server.schema + "://ossrs.net/players/srs_player.html";
        var http = $sc_server.http[$sc_server.http.length - 1];
        var query = "vhost=" + v.owner.name + "&app=" + v.app + "&stream=" + v.name + ".flv";
        query += "&server=" + $sc_server.host +"&port=" + http + "&autostart=true&schema=" + $sc_server.schema;
        return v? page+"?" + query:"javascript:void(0)";
    };
}]);

// the sc nav is the nevigator
scApp.provider("$sc_nav", function(){
    this.$get = function(){
        return {
            selected: null,
            in_control: function(){
                this.selected = "/console";
            },
            in_summary: function(){
                this.selected = "/summaries";
            },
            in_vhosts: function(){
                this.selected = "/vhosts";
            },
            in_streams: function(){
                this.selected = "/streams";
            },
            in_clients: function(){
                this.selected = "/clients";
            },
            in_configs: function(){
                this.selected = "/configs";
            },
            go_summary: function($location){
                $location.path("/summaries");
            },
            is_selected: function(v){
                return v === this.selected;
            }
        };
    };
});

// the sc server is the server we connected to.
scApp.provider("$sc_server", [function(){
    this.$get = function(){
        var self = {
            schema: "http",
            host: null,
            port: 1985,
            rtmp: [1935],
            http: [8080],
            baseurl: function(){
                return self.schema + "://" + self.host + (self.port === 80? "": ":" + self.port);
            },
            jsonp: function(url){
                return self.baseurl() + url + "?callback=JSON_CALLBACK";
            },
            jsonp_delete: function(url) {
                return self.jsonp(url) + "&method=DELETE";
            },
            jsonp_query: function(url, query){
                return self.baseurl() + url + "?callback=JSON_CALLBACK&" + query;
            },
            buildNavUrl: function () {
                var $location = self.$location;
                var url = $location.absUrl();
                if (url.indexOf('?') > 0) {
                    url = url.substr(0, url.indexOf('?'));
                }
                url += '?';

                var query = {};
                for (var key in $location.search()) {
                    query[key] = $location.search()[key];
                }

                query['schema'] = self.schema;
                query['host'] = self.host;
                query['port'] = self.port;

                var queries = [];
                for (var key in query) {
                    var value = query[key];
                    if (!Array.isArray(value)) {
                        value = [value];
                    }
                    for (var i in value) {
                        queries.push(key + '=' + value[i]);
                    }
                }
                url += queries.join('&');

                return url;
            },
            init: function($location, MSCApi) {
                self.$location = $location;

                // Set the right schema for proxy.
                if ($location.search().schema) {
                    self.schema = $location.search().schema;
                } else {
                    self.schema = $location.protocol();
                }

                // query string then url.
                if ($location.search().host) {
                    self.host = $location.search().host;
                } else {
                    self.host = $location.host();
                }

                if ($location.search().port) {
                    self.port = $location.search().port;
                } else {
                    self.port = $location.port();
                }
            }
        };
        return self;
    };
}]);

// the sc utility is a set of helper utilities.
scApp.provider("$sc_utility", function(){
    this.$get = ["$rootScope", function($rootScope){
        return {
            log: function(level, msg) {
                $rootScope.$broadcast("$sc_utility_log", level, msg);
            },
            http_error: function(status, response) {
                $rootScope.$broadcast("$sc_utility_http_error", status, response);
            },
            find_siblings: function(elem, className) {
                if (elem.hasClass(className)) {
                    return elem;
                }

                if (!elem[0].nextSibling) {
                    return null;
                }

                var sibling = angular.element(elem[0].nextSibling);
                return this.find_siblings(sibling, className);
            },
            array_actual_equals: function(a, b) {
                // all elements of a in b.
                for (var i = 0; i < a.length; i++) {
                    if (!system_array_contains(b, a[i])) {
                        return false;
                    }
                }

                // all elements of b in a.
                for (i = 0; i < b.length; i++) {
                    if (!system_array_contains(a, b[i])) {
                        return false;
                    }
                }

                return true;
            },
            object2arr: function(obj, each) {
                var arr = [];
                for (var k in obj) {
                    var v = obj[k];

                    if (each) {
                        each(v);
                    }

                    arr.push(v);
                }
                return arr;
            },
            /**
             * transform the api data to angularjs perfer, for instance:
             data.global.listen = ["1935, "1936"];
             data.global.http_api.listen = "1985";
             * parsed to:
             global.listen = {
                        key: 'listen',
                        value: ["1935", "1936"],
                        error: false
                     }
             global.http_api.listen = {
                        key: 'http_api.listen',
                        value: "1985",
                        error: false
                     }
             * where the error is used for commit error.
             */
            object2complex: function(complex, obj, prefix) {
                for (var k in obj) {
                    var v = obj[k];
                    //console.log("k=" + key + ", v=" + typeof v + ", " + v);

                    var key = prefix? prefix + "." + k : k;
                    if (key === "vhosts") {
                        // use name as vhost id.
                        complex[k] = this.object2arr(v, function(e) { e.vid = e.name; });
                        continue;
                    }

                    if (typeof v === "object" && v.constructor !== Array) {
                        var cv = {};
                        complex[k] = cv;

                        this.object2complex(cv, v, key);
                        continue;
                    }

                    // convert number to str for select to
                    // choose the right default one.
                    if (key === "pithy_print_ms") {
                        v = String(v);
                    }

                    complex[k] = {
                        key: system_string_trim(key, 'global.'),
                        value: v,
                        error: false
                    };
                }

                return complex;
            },
            copy_object: function(dst, src) {
                for (var k in src) {
                    dst[k] = src[k];
                }
            },
            dvr_id: function(stream) {
                var url = '/dvr/' + stream.owner.name
                    + '/' + stream.id
                    + '/' + escape(stream.app.replace('/', '___'))
                    + '/' + escape(stream.name.replace('/', '___'));
                return url;
            },
            raw_api_enabled: function(data) {
                return data.http_api && data.http_api.enabled && data.http_api.raw_api && data.http_api.raw_api.enabled;
            },
            refresh: async_refresh2
        };
    }];
});

// sc-collapse: scCollapse
/**
 * Usage:
        <div class="accordion">
            <div class="accordion-group">
                <div class="accordion-heading" sc-collapse="in">
                    <a class="accordion-toggle" href="javascript:void(0)">
                        HTTP RAW API
                    </a>
                </div>
                <div class="accordion-body collapse">
                    <div class="accordion-inner">
                        该服务器不支持HTTP RAW API，或者配置中禁用了该功能。
                    </div>
                </div>
            </div>
        </div>
 */
scApp.directive('scCollapse', ["$sc_utility", function($sc_utility){
    return {
        restrict: 'A',
        scope: true,
        controller: ['$scope', function($scope) {
        }],
        compile: function(elem, attrs) {
            return function(scope, elem, attrs){
                if (attrs.scCollapse === "in") {
                    var obj = $sc_utility.find_siblings(elem, 'accordion-body');
                    obj.addClass('in');
                }

                elem.on('click', function(){
                    var obj = $sc_utility.find_siblings(elem, 'accordion-body');
                    obj.toggleClass('in');
                });
            };
        }
    };
}]);

// sc-pretty: scPretty
/**
 * Usage:
     <tr sc-pretty scp-key="http_api.enabled" scp-value="http_api.enabled" scp-bool="true"
        scp-desc="是否开启HTTP API，开启后就可以访问SRS提供的API管理服务器。默认: {{false| sc_filter_enabled}}">
     </tr>
 */
scApp.directive("scPretty", [function(){
    return {
        restrict: 'A',
        scope: {
            key: '@scpKey',
            value: '=scpValue',
            desc: '@scpDesc',
            bool: '@scpBool'
        },
        template: ''
            + '<td>{{key}}</td>'
            + '<td>'
                + '<span class="{{value == undefined? \'label\':\'\'}}">'
                    + '<span ng-show="bool && value != undefined">{{value| sc_filter_enabled}}</span>'
                    + '<span ng-show="!bool || value === undefined">{{value| sc_filter_obj}}</span>'
                + '</span>'
            + '</td>'
            + '<td>{{desc}}</td>'
            + '<td>只读</td>'
    };
}]);

// sc-pretty2: scPretty2
/**
 * Usage:
     <tr sc-pretty2 scp-data="global.daemon" scp-bool="true"
        scp-desc="是否以后台启动SRS。默认: {{true| sc_filter_yesno}}">
     </tr>
 */
scApp.directive("scPretty2", [function(){
    return {
        restrict: 'A',
        scope: {
            data: '=scpData',
            desc: '@scpDesc',
            bool: '@scpBool',
            link: '@scpLink'
        },
        controller: ['$scope', function($scope){
        }],
        template: ''
            + '<td>{{key}}</td>'
            + '<td>'
                + '<span ng-if="link">'
                    + '<a href="{{link}}">{{data.value}}</a>'
                + '</span>'
                + '<span class="{{data.value == undefined? \'label\':\'\'}}" ng-if="!link">'
                    + '<span ng-show="bool && data.value != undefined">{{data.value| sc_filter_enabled}}</span>'
                    + '<span ng-show="!bool || data.value == undefined">{{data.value| sc_filter_obj}}</span>'
                + '</span>'
            + '</td>'
            + '<td>{{desc}}</td>'
            + '<td>Readonly</td>',
        link: function(scope, elem, attrs){
            scope.key = system_string_trim(attrs.scpData, "global.");
        }
    };
}]);

// sc-directive: scDirective
/**
 * Usage:
         <tr sc-directive scd-data="obj"
             scd-desc="侦听的端口" scd-default="1935" scd-span="span3"
             scd-array="true" scd-bool="true" scd-select="1935,1936,1937"
             scd-submit="submit(obj)">
         </tr>
 * where obj is:
         {
             key: "listen",
             value: ["1935", "1936"],
             error: false
         }
 */
scApp.directive("scDirective", ["$sc_utility", function($sc_utility){
    return {
        restrict: 'A',
        scope: {
            data: '=scdData',
            desc: '@scdDesc',
            submit: '&scdSubmit',
            span: '@scdSpan',
            default: '@scdDefault',
            array: '@scdArray',
            bool: '@scdBool',
            select: '@scdSelect'
        },
        controller: ['$scope', function($scope) {
            // whether current directive is editing.
            $scope.editing = false;

            // previous old value, for cancel and array value.
            $scope.old_data = {
                init: false,
                value: undefined,
                reset: function(){
                    this.init = false;
                    this.value = undefined;
                }
            };

            // split select to array.
            if (typeof $scope.select === "string" && $scope.select && !$scope.selects) {
                $scope.selects = $scope.select.split(",");
            }

            $scope.edit = function() {
                $scope.editing = true;
            };

            $scope.commit = function() {
                // for array, string to array.
                if ($scope.array === "true" && typeof $scope.data.value === "string") {
                    $scope.data.value = $scope.data.value.split(",");
                }

                if ($scope.old_data.init && !$scope.submit()) {
                    return;
                }

                $scope.editing = false;
                $scope.old_data.reset();
            };

            $scope.load_default = function(){
                if ($scope.default !== undefined) {
                    if ($scope.bool === "true") {
                        $scope.data.value = $scope.default === "true";
                    } else if ($scope.array === "true") {
                        $scope.data.value = $scope.default.split(",");
                    } else {
                        $scope.data.value = $scope.default;
                    }
                }
            };

            $scope.cancel = function() {
                if ($scope.old_data.init) {
                    $scope.data.value = $scope.old_data.value;
                }

                // for array, always restore it when cancel.
                if ($scope.array === "true") {
                    $scope.data.value = $scope.old_data.value;
                }

                $scope.editing = false;
                $scope.old_data.reset();
            };

            $scope.$watch("editing", function(nv, ov){
                // init, ignore.
                if (!nv && !nv) {
                    return;
                }

                // when server not set this option, the whole data is undefined.
                if (!$scope.data) {
                    $scope.data = {
                        key: $scope.key,
                        value: undefined,
                        error: false
                    };
                }

                // save the old value.
                if (!$scope.old_data.init) {
                    $scope.old_data.value = $scope.data.value;
                    $scope.old_data.init = true;
                }

                // start editing.
                if (nv && !ov) {
                    // for array, array to string.
                    if ($scope.array === "true") {
                        $scope.data.value = $scope.data.value.join(",");
                    }
                }
            });
        }],
        template: scDirectiveTemplate,
        link: function(scope, elem, attrs){
            scope.key = system_string_trim(attrs.scdData, "global.");
        }
    };
}]);

// config the http interceptor.
scApp.config(['$httpProvider', function($httpProvider){
    $httpProvider.interceptors.push('MHttpInterceptor');
}]);

// the http interceptor.
scApp.factory('MHttpInterceptor', ["$q", "$sc_utility", function($q, $sc_utility){
    // register the interceptor as a service
    // @see: https://code.angularjs.org/1.2.0-rc.3/docs/api/ng.$http
    // @remark: the function($q) should never add other params.
    return {
        'request': function(config) {
            return config || $q.when(config);
        },
        'requestError': function(rejection) {
            return $q.reject(rejection);
        },
        'response': function(response) {
            if (response.data.code && response.data.code !== 0) {
                $sc_utility.http_error(response.status, response.data);
                // the $q.reject, will cause the error function of controller.
                // @see: https://code.angularjs.org/1.2.0-rc.3/docs/api/ng.$q
                return $q.reject(response);
            }
            return response || $q.when(response);
        },
        'responseError': function(rejection) {
            $sc_utility.http_error(rejection.status, rejection.data);
            return $q.reject(rejection);
        }
    };
}]);
