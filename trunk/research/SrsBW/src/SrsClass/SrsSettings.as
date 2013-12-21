package SrsClass
{
	import flash.net.SharedObject;
	
	public class SrsSettings
	{
		private var settings:SharedObject;
		private var key:String = "SrsBandCheck";
		
		public function SrsSettings()
		{
			settings = SharedObject.getLocal(key);
		}
		
		public function addAddressText(val:String):void{
			settings.data.address_text = val;
		}
		
		public function addressText():String{
			return settings.data.address_text;
		}
		
		static public function instance():SrsSettings{
			return new SrsSettings;
		}
	}
}