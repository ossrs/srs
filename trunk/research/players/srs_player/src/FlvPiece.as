package
{
    import flash.utils.ByteArray;

    /**
    * a piece of flv, fetch from cdn or p2p.
    */
    public class FlvPiece
    {
        private var _pieceId:Number;
        protected var _flv:ByteArray;
        /**
        * the private object for the channel,
        * for example, the cdn channel will set to CdnEdge object.
        */
        private var _privateObject:Object;
        /**
        * when encoder error, this piece cannot be generated,
        * and it should be skip. default to false.
        */
        private var _skip:Boolean;
        
        public function FlvPiece(pieceId:Number)
        {
            _pieceId = pieceId;
            _flv = null;
            _skip = false;
        }
        
        /**
         * when piece is fetch ok.
         */
        public function onPieceDone(flv:ByteArray):void
        {
            // save body.
            _flv = flv;
        }
        
        /**
         * when piece is fetch error.
         */
        public function onPieceError():void
        {
        }
        
        /**
         * when piece is empty.
         */
        public function onPieceEmpty():void
        {
        }
        
        /**
        * destroy the object, set reference to null.
        */
        public function destroy():void
        {
            _privateObject = null;
            _flv = null;
        }
        
        public function get privateObject():Object
        {
            return _privateObject;
        }
        
        public function set privateObject(v:Object):void
        {
            _privateObject = v;
        }
        
        public function get skip():Boolean
        {
            return _skip;
        }
        
        public function set skip(v:Boolean):void
        {
            _skip = v;
        }
        
        public function get pieceId():Number
        {
            return _pieceId;
        }
        
        public function get flv():ByteArray
        {
            return _flv;
        }
        
        public function get completed():Boolean
        {
            return _flv != null;
        }
    }
}