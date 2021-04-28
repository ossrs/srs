angular.module('bravoUiPopover', [])
    .directive("bravoPopover", function($compile, $position, $sce){
        // $parse : ng表达式 {{1+2}} {{text}}
        // $compile : 编译一段html字符串（可以包括ng表达式）
        return {
            restrict: "A",
            scope: {
                confirm: '&bravoPopoverConfirm'
            },
            compile: function (elem, attr) {
                var confirm_template = attr['bravoPopoverConfirm'] ? '<span>' +
                            '<a class="btn btn-danger" ng-click="on_confirm()">Confirm</a> ' +
                            '<a class="btn btn-info" ng-click="on_cancel()">Cancel</a>' +
                        '</span>' : '';
                var template =
                    '<div class="popover fade {{placement}} in" ng-show="popoover_show == \'in\'">' +
                        '<div class="arrow"></div>' +
                        '<h3 class="popover-title">{{title}}</h3>' +
                        '<div class="popover-content" ng-bind-html="content">' +
                        '</div>' +
                        '<div class="popover-content" ng-show="show_confirm_template">' +
                             confirm_template +
                        '</div>' +
                    '</div>';
                var linker = $compile(template);
                return function (scope, elem, attr) {
                    scope.popoover_show = "";
                    scope.title = attr['title'];
                    scope.content = $sce.trustAsHtml(attr['content']);
                    scope.placement = attr['placement'] ? attr['placement'] : 'top';
                    scope.trigger = attr['bravoPopoverConfirm'] ? 'click' : attr['trigger'];
                    scope.show_confirm_template =  attr['bravoPopoverConfirm'] ? true : false;

                    var tooltip = linker(scope, function (o) {
                        elem.after(o);
                    });
                    tooltip.css({ top: 0, left: 0, display: 'block' });

                    if (!scope.trigger || scope.trigger == 'click') {
                        elem.on('click', function (event) {
                            toggle();
                        });
                    } else {
                        var eventIn = scope.trigger == 'hover' ? 'mouseenter' : 'focus';
                        var eventOut = scope.trigger == 'hover' ? 'mouseleave' : 'blur';
                        elem.on(eventIn, function (event) {
                            show_popover();
                        });
                        elem.on(eventOut, function () {
                            hide_popover();
                        });
                    }

                    var toggle = function() {
                        scope.popoover_show == 'in'? hide_popover() : show_popover();
                        render_css(tooltip);
                    };

                    var show_popover = function() {
                        scope.popoover_show = "in";
                        scope.$apply();
                        render_css(tooltip);
                    };

                    var hide_popover = function() {
                        scope.popoover_show = "";
                        scope.$apply();
                    };

                    var render_css = function (scope_element) {
                        var ttPosition = $position.positionElements(elem, scope_element, scope.placement, false);
                        ttPosition.top += 'px';
                        ttPosition.left += 'px';
                        // Now set the calculated positioning.
                        scope_element.css( ttPosition );
                    };

                    render_css(tooltip);
                    scope.on_cancel = function () {
                        scope.popoover_show = "";
                    };
                    scope.on_confirm = function () {
                        scope.confirm();
                        scope.popoover_show = "";
                    };
                }
            }
        };
    })
    .factory('$position', ['$document', '$window', function ($document, $window) {

        function getStyle(el, cssprop) {
            if (el.currentStyle) { //IE
                return el.currentStyle[cssprop];
            } else if ($window.getComputedStyle) {
                return $window.getComputedStyle(el)[cssprop];
            }
            // finally try and get inline style
            return el.style[cssprop];
        }

        /**
         * Checks if a given element is statically positioned
         * @param element - raw DOM element
         */
        function isStaticPositioned(element) {
            return (getStyle(element, 'position') || 'static' ) === 'static';
        }

        /**
         * returns the closest, non-statically positioned parentOffset of a given element
         * @param element
         */
        var parentOffsetEl = function (element) {
            var docDomEl = $document[0];
            var offsetParent = element.offsetParent || docDomEl;
            while (offsetParent && offsetParent !== docDomEl && isStaticPositioned(offsetParent) ) {
                offsetParent = offsetParent.offsetParent;
            }
            return offsetParent || docDomEl;
        };

        return {
            /**
             * Provides read-only equivalent of jQuery's position function:
             * http://api.jquery.com/position/
             */
            position: function (element) {
                var elBCR = this.offset(element);
                var offsetParentBCR = { top: 0, left: 0 };
                var offsetParentEl = parentOffsetEl(element[0]);
                if (offsetParentEl != $document[0]) {
                    offsetParentBCR = this.offset(angular.element(offsetParentEl));
                    offsetParentBCR.top += offsetParentEl.clientTop - offsetParentEl.scrollTop;
                    offsetParentBCR.left += offsetParentEl.clientLeft - offsetParentEl.scrollLeft;
                }

                var boundingClientRect = element[0].getBoundingClientRect();
                return {
                    width: boundingClientRect.width || element.prop('offsetWidth'),
                    height: boundingClientRect.height || element.prop('offsetHeight'),
                    top: elBCR.top - offsetParentBCR.top,
                    left: elBCR.left - offsetParentBCR.left
                };
            },

            /**
             * Provides read-only equivalent of jQuery's offset function:
             * http://api.jquery.com/offset/
             */
            offset: function (element) {
                var boundingClientRect = element[0].getBoundingClientRect();
                return {
                    width: boundingClientRect.width || element.prop('offsetWidth'),
                    height: boundingClientRect.height || element.prop('offsetHeight'),
                    top: boundingClientRect.top + ($window.pageYOffset || $document[0].documentElement.scrollTop),
                    left: boundingClientRect.left + ($window.pageXOffset || $document[0].documentElement.scrollLeft)
                };
            },

            /**
             * Provides coordinates for the targetEl in relation to hostEl
             */
            positionElements: function (hostEl, targetEl, positionStr, appendToBody) {

                var positionStrParts = positionStr.split('-');
                var pos0 = positionStrParts[0], pos1 = positionStrParts[1] || 'center';

                var hostElPos,
                    targetElWidth,
                    targetElHeight,
                    targetElPos;

                hostElPos = appendToBody ? this.offset(hostEl) : this.position(hostEl);

                targetElWidth = targetEl.prop('offsetWidth');
                targetElHeight = targetEl.prop('offsetHeight');

                var shiftWidth = {
                    center: function () {
                        return hostElPos.left + hostElPos.width / 2 - targetElWidth / 2;
                    },
                    left: function () {
                        return hostElPos.left;
                    },
                    right: function () {
                        return hostElPos.left + hostElPos.width;
                    }
                };

                var shiftHeight = {
                    center: function () {
                        return hostElPos.top + hostElPos.height / 2 - targetElHeight / 2;
                    },
                    top: function () {
                        return hostElPos.top;
                    },
                    bottom: function () {
                        return hostElPos.top + hostElPos.height;
                    }
                };

                switch (pos0) {
                    case 'right':
                        targetElPos = {
                            top: shiftHeight[pos1](),
                            left: shiftWidth[pos0]()
                        };
                        break;
                    case 'left':
                        targetElPos = {
                            top: shiftHeight[pos1](),
                            left: hostElPos.left - targetElWidth
                        };
                        break;
                    case 'bottom':
                        targetElPos = {
                            top: shiftHeight[pos0](),
                            left: shiftWidth[pos1]()
                        };
                        break;
                    default:
                        targetElPos = {
                            top: hostElPos.top - targetElHeight,
                            left: shiftWidth[pos1]()
                        };
                        break;
                }

                return targetElPos;
            }
        };
    }]);