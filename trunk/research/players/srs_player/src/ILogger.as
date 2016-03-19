package
{
    public interface ILogger
    {
        function debug0(message:String, ... rest):void;
        function debug(message:String, ... rest):void;
        function info(message:String, ... rest):void;
        function warn(message:String, ... rest):void;
        function error(message:String, ... rest):void;
        function fatal(message:String, ... rest):void;
    }
}