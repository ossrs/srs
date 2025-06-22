#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Example usage of SRS Logger module
This file demonstrates how to import and use the SRS logger in other Python files.
"""

import sys
import argparse
import time
import traceback

# Import the SRS logger module
from srs_logger import get_logger, init_logger, log_error_and_exit
import srs_logger

def example_basic_usage():
    """Basic usage example"""
    logger = get_logger()
    
    logger.info("Starting basic usage example")
    logger.debug("This is a debug message")
    logger.warn("This is a warning message")
    logger.info("Basic usage example completed")

def example_with_config():
    """Example with config file parsing"""
    # This would typically be passed via --config argument
    config_path = "./conf/console.conf"
    
    logger = init_logger(config_path)
    logger.info(f"Logger initialized with config file: {config_path}")
    
    # Demonstrate various log levels
    logger.trace("Trace level message")
    logger.debug("Debug level message")
    logger.info("Info level message")
    logger.warning("Warning level message")
    logger.error("Error level message")

def example_error_handling():
    """Example of error handling and logging"""
    logger = get_logger()
    
    try:
        # Simulate some work that might fail
        logger.info("Starting risky operation")
        
        # Simulate an error condition
        if True:  # This would be your actual error condition
            raise RuntimeError("Simulated error in Python process")
            
        logger.info("Risky operation completed successfully")
        
    except Exception as e:
        # Log the error with full traceback
        logger.exception(f"Error in risky operation: {e}")
        
        # For fatal errors, you can use log_error_and_exit
        # log_error_and_exit(f"Fatal error occurred: {e}")
        
        # Or just log and continue
        logger.error(f"Continuing after error: {e}")

def example_convenience_functions():
    """Example using convenience functions"""
    # These functions use the global logger instance
    srs_logger.info("Using convenience function for info")
    srs_logger.warn("Using convenience function for warning")
    srs_logger.error("Using convenience function for error")

def analytics_simulation():
    """Simulate analytics processing with logging"""
    logger = get_logger()
    
    logger.info("Analytics process started")
    
    try:
        # Simulate analytics work
        for i in range(5):
            logger.debug(f"Processing analytics batch {i+1}/5")
            time.sleep(0.5)  # Simulate processing time
            
            if i == 3:  # Simulate a warning condition
                logger.warn(f"High CPU usage detected during batch {i+1}")
        
        logger.info("Analytics processing completed successfully")
        
    except KeyboardInterrupt:
        logger.warn("Analytics process interrupted by user")
        return False
    except Exception as e:
        logger.exception(f"Analytics process failed: {e}")
        return False
    
    return True

def main():
    """Main function demonstrating different usage patterns"""
    parser = argparse.ArgumentParser(description="SRS Logger Usage Examples")
    parser.add_argument("--config", help="Path to SRS config file")
    parser.add_argument("--port", type=int, default=8888, help="Port number (example parameter)")
    parser.add_argument("--example", choices=['basic', 'config', 'error', 'convenience', 'analytics'], 
                       default='analytics', help="Example to run")
    
    args = parser.parse_args()
    
    try:
        # Initialize logger with config if provided
        if args.config:
            logger = init_logger(args.config)
            logger.info(f"SRS Logger example started with config: {args.config}")
        else:
            logger = get_logger()
            logger.info("SRS Logger example started without config file")
        
        logger.info(f"Example parameters: port={args.port}, example={args.example}")
        
        # Run the selected example
        if args.example == 'basic':
            example_basic_usage()
        elif args.example == 'config':
            example_with_config()
        elif args.example == 'error':
            example_error_handling()
        elif args.example == 'convenience':
            example_convenience_functions()
        elif args.example == 'analytics':
            success = analytics_simulation()
            if not success:
                log_error_and_exit("Analytics simulation failed")
        
        logger.info("SRS Logger example completed successfully")
        
    except Exception as e:
        # Use the logger for any unhandled exceptions
        if 'logger' in locals():
            logger.exception(f"Unhandled exception in main: {e}")
        else:
            print(f"FATAL ERROR: {e}", file=sys.stderr)
            traceback.print_exc()
        
        sys.exit(1)

if __name__ == "__main__":
    main()
