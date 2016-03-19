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
         * initialize the player by flashvars for config.
         * @param flashvars the config.
         */
        public static function stringEndswith(s:String, f:String):Boolean {
            return s && f && s.indexOf(f) == s.length - f.length;
        }

        /**
         * write log to trace and console.log.
         * @param msg the log message.
         */
        public static function log(js_id:String, msg:String):void {
            msg = "[" + new Date() +"][srs-player][" + js_id + "] " + msg;

            logData += msg + "\n";

            trace(msg);

            if (!flash.external.ExternalInterface.available) {
                flash.utils.setTimeout(log, 300, msg);
                return;
            }

            ExternalInterface.call("console.log", msg);
        }
    }
}