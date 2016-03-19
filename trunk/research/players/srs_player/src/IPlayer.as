package
{
    import flash.net.NetStream;

    /**
     * the player interface.
     */
    public interface IPlayer
    {
        /**
         * initialize the player by flashvars for config.
         * @param flashvars the config.
         */
        function init(flashvars:Object):void;

        /**
         * get the NetStream to play the stream.
         * @return the underlayer stream object.
         */
        function stream():NetStream;

        /**
         * connect and play url.
         * @param url the stream url to play.
         */
        function play(url:String):void;

        /**
         * close the player.
         */
        function close():void;
    }
}