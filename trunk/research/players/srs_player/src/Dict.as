package
{
    import flash.utils.Dictionary;

    public class Dict 
    {
        private var _dict:Dictionary;
        private var _size:uint;

        public function Dict()
        {
            clear();
        }
        
        /**
        * get the underlayer dict.
        * @remark for core-ng.
        */
        public function get dict():Dictionary
        {
            return _dict;
        }

        public function has(key:Object):Boolean
        {
            return (key in _dict);
        }

        public function get(key:Object):Object
        {
            return ((key in _dict) ? _dict[key] : null);
        }

        public function set(key:Object, object:Object):void
        {
            if (!(key in _dict)) 
            {
                _size++;
            }
            _dict[key] = object;
        }

        public function remove(key:Object):Object
        {
            var object:Object;
            if (key in _dict) 
            {
                object = _dict[key];
                delete _dict[key];
                _size--;
            }
            return (object);
        }

        public function keys():Array
        {
            var array:Array = new Array(_size);
            var index:int;
            for (var key:Object in _dict)
            {
                var _local6:int = index++;
                array[_local6] = key;
            }
            return (array);
        }

        public function values():Array
        {
            var array:Array = new Array(_size);
            var index:int;
            for each (var value:Object in _dict)
            {
                var _local6:int = index++;
                array[_local6] = value;
            };
            return (array);
        }

        public function clear():void
        {
            _dict = new Dictionary();
            _size = 0;
        }

        public function toArray():Array
        {
            var array:Array = new Array(_size * 2);
            var index:int;
            for (var key:Object in _dict) 
            {
                var _local6:int = index++;
                array[_local6] = key;
                var _local7:int = index++;
                array[_local7] = _dict[key];
            };
            return (array);
        }

        public function toObject():Object
        {
            return (toArray());
        }

        public function fromObject(object:Object):void
        {
            clear();
            var index:uint;
            while (index < (object as Array).length) {
                set((object as Array)[index], (object as Array)[(index + 1)]);
                index += 2;
            };
        }

        public function get size():uint
        {
            return (_size);
        }

    }
}