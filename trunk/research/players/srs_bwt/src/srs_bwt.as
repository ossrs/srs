/*
The MIT License (MIT)

Copyright (c) 2013-2015 SRS(simple-rtmp-server)

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
package
{
    import flash.display.LoaderInfo;
    import flash.display.Sprite;
    import flash.display.StageAlign;
    import flash.display.StageScaleMode;
    import flash.events.Event;
    import flash.events.NetStatusEvent;
    import flash.events.TimerEvent;
    import flash.external.ExternalInterface;
    import flash.net.NetConnection;
    import flash.net.ObjectEncoding;
    import flash.system.System;
    import flash.ui.ContextMenu;
    import flash.ui.ContextMenuItem;
    import flash.utils.Timer;
    import flash.utils.setTimeout;
    
    public class srs_bwt extends Sprite
    {
        /**
        * the SRS bandwidth check/test library object.
        */
        private var bandwidth:SrsBandwidth = new SrsBandwidth();
        
        /**
        * when not specifies any param, directly run the swf.
        */
        private var default_url:String = "rtmp://dev:1935/app?key=35c9b402c12a7246868752e2878f7e0e&vhost=bandcheck.srs.com";
        
        public function srs_bwt()
        {
            if (!this.stage) {
                this.addEventListener(Event.ADDED_TO_STAGE, this.system_on_add_to_stage);
            } else {
                this.system_on_add_to_stage(null);
            }
        }
        private function system_on_add_to_stage(evt:Event):void {
            this.stage.scaleMode = StageScaleMode.NO_SCALE;
            this.stage.align = StageAlign.TOP_LEFT;
            
            // init context menu
            var myMenu:ContextMenu  = new ContextMenu();
            myMenu.hideBuiltInItems();
            myMenu.customItems.push(new ContextMenuItem("SRS带宽测试工具", true));
            this.contextMenu = myMenu;
            
            check_bandwidth();
        }
        
        private function check_bandwidth():void {
            // closure
            var self:srs_bwt = this;
            
            /////////////////////////////////////////////////////////////////////
            // initialize the bandwidth check/test library
            /////////////////////////////////////////////////////////////////////
            // js callback, set to null if ignore.
            var conf:Object = this.root.loaderInfo.parameters;
            var js_id:String = conf.id? conf.id:null;
            var js_on_ready:String = conf.on_bandwidth_ready? conf.on_bandwidth_ready:null;
            var js_on_srs_info:String = conf.on_srs_info? conf.on_srs_info:null;
            var js_on_progress_change:String = conf.on_update_progress? conf.on_update_progress:null;
            var js_on_status_change:String = conf.on_update_status? conf.on_update_status:null;
            var js_on_complete:String = conf.on_complete? conf.on_complete:null;
            
            // js export, set to null to disable
            var js_export_check_bandwidth:String = "__check_bandwidth";
            var js_export_stop:String = "__stop";
            
            // as callback, set to null if ignore.
            var as_on_ready:Function = function():void {
                self.on_ready();
            };
            var as_on_status_change:Function = function(code:String, data:String):void {
                self.on_status_change(code, data);
            };
            var as_on_progress_change:Function = function(percent:Number):void {
                self.on_progress(percent);
            };
            var as_on_srs_info:Function = function(srs_server:String, srs_primary:String, srs_authors:String, srs_id:String, srs_pid:String, srs_server_ip:String):void {
                self.update_context_items(srs_server, srs_primary, srs_authors, srs_id, srs_pid, srs_server_ip);
            };
            var as_on_complete:Function = function(start_time:Number, end_time:Number, play_kbps:Number, publish_kbps:Number, play_bytes:Number, publish_bytes:Number, play_time:Number, publish_time:Number):void {
                self.on_complete(start_time, end_time, play_kbps, publish_kbps, play_bytes, publish_bytes, play_time, publish_time);
            };
            
            bandwidth.initialize(
                as_on_ready, as_on_status_change, as_on_progress_change, as_on_srs_info, as_on_complete,
                js_id, js_on_ready, js_on_status_change, js_on_progress_change, js_on_srs_info, js_on_complete,
                js_export_check_bandwidth, js_export_stop
            );
            /////////////////////////////////////////////////////////////////////
        }
        
        private function on_ready():void {
            var conf:Object = this.root.loaderInfo.parameters;
            
            // for directly run swf.
            if (!conf.id) {
                trace("directly run swf, load default url: " + this.default_url);
                this.bandwidth.check_bandwidth(this.default_url);
            }
            
        }
        private function on_progress(percent:Number):void {
            trace("progress:" + percent + "%");
        }
        private function update_context_items(
            srs_server:String, srs_primary:String, srs_authors:String, 
            srs_id:String, srs_pid:String, srs_server_ip:String
        ):void {
            // for context menu
            var customItems:Array = [new ContextMenuItem("SrsPlayer")];
            if (srs_server != null) {
                customItems.push(new ContextMenuItem("Server: " + srs_server));
            }
            if (srs_primary != null) {
                customItems.push(new ContextMenuItem("Primary: " + srs_primary));
            }
            if (srs_authors != null) {
                customItems.push(new ContextMenuItem("Authors: " + srs_authors));
            }
            if (srs_server_ip != null) {
                customItems.push(new ContextMenuItem("SrsIp: " + srs_server_ip));
            }
            if (srs_pid != null) {
                customItems.push(new ContextMenuItem("SrsPid: " + srs_pid));
            }
            if (srs_id != null) {
                customItems.push(new ContextMenuItem("SrsId: " + srs_id));
            }
            contextMenu.customItems = customItems;
        }
        public function on_status_change(code:String, data:String): void {
            trace(code);
            switch(code){
                case "NetConnection.Connect.Failed":
                    trace("连接服务器失败！");
                    break;
                case "NetConnection.Connect.Rejected":
                    trace("服务器拒绝连接！");
                    break;
                case "NetConnection.Connect.Success":
                    trace("连接服务器成功!");
                    break;
                case SrsBandwidth.StatusSrsBwtcPlayStart:
                    trace("开始测试下行带宽");
                    break;
                case SrsBandwidth.StatusSrsBwtcPlayStop:
                    trace("下行带宽测试完毕，" + data + "kbps，开始测试上行带宽。");
                    break;
                case SrsBandwidth.StatusSrsBwtcPublishStart:
                    trace("开始测试上行带宽");
                    break;
                case SrsBandwidth.StatusSrsBwtcPublishStop:
                    trace("上行带宽测试完毕，" + data + "kbps，");
                    break;
                case "NetConnection.Connect.Closed":
                    trace("连接已断开!");
                    break;
            }
        }
        private function on_complete(
            start_time:Number, end_time:Number, play_kbps:Number, publish_kbps:Number, 
            play_bytes:Number, publish_bytes:Number, play_time:Number, publish_time:Number
        ):void {
            var status:String = "检测结束: 上行: " + publish_kbps + " kbps" + " 下行: " + play_kbps + " kbps"
                + " 测试时间: " + Number((end_time - start_time) / 1000).toFixed(1) + " 秒";
            trace(status);
        }
    }
}