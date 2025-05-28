#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
SRS Python Addon - Simple HTTP Server
This addon demonstrates how to create a simple HTTP server as an SRS addon.
"""

import sys
import signal
import time
import logging
import argparse
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger('srs_http_addon')

class SRSAddonHandler(BaseHTTPRequestHandler):
    """Simple HTTP request handler for SRS addon."""
    
    def do_GET(self):
        """Handle GET requests."""
        if self.path == '/':
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            response = '''
            <html>
            <body>
                <h1>SRS Python Addon - HTTP Server</h1>
                <p>This is a simple HTTP server running as an SRS addon.</p>
                <p>Status: Running</p>
                <p>Time: {}</p>
            </body>
            </html>
            '''.format(time.strftime('%Y-%m-%d %H:%M:%S'))
            self.wfile.write(response.encode())
        elif self.path == '/status':
            self.send_response(200)
            self.send_header('Content-type', 'application/json')
            self.end_headers()
            response = '{"status": "running", "time": "%s"}' % time.strftime('%Y-%m-%d %H:%M:%S')
            self.wfile.write(response.encode())
        else:
            self.send_response(404)
            self.end_headers()
    
    def log_message(self, format, *args):
        """Override to use our logger."""
        logger.info("%s - %s" % (self.address_string(), format % args))

class SRSHTTPAddon:
    """SRS HTTP Addon main class."""
    
    def __init__(self, port=8888):
        self.port = port
        self.server = None
        self.running = False
        self.server_thread = None
        
    def start(self):
        """Start the HTTP server."""
        try:
            self.server = HTTPServer(('', self.port), SRSAddonHandler)
            self.running = True
            
            # Start server in a separate thread
            self.server_thread = threading.Thread(target=self._run_server)
            self.server_thread.daemon = True
            self.server_thread.start()
            
            logger.info(f"SRS HTTP addon started on port {self.port}")
            return True
        except Exception as e:
            logger.error(f"Failed to start HTTP addon: {e}")
            return False
    
    def stop(self):
        """Stop the HTTP server."""
        if self.server and self.running:
            self.running = False
            self.server.shutdown()
            self.server.server_close()
            if self.server_thread:
                self.server_thread.join(timeout=5)
            logger.info("SRS HTTP addon stopped")
    
    def _run_server(self):
        """Run the HTTP server."""
        try:
            self.server.serve_forever()
        except Exception as e:
            if self.running:
                logger.error(f"HTTP server error: {e}")

def signal_handler(signum, frame):
    """Handle termination signals."""
    logger.info(f"Received signal {signum}, shutting down...")
    if 'addon' in globals():
        addon.stop()
    sys.exit(0)

def main():
    """Main function."""
    parser = argparse.ArgumentParser(description='SRS HTTP Addon')
    parser.add_argument('--port', type=int, default=8888, help='HTTP server port')
    parser.add_argument('--verbose', action='store_true', help='Enable verbose logging')
    
    args = parser.parse_args()
    
    if args.verbose:
        logger.setLevel(logging.DEBUG)
    
    # Set up signal handlers
    signal.signal(signal.SIGTERM, signal_handler)
    signal.signal(signal.SIGINT, signal_handler)
    
    # Create and start the addon
    global addon
    addon = SRSHTTPAddon(args.port)
    
    if addon.start():
        logger.info("SRS HTTP addon is running, press Ctrl+C to stop")
        try:
            while addon.running:
                time.sleep(1)
        except KeyboardInterrupt:
            logger.info("Interrupted by user")
        finally:
            addon.stop()
    else:
        logger.error("Failed to start SRS HTTP addon")
        sys.exit(1)

if __name__ == '__main__':
    main()