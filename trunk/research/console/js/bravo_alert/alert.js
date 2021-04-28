angular.module('bravoUiAlert', [])
    .directive('bravoAlert', function () {
        return {
            restrict: "A",
            transclude: true,
            template: '<div ng-transclude></div>',
            scope: {
                alert_show: '=alertShow',
                on_close: '&bravoAlertClose',
                on_closed: '&bravoAlertClosed'
            },
            compile: function (elem, attr) {
                var manual = attr['alertShow'];
                return function (scope, elem, attr) {
                    elem.on('click', function(event) {
                        var obj = angular.element(event.target);
                        if (obj.attr('data-dismiss')) {
                            scope.on_destory();
                        }
                    });
                    scope.on_destory = function () {
                        if (!manual) {
                            scope.on_close();
                            elem.addClass('ng-hide');
                            scope.on_closed();
                        } else {
                            scope.on_close();
                            scope.alert_show = false;
                            scope.$apply();
                        }
                    };
                    scope.$watch('alert_show', function (nv, ov) {
                        if (nv != ov) {
                            if (!nv) {
                                elem.addClass('ng-hide');
                                scope.on_closed();
                            } else {
                                elem.removeClass('ng-hide');
                            }
                        }
                    });
                }
            }
        }
    });
