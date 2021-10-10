var scApp = angular.module("scApp", ["ngRoute", "ngResource",
    "bravoUiAlert", "bravoUiPopover"
]);

scApp.config(["$routeProvider", function($routeProvider){
    $routeProvider.otherwise({redirectTo:"/connect"})
        .when("/connect", {templateUrl:"views/connect.html", controller:"CSCConnect"})
        .when("/vhosts", {templateUrl:"views/vhosts.html", controller:"CSCVhosts"})
        .when("/vhosts/:id", {templateUrl:"views/vhost.html", controller:"CSCVhost"})
        .when("/streams", {templateUrl:"views/streams.html", controller:"CSCStreams"})
        .when("/streams/:id", {templateUrl:"views/stream.html", controller:"CSCStream"})
        .when("/clients", {templateUrl:"views/clients.html", controller:"CSCClients"})
        .when("/clients/:id", {templateUrl:"views/client.html", controller:"CSCClient"})
        .when("/configs", {templateUrl:"views/configs.html", controller:"CSCConfigs"})
        .when("/summaries", {templateUrl:"views/summary.html", controller:"CSCSummary"});
}]);

scApp.filter("sc_filter_time", function(){
    return function(v){
        var s = "";
        if (v > 3600 * 24) {
            s = Number(v / 3600 / 24).toFixed(0) + "天 ";
            v = v % (3600 * 24);
        }
        s += relative_seconds_to_HHMMSS(v);
        return s;
    };
});

scApp.filter("sc_filter_yesno", function(){
    return function(v){
        return v? "是":"否";
    };
});

scApp.filter("sc_filter_enabled", function(){
    return function(v){
        return v? "开启":"关闭";
    };
});

scApp.filter("sc_filter_yn", function(){
    return function(v){
        return v? "Y":"N";
    };
});

scApp.filter("sc_filter_has_stream", function(){
    return function(v){
        return v? "有流":"无流";
    };
});

scApp.filter("sc_filter_ctype", function(){
    return function(v){
        return v? "推流":"播放";
    };
});

scApp.filter("sc_filter_obj", function(){
    return function(v) {
        return v !== undefined? v : "未设置";
    };
});

scApp.filter("sc_filter_security", function(){
    return function(v) {
        var action = v.action === "allow"? "允许":"禁止";
        var method = v.method === "all"? "任何操作": (v.method === "publish"? "推流":"播放");
        var entry = v.entry === "all"? "所有人" : v.entry;
        return action + " " + entry + " " + method;
    }
});

var scDirectiveTemplate = ''
    + '<td class="{{data.error| sc_filter_style_error}}">'
      + '{{key}}'
    + '</td>'
    + '<td colspan="{{editing? 2:0}}" title="{{data.value}}" class="{{data.error| sc_filter_style_error}}">'
      + '<div class="form-inline">'
          + '<span class="{{!data.error && data.value == undefined?\'label\':\'\'}}" ng-show="!editing">'
              + '<span ng-show="bool == \'true\' && data.value != undefined">{{data.value| sc_filter_enabled}}</span>'
              + '<span ng-show="bool != \'true\' || data.value == undefined">{{data.value| sc_filter_obj| sc_filter_less}}</span>'
          + '</span> '
          + '<input type="text" class="{{span}} inline" ng-show="editing && bool != \'true\' && !select" ng-model="data.value"> '
          + '<label class="checkbox" ng-show="editing && bool == \'true\'"><input type="checkbox" ng-model="data.value">开启</label> '
          + '<select ng-model="data.value" ng-options="s as s for s in selects" ng-show="editing && select"></select>'
          + '<a href="javascript:void(0)" ng-click="load_default()" ng-show="editing && default != undefined" title="使用默认值">使用默认值</a> '
      + '</div>'
      + '<div ng-show="editing">{{desc}}</div>'
    + '</td>'
    + '<td ng-show="!editing" class="{{data.error| sc_filter_style_error}}">'
      + '{{desc}}'
    + '</td>'
    + '<td class="span1 {{data.error| sc_filter_style_error}}">'
      + '<a href="javascript:void(0)" ng-click="edit()" ng-show="!editing" title="修改">修改</a> '
      + '<a bravo-popover href="javascript:void(0)"'
          + 'data-content="请确认是否修改?" data-title="请确认" data-placement="left"'
          + 'bravo-popover-confirm="commit()" ng-show="editing">'
              + '提交'
      + '</a> '
      + '<a href="javascript:void(0)" ng-click="cancel()" ng-show="editing" title="取消">放弃</a> '
    + '</td>';
