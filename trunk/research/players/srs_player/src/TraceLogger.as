package
{
	import flash.globalization.DateTimeFormatter;

    public class TraceLogger implements ILogger
    {
        private var _category:String;

        public function get category():String
        {
            return _category;
        }
        public function TraceLogger(category:String)
        {
            _category = category;
        }
        public function debug0(message:String, ...rest):void
        {
        }

        public function debug(message:String, ...rest):void
        {
        }

        public function info(message:String, ...rest):void
        {
            logMessage(LEVEL_INFO, message, rest);
        }

        public function warn(message:String, ...rest):void
        {
            logMessage(LEVEL_WARN, message, rest);
        }

        public function error(message:String, ...rest):void
        {
            logMessage(LEVEL_ERROR, message, rest);
        }

        public function fatal(message:String, ...rest):void
        {
            logMessage(LEVEL_FATAL, message, rest);
        }
        protected function logMessage(level:String, message:String, params:Array):void
        {
            var msg:String = "";

            // add datetime
            var date:Date = new Date();
            var dtf:DateTimeFormatter = new DateTimeFormatter("UTC");
            dtf.setDateTimePattern("yyyy-MM-dd HH:mm:ss");

            // TODO: FIXME: the SSS format not run, use date.milliseconds instead.
            msg += '[' + dtf.format(date) + "." + date.milliseconds + ']';
            msg += " [" + level + "] ";

            // add category and params
            msg += "[" + category + "] " + applyParams(message, params);

            // trace the message
            Utility.log("CORE", msg);
        }
        private function leadingZeros(x:Number):String
        {
            if (x < 10) {
                return "00" + x.toString();
            }

            if (x < 100) {
                return "0" + x.toString();
            }

            return x.toString();
        }
        private function applyParams(message:String, params:Array):String
        {
            var result:String = message;

            var numParams:int = params.length;

            for (var i:int = 0; i < numParams; i++) {
                result = result.replace(new RegExp("\\{" + i + "\\}", "g"), params[i]);
            }
            return result;
        }

        private static const LEVEL_DEBUG:String = "DEBUG";
        private static const LEVEL_WARN:String = "WARN";
        private static const LEVEL_INFO:String = "INFO";
        private static const LEVEL_ERROR:String = "ERROR";
        private static const LEVEL_FATAL:String = "FATAL";
    }
}