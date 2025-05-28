#!/usr/bin/env python3
"""
SRS Python Monitor Script
This is an example Python script that runs alongside SRS server.
It demonstrates how Python processes can be managed by SRS.
"""

import time
import signal
import sys
import argparse
import logging
from datetime import datetime

class SRSMonitor:
    def __init__(self, config_file=None, verbose=False):
        self.running = True
        self.config_file = config_file
        self.verbose = verbose
        
        # Set up logging
        level = logging.DEBUG if verbose else logging.INFO
        logging.basicConfig(
            level=level,
            format='[%(asctime)s] [Python Monitor] %(levelname)s: %(message)s',
            datefmt='%Y-%m-%d %H:%M:%S'
        )
        self.logger = logging.getLogger(__name__)
        
        # Set up signal handlers
        signal.signal(signal.SIGTERM, self.signal_handler)
        signal.signal(signal.SIGINT, self.signal_handler)
        
    def signal_handler(self, signum, frame):
        """Handle shutdown signals from SRS"""
        self.logger.info(f"Received signal {signum}, shutting down gracefully...")
        self.running = False
        
    def run(self):
        """Main monitoring loop"""
        self.logger.info("SRS Python Monitor started")
        if self.config_file:
            self.logger.info(f"Using config file: {self.config_file}")
            
        try:
            while self.running:
                # Simulate monitoring work
                self.logger.debug(f"Monitor heartbeat at {datetime.now()}")
                
                # You can add your monitoring logic here:
                # - Check stream status
                # - Monitor server health
                # - Send alerts
                # - Log analytics
                
                time.sleep(5)  # Check every 5 seconds
                
        except Exception as e:
            self.logger.error(f"Error in monitor loop: {e}")
        finally:
            self.cleanup()
            
    def cleanup(self):
        """Cleanup before shutdown"""
        self.logger.info("Cleaning up Python Monitor...")
        # Add any cleanup logic here
        self.logger.info("Python Monitor stopped")

def main():
    parser = argparse.ArgumentParser(description='SRS Python Monitor')
    parser.add_argument('--config', help='Configuration file path')
    parser.add_argument('--verbose', action='store_true', help='Enable verbose logging')
    
    args = parser.parse_args()
    
    monitor = SRSMonitor(args.config, args.verbose)
    monitor.run()

if __name__ == '__main__':
    main()
