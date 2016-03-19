package
{
    import flash.external.ExternalInterface;
    import flash.utils.setTimeout;

    /**
     * the utility functions.
     */
    public class Utility
    {
        /**
         * total log.
         */
        public static var logData:String = "";

        /**
         * whether string s endswith f.
         */
        public static function stringEndswith(s:String, f:String):Boolean {
            return s && f && s.indexOf(f) == s.length - f.length;
        }

        /**
         * whether string s startswith f.
         */
        public static function stringStartswith(s:String, f:String):Boolean {
            return s && f && s.indexOf(f) == 0;
        }

        /**
         * write log to trace and console.log.
         * @param msg the log message.
         */
        public static function log(js_id:String, msg:String):void {
			if (js_id) {
            	msg = "[" + new Date() +"][srs-player][" + js_id + "] " + msg;
			}

            logData += msg + "\n";

            trace(msg);

            if (!flash.external.ExternalInterface.available) {
                flash.utils.setTimeout(log, 300, null, msg);
                return;
            }

            ExternalInterface.call("console.log", msg);
        }
    }
}