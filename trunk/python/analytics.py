#!/usr/bin/env python3
"""
SRS Analytics Script
This demonstrates another Python process that can run alongside SRS.
"""

import time
import signal
import sys
import argparse
import logging
import json
from http.server import HTTPServer, BaseHTTPRequestHandler
from threading import Thread
from datetime import datetime

class AnalyticsHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        """Handle GET requests for analytics data"""
        if self.path == '/stats':
            # Return sample analytics data
            stats = {
                'timestamp': datetime.now().isoformat(),
                'connections': 42,
                'streams': 5,
                'bandwidth': '1.2 Mbps',
                'uptime': '2h 30m'
            }
            
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps(stats, indent=2).encode())
        else:
            self.send_response(404)
            self.end_headers()
            
    def log_message(self, format, *args):
        """Override to use our logger"""
        pass

class SRSAnalytics:
    def __init__(self, port=8888):
        self.port = port
        self.running = True
        self.server = None
        self.server_thread = None
        
        # Set up logging
        logging.basicConfig(
            level=logging.INFO,
            format='[%(asctime)s] [Python Analytics] %(levelname)s: %(message)s',
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
        if self.server:
            self.server.shutdown()
            
    def start_http_server(self):
        """Start the HTTP analytics server"""
        try:
            self.server = HTTPServer(('localhost', self.port), AnalyticsHandler)
            self.logger.info(f"Analytics server started on http://localhost:{self.port}")
            self.server.serve_forever()
        except Exception as e:
            self.logger.error(f"Error starting HTTP server: {e}")
            
    def run(self):
        """Main analytics loop"""
        self.logger.info(f"SRS Analytics started on port {self.port}")
        
        try:
            # Start HTTP server in a separate thread
            self.server_thread = Thread(target=self.start_http_server)
            self.server_thread.daemon = True
            self.server_thread.start()
            
            # Main analytics loop
            while self.running:
                # Simulate analytics work
                self.logger.debug("Processing analytics data...")
                
                # You can add your analytics logic here:
                # - Collect stream metrics
                # - Process viewer statistics
                # - Generate reports
                # - Store data to database
                
                time.sleep(10)  # Process every 10 seconds
                
        except Exception as e:
            self.logger.error(f"Error in analytics loop: {e}")
        finally:
            self.cleanup()
            
    def cleanup(self):
        """Cleanup before shutdown"""
        self.logger.info("Cleaning up Analytics server...")
        if self.server:
            self.server.shutdown()
        self.logger.info("Analytics server stopped")

def main():
    parser = argparse.ArgumentParser(description='SRS Analytics Server')
    parser.add_argument('--port', type=int, default=8888, help='HTTP server port')
    
    args = parser.parse_args()
    
    analytics = SRSAnalytics(args.port)
    analytics.run()

if __name__ == '__main__':
    main()
