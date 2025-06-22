#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SRS Python Logger Module
Provides synchronized logging with SRS main process.
"""

import logging
import os
import sys
import threading
import time
import re
from datetime import datetime
from typing import Optional, Dict, Any
import argparse

class SRSLogFormatter(logging.Formatter):
    """
    Custom formatter that matches SRS log format with color support:
    [2025-05-30 21:54:18.835][WARN][3448][python_analytics] message
    """
    
    # ANSI color codes
    COLORS = {
        'RESET': '\033[0m',
        'RED': '\033[31m',
        'YELLOW': '\033[33m',
        'WHITE': '\033[37m',
        'CYAN': '\033[36m',
        'GREEN': '\033[32m'
    }
    
    def __init__(self, use_colors=True):
        super().__init__()
        self.pid = os.getpid()
        self.session_id = self._generate_session_id()
        self.use_colors = use_colors and self._supports_color()
    
    def _supports_color(self) -> bool:
        """Check if the terminal supports color output"""
        import sys
        
        # Check if we're in a terminal
        if not hasattr(sys.stdout, 'isatty') or not sys.stdout.isatty():
            return False
        
        # Check for Windows terminal support
        if os.name == 'nt':
            try:
                import colorama
                colorama.init()
                return True
            except ImportError:
                # Try to enable ANSI escape sequence support on Windows
                try:
                    import ctypes
                    kernel32 = ctypes.windll.kernel32
                    kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
                    return True
                except:
                    return False
        
        # Unix-like systems usually support colors
        return True
    
    def _generate_session_id(self) -> str:
        """Generate a meaningful session ID based on the calling script"""
        import inspect
        
        # Try to get the main script name
        main_module = sys.modules.get('__main__')
        if main_module and hasattr(main_module, '__file__'):
            script_path = main_module.__file__
            if script_path:
                script_name = os.path.splitext(os.path.basename(script_path))[0]
                return f"python_{script_name}"
        
        # Fallback to inspect the call stack to find the calling script
        try:
            for frame_info in inspect.stack():
                filename = frame_info.filename
                if filename and not filename.endswith('srs_logger.py') and not filename.endswith('logging/__init__.py'):
                    script_name = os.path.splitext(os.path.basename(filename))[0]
                    if script_name != '<string>' and script_name != '<stdin>':
                        return f"python_{script_name}"
        except:
            pass
        
        # Final fallback
        return "python_unknown"
    
    def format(self, record: logging.LogRecord) -> str:
        """Format log record to match SRS format with color support"""
        # Get current timestamp with milliseconds
        now = datetime.now()
        timestamp = now.strftime("%Y-%m-%d %H:%M:%S.") + f"{now.microsecond // 1000:03d}"
        
        # Map Python log levels to SRS log levels
        level_mapping = {
            'DEBUG': 'TRACE',
            'INFO': 'INFO',
            'WARNING': 'WARN',
            'ERROR': 'ERROR',
            'CRITICAL': 'ERROR'
        }
        
        srs_level = level_mapping.get(record.levelname, 'INFO')
        
        # Format: [timestamp][level][pid][session] message
        formatted_msg = f"[{timestamp}][{srs_level}][{self.pid}][{self.session_id}] {record.getMessage()}"
        
        if record.exc_info:
            formatted_msg += f"\n{self.formatException(record.exc_info)}"
        
        # Apply colors if supported and enabled
        if self.use_colors:
            if srs_level == 'ERROR':
                formatted_msg = f"{self.COLORS['RED']}{formatted_msg}{self.COLORS['RESET']}"
            elif srs_level == 'WARN':
                formatted_msg = f"{self.COLORS['YELLOW']}{formatted_msg}{self.COLORS['RESET']}"
            # INFO, TRACE levels remain uncolored (default terminal color)
            
        return formatted_msg

class SRSConfigParser:
    """Parser for SRS configuration files"""
    
    @staticmethod
    def parse_config_file(config_path: str) -> Dict[str, Any]:
        """Parse SRS configuration file and extract logging settings"""
        config = {
            'log_tank': 'all',
            'log_file': './objs/srs.log',
            'log_level': 'info'
        }
        
        if not os.path.exists(config_path):
            return config
            
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                content = f.read()
                
            # Parse srs_log_tank
            match = re.search(r'srs_log_tank\s+([^;]+);', content)
            if match:
                config['log_tank'] = match.group(1).strip()
                
            # Parse srs_log_file
            match = re.search(r'srs_log_file\s+([^;]+);', content)
            if match:
                config['log_file'] = match.group(1).strip()
                
            # Parse srs_log_level_v2
            match = re.search(r'srs_log_level_v2\s+([^;]+);', content)
            if match:
                config['log_level'] = match.group(1).strip().lower()
                
        except Exception as e:
            print(f"Warning: Failed to parse config file {config_path}: {e}", file=sys.stderr)
            
        return config

class SRSLogger:
    """
    SRS-compatible Python logger that synchronizes with SRS main process logging
    """
    
    _instance = None
    _lock = threading.Lock()
    
    def __new__(cls, config_path: Optional[str] = None):
        """Singleton pattern to ensure only one logger instance"""
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
                    cls._instance._initialized = False
        return cls._instance
    
    def __init__(self, config_path: Optional[str] = None):
        if self._initialized:
            return
            
        self._initialized = True
        self.config_path = config_path
        self.logger = None
        self.config = {}
        self._setup_logger()
    
    def _setup_logger(self):
        """Setup the logger based on SRS configuration"""
        try:
            # Parse SRS config if provided
            if self.config_path and os.path.exists(self.config_path):
                self.config = SRSConfigParser.parse_config_file(self.config_path)
            else:
                # Default configuration
                self.config = {
                    'log_tank': 'all',
                    'log_file': './objs/srs.log',
                    'log_level': 'info'
                }
            
            # Create logger
            self.logger = logging.getLogger('srs_python')
            self.logger.setLevel(self._get_python_log_level(self.config['log_level']))
            
            # Clear existing handlers
            self.logger.handlers.clear()
            
            # Setup handlers based on log_tank setting
            log_tank = self.config['log_tank'].lower()
            
            if log_tank in ['all', 'console']:
                # Console handler with colors
                console_handler = logging.StreamHandler(sys.stdout)
                console_formatter = SRSLogFormatter(use_colors=True)
                console_handler.setFormatter(console_formatter)
                self.logger.addHandler(console_handler)
            
            if log_tank in ['all', 'file']:
                # File handler without colors
                log_file = self.config['log_file']
                # Create directory if it doesn't exist
                log_dir = os.path.dirname(log_file)
                if log_dir and not os.path.exists(log_dir):
                    os.makedirs(log_dir, exist_ok=True)
                
                file_handler = logging.FileHandler(log_file, encoding='utf-8')
                file_formatter = SRSLogFormatter(use_colors=False)
                file_handler.setFormatter(file_formatter)
                self.logger.addHandler(file_handler)
            
            # Prevent propagation to root logger
            self.logger.propagate = False
            
            self.info(f"SRS Python logger initialized with config: {self.config}")
            
        except Exception as e:
            print(f"ERROR: Failed to setup SRS logger: {e}", file=sys.stderr)
            # Setup a basic console logger as fallback
            self._setup_fallback_logger()
    
    def _setup_fallback_logger(self):
        """Setup a basic fallback logger if main setup fails"""
        self.logger = logging.getLogger('srs_python_fallback')
        self.logger.setLevel(logging.INFO)
        
        console_handler = logging.StreamHandler(sys.stderr)
        formatter = logging.Formatter('%(asctime)s [ERROR] [Python] %(message)s')
        console_handler.setFormatter(formatter)
        
        self.logger.addHandler(console_handler)
        self.logger.propagate = False
    
    def _get_python_log_level(self, srs_level: str) -> int:
        """Convert SRS log level to Python log level"""
        level_mapping = {
            'trace': logging.DEBUG,
            'debug': logging.DEBUG,
            'info': logging.INFO,
            'warn': logging.WARNING,
            'warning': logging.WARNING,
            'error': logging.ERROR
        }
        return level_mapping.get(srs_level.lower(), logging.INFO)
    
    def trace(self, message: str, *args, **kwargs):
        """Log trace message (mapped to DEBUG in Python)"""
        if self.logger:
            self.logger.debug(message, *args, **kwargs)
    
    def debug(self, message: str, *args, **kwargs):
        """Log debug message"""
        if self.logger:
            self.logger.debug(message, *args, **kwargs)
    
    def info(self, message: str, *args, **kwargs):
        """Log info message"""
        if self.logger:
            self.logger.info(message, *args, **kwargs)
    
    def warn(self, message: str, *args, **kwargs):
        """Log warning message"""
        if self.logger:
            self.logger.warning(message, *args, **kwargs)
    
    def warning(self, message: str, *args, **kwargs):
        """Log warning message"""
        if self.logger:
            self.logger.warning(message, *args, **kwargs)
    
    def error(self, message: str, *args, **kwargs):
        """Log error message"""
        if self.logger:
            self.logger.error(message, *args, **kwargs)
    
    def critical(self, message: str, *args, **kwargs):
        """Log critical message (mapped to ERROR in SRS)"""
        if self.logger:
            self.logger.critical(message, *args, **kwargs)
    
    def exception(self, message: str, *args, **kwargs):
        """Log exception with traceback"""
        if self.logger:
            self.logger.exception(message, *args, **kwargs)

# Global logger instance
_global_logger: Optional[SRSLogger] = None

def get_logger(config_path: Optional[str] = None) -> SRSLogger:
    """
    Get the global SRS logger instance
    
    Args:
        config_path: Path to SRS configuration file
        
    Returns:
        SRSLogger instance
    """
    global _global_logger
    
    if _global_logger is None:
        _global_logger = SRSLogger(config_path)
    
    return _global_logger

def init_logger(config_path: Optional[str] = None) -> SRSLogger:
    """
    Initialize the SRS logger with configuration
    
    Args:
        config_path: Path to SRS configuration file
        
    Returns:
        SRSLogger instance
    """
    return get_logger(config_path)

# Convenience functions for direct logging
def trace(message: str, *args, **kwargs):
    """Log trace message"""
    get_logger().trace(message, *args, **kwargs)

def debug(message: str, *args, **kwargs):
    """Log debug message"""
    get_logger().debug(message, *args, **kwargs)

def info(message: str, *args, **kwargs):
    """Log info message"""
    get_logger().info(message, *args, **kwargs)

def warn(message: str, *args, **kwargs):
    """Log warning message"""
    get_logger().warn(message, *args, **kwargs)

def warning(message: str, *args, **kwargs):
    """Log warning message"""
    get_logger().warning(message, *args, **kwargs)

def error(message: str, *args, **kwargs):
    """Log error message"""
    get_logger().error(message, *args, **kwargs)

def critical(message: str, *args, **kwargs):
    """Log critical message"""
    get_logger().critical(message, *args, **kwargs)

def exception(message: str, *args, **kwargs):
    """Log exception with traceback"""
    get_logger().exception(message, *args, **kwargs)

def log_error_and_exit(message: str, exit_code: int = 1):
    """
    Log an error message and exit the process
    Used when Python process encounters fatal errors
    """
    error(f"FATAL: {message}")
    sys.exit(exit_code)

if __name__ == "__main__":
    # Test the logger
    parser = argparse.ArgumentParser(description="SRS Python Logger Test")
    parser.add_argument("--config", help="Path to SRS config file")
    args = parser.parse_args()
    
    # Initialize logger
    logger = init_logger(args.config)
    
    # Test different log levels
    logger.trace("This is a trace message")
    logger.debug("This is a debug message")
    logger.info("SRS Python logger test started")
    logger.warn("This is a warning message")
    logger.error("This is an error message")
    
    # Test exception logging
    try:
        raise ValueError("Test exception")
    except Exception:
        logger.exception("Caught test exception")
    
    logger.info("SRS Python logger test completed")
