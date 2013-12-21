package SrsClass
{	
	import flash.system.System;
	
	public class SrsElapsedTimer
	{
		private var beginDate:Date;
		public function SrsElapsedTimer()
		{
			beginDate = new Date;
		}
		
		public function elapsed():Number{
			var endDate:Date = new Date;

			// get deiff by ms
			return (endDate.time - 	beginDate.time);	
		}
		
		public function restart():void{
			beginDate = new Date;
		}
	}
}