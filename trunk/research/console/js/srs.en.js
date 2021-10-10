var scApp = angular.module("scApp", ["ngRoute", "ngResource",
    "bravoUiAlert", "bravoUiPopover"
]);

scApp.config(["$routeProvider", function($routeProvider){
    $routeProvider.otherwise({redirectTo:"/connect"})
        .when("/connect", {templateUrl:"views/connect_en.html", controller:"CSCConnect"})
        .when("/vhosts", {templateUrl:"views/vhosts_en.html", controller:"CSCVhosts"})
        .when("/vhosts/:id", {templateUrl:"views/vhost_en.html", controller:"CSCVhost"})
        .when("/streams", {templateUrl:"views/streams_en.html", controller:"CSCStreams"})
        .when("/streams/:id", {templateUrl:"views/stream_en.html", controller:"CSCStream"})
        .when("/clients", {templateUrl:"views/clients_en.html", controller:"CSCClients"})
        .when("/clients/:id", {templateUrl:"views/client_en.html", controller:"CSCClient"})
        .when("/configs", {templateUrl:"views/configs_en.html", controller:"CSCConfigs"})
        .when("/summaries", {templateUrl:"views/summary_en.html", controller:"CSCSummary"});
}]);

scApp.filter("sc_filter_time", function(){
    return function(v){
        var s = "";
        if (v > 3600 * 24) {
            s = Number(v / 3600 / 24).toFixed(0) + "d ";
            v = v % (3600 * 24);
        }
        s += relative_seconds_to_HHMMSS(v);
        return s;
    };
});

scApp.filter("sc_filter_yesno", function(){
    return function(v){
        return v? "Yes":"No";
    };
});

scApp.filter("sc_filter_enabled", function(){
    return function(v){
        return v? "Enabled":"Disabled";
    };
});

scApp.filter("sc_filter_yn", function(){
    return function(v){
        return v? "Y":"N";
    };
});

scApp.filter("sc_filter_has_stream", function(){
    return function(v){
        return v? "Y":"N";
    };
});

scApp.filter("sc_filter_ctype", function(){
    return function(v){
        return v? "Publish":"Play";
    };
});

scApp.filter("sc_filter_obj", function(){
    return function(v) {
        return v !== undefined? v : "Unknown";
    };
});

scApp.filter("sc_filter_security", function(){
    return function(v) {
        var action = v.action === "allow"? "Allow":"Denied";
        var method = v.method === "all"? "Any": (v.method === "publish"? "Publish":"Play");
        var entry = v.entry === "all"? "All" : v.entry;
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
          + '<label class="checkbox" ng-show="editing && bool == \'true\'"><input type="checkbox" ng-model="data.value">Enable</label> '
          + '<select ng-model="data.value" ng-options="s as s for s in selects" ng-show="editing && select"></select>'
          + '<a href="javascript:void(0)" ng-click="load_default()" ng-show="editing && default != undefined" title="Use Default Values">Restore</a> '
      + '</div>'
      + '<div ng-show="editing">{{desc}}</div>'
    + '</td>'
    + '<td ng-show="!editing" class="{{data.error| sc_filter_style_error}}">'
      + '{{desc}}'
    + '</td>'
    + '<td class="span1 {{data.error| sc_filter_style_error}}">'
      + '<a href="javascript:void(0)" ng-click="edit()" ng-show="!editing" title="Mofity it">Update</a> '
      + '<a bravo-popover href="javascript:void(0)"'
          + 'data-content="Confirm Update?" data-title="Please Confirm" data-placement="left"'
          + 'bravo-popover-confirm="commit()" ng-show="editing">'
              + 'Submit'
      + '</a> '
      + '<a href="javascript:void(0)" ng-click="cancel()" ng-show="editing" title="Cancel">Cancel</a> '
    + '</td>';

