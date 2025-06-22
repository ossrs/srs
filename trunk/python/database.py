#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Database module for JWT authentication system
Provides SQLite database initialization and management for user authentication
"""

import sqlite3
import os
import json
import re
import threading
import uuid
from datetime import datetime, timedelta
from typing import Optional, List, Dict, Any, Union
from contextlib import contextmanager

import random
import string

# Import SRS logger
from srs_logger import get_logger

class Database:
    
    _instance = None
    _lock = threading.Lock()
    
    def __new__(cls, db_path: str = "./objs/srs_database.db"):
        """Singleton pattern to ensure only one database instance"""
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
                    cls._instance._initialized = False
        return cls._instance
    
    def __init__(self, db_path: str = "./objs/srs_database.db"):
        if self._initialized:
            return
            
        self._initialized = True
        self.db_path = db_path
        self.logger = get_logger()
        self._connection_lock = threading.Lock()
        
        # Initialize database
        self._init_database()
    
    def _init_database(self):
        """Initialize database and create tables if they don't exist"""
        try:
            # Create objs directory if it doesn't exist
            db_dir = os.path.dirname(self.db_path)
            if db_dir and not os.path.exists(db_dir):
                os.makedirs(db_dir, exist_ok=True)
                self.logger.info(f"Created database directory: {db_dir}")
            
            # Check if database exists
            db_exists = os.path.exists(self.db_path)
            if not db_exists:
                self.logger.warn(f"Database file not found, creating new database: {self.db_path}")
            else:
                self.logger.info(f"Using existing database: {self.db_path}")
            
            # Create tables
            self._create_tables()
            
            if not db_exists:
                self.logger.info("Database initialized successfully with all tables created")
            else:
                self.logger.info("Database connection established and tables verified")
                
        except Exception as e:
            self.logger.exception(f"Failed to initialize database: {e}")
            raise
    
    def _create_tables(self):
        """Create all required tables"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                
                # Create users table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS users (
                        user_id TEXT PRIMARY KEY,
                        username TEXT UNIQUE NOT NULL,
                        name TEXT NOT NULL,
                        email TEXT UNIQUE,
                        hashed_password TEXT NOT NULL,
                        salt TEXT NOT NULL,
                        user_group TEXT NOT NULL DEFAULT '["user"]',
                        last_active TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                        is_activated BOOLEAN NOT NULL DEFAULT 1
                    )
                ''')
                
                # Create auth_session table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS auth_session (
                        session_id TEXT PRIMARY KEY,
                        user_id TEXT NOT NULL,
                        hashed_authkey TEXT NOT NULL,
                        salt TEXT NOT NULL,
                        expire_time TIMESTAMP NOT NULL,
                        FOREIGN KEY (user_id) REFERENCES users (user_id) ON DELETE CASCADE
                    )
                ''')
                
                # Create refresh_session table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS refresh_session (
                        session_id TEXT PRIMARY KEY,
                        user_id TEXT NOT NULL,
                        hashed_refreshkey TEXT NOT NULL,
                        salt TEXT NOT NULL,
                        expire_time TIMESTAMP NOT NULL,
                        FOREIGN KEY (user_id) REFERENCES users (user_id) ON DELETE CASCADE
                    )
                ''')

                # Create streams table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS streams (
                        stream_id TEXT PRIMARY KEY,
                        stream_code TEXT UNIQUE NOT NULL,
                        streamer_id TEXT NOT NULL,
                        stream_title TEXT NOT NULL,
                        stream_description TEXT,
                        stream_tags TEXT DEFAULT '[]',
                        stream_visibility TEXT NOT NULL DEFAULT 'public',
                        quality_info TEXT,
                        active_time TIMESTAMP,
                        stream_status TEXT NOT NULL DEFAULT 'planned',
                        FOREIGN KEY (streamer_id) REFERENCES users (user_id) ON DELETE CASCADE
                    )
                ''')

                # Create system_stats table
                cursor.execute('''
                    CREATE TABLE IF NOT EXISTS system_stats (
                        timestamp TIMESTAMP PRIMARY KEY,
                        srs_uptime REAL NOT NULL,
                        srs_cpu_percent REAL NOT NULL,
                        srs_memory_percent REAL NOT NULL,
                        srs_recv_KBps REAL NOT NULL,
                        srs_send_KBps REAL NOT NULL,
                        disk_read_KBps REAL NOT NULL,
                        disk_write_KBps REAL NOT NULL,
                        os_uptime REAL NOT NULL,
                        os_cpu_percent REAL NOT NULL,
                        os_memory_percent REAL NOT NULL
                    )
                ''')
                
                # Create indexes for better performance
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_users_username ON users(username)')
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_users_email ON users(email)')
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_auth_session_user_id ON auth_session(user_id)')
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_auth_session_expire ON auth_session(expire_time)')
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_refresh_session_user_id ON refresh_session(user_id)')
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_refresh_session_expire ON refresh_session(expire_time)')
                cursor.execute('CREATE INDEX IF NOT EXISTS idx_system_stats_timestamp ON system_stats(timestamp)')

                conn.commit()
                self.logger.debug("All database tables and indexes created/verified successfully")
                
        except Exception as e:
            self.logger.exception(f"Failed to create database tables: {e}")
            raise
    
    @contextmanager
    def get_connection(self):
        """Get a database connection with proper error handling and cleanup"""
        conn = None
        try:
            conn = sqlite3.connect(self.db_path, timeout=30.0)
            conn.row_factory = sqlite3.Row  # Enable column access by name
            conn.execute('PRAGMA foreign_keys = ON')  # Enable foreign key constraints
            # Configure datetime adapter for Python 3.12+ compatibility
            sqlite3.register_adapter(datetime, lambda dt: dt.isoformat())
            sqlite3.register_converter("TIMESTAMP", lambda b: datetime.fromisoformat(b.decode()))
            yield conn
        except Exception as e:
            if conn:
                conn.rollback()
            self.logger.error(f"Database connection error: {e}")
            raise
        finally:
            if conn:
                conn.close()
    
    # ============================================================================
    # Users management functions
    # ============================================================================

    def cleanup_expired_sessions(self):
        """Remove expired auth and refresh sessions, and sessions for deactivated users"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                current_time = datetime.now()
                
                # Clean up expired auth sessions
                cursor.execute('''
                    DELETE FROM auth_session 
                    WHERE expire_time < ?
                ''', (current_time,))
                auth_expired_deleted = cursor.rowcount
                
                # Clean up expired refresh sessions
                cursor.execute('''
                    DELETE FROM refresh_session 
                    WHERE expire_time < ?
                ''', (current_time,))
                refresh_expired_deleted = cursor.rowcount
                
                # Clean up sessions for deactivated users
                cursor.execute('''
                    DELETE FROM auth_session 
                    WHERE user_id IN (SELECT user_id FROM users WHERE is_activated = 0)
                ''')
                auth_deactivated_deleted = cursor.rowcount
                
                cursor.execute('''
                    DELETE FROM refresh_session 
                    WHERE user_id IN (SELECT user_id FROM users WHERE is_activated = 0)
                ''')
                refresh_deactivated_deleted = cursor.rowcount
                
                conn.commit()
                
                total_auth_deleted = auth_expired_deleted + auth_deactivated_deleted
                total_refresh_deleted = refresh_expired_deleted + refresh_deactivated_deleted

                if total_auth_deleted > 0 or total_refresh_deleted > 0:
                    self.logger.info(f"Cleaned up sessions: {total_auth_deleted} auth ({auth_expired_deleted} expired, {auth_deactivated_deleted} deactivated), {total_refresh_deleted} refresh ({refresh_expired_deleted} expired, {refresh_deactivated_deleted} deactivated)")

        except Exception as e:
            self.logger.exception(f"Failed to cleanup expired sessions: {e}")
    
    def get_user(self, username_or_email: str) -> Optional[Dict[str, Any]]:
        """Get user by username or email"""
        if not username_or_email:
            self.logger.warn("get_user called with None or empty value")
            return None
            
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    SELECT user_id, username, name, email, hashed_password, salt, 
                           user_group, last_active, is_activated
                    FROM users WHERE username = ? OR email = ?
                ''', (username_or_email, username_or_email))

                row = cursor.fetchone()
                if row:
                    user_data = dict(row)
                    user_data['user_group'] = json.loads(user_data['user_group'])
                    return user_data
                else:
                    return None

        except Exception as e:
            self.logger.exception(f"Failed to get user by username/email '{username_or_email}': {e}")
            return None
    
    def get_user_by_id(self, user_id: str) -> Optional[Dict[str, Any]]:
        """Get user by user_id"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    SELECT user_id, username, name, email, hashed_password, salt, 
                           user_group, last_active, is_activated
                    FROM users WHERE user_id = ?
                ''', (user_id,))
                
                row = cursor.fetchone()
                if row:
                    user_data = dict(row)
                    user_data['user_group'] = json.loads(user_data['user_group'])
                    return user_data
                else: 
                    return None
                
        except Exception as e:
            self.logger.exception(f"Failed to get user by user_id {user_id}: {e}")
            return None
    
    def create_user(self, username: str, name: str, email: Optional[str], 
                   hashed_password: str, salt: str, user_group: List[str] = None, 
                   is_activated: bool = True) -> Optional[Dict[str, Any]]:
        """Create a new user and return user data"""
        try:
            # Validate username format
            if not re.match(r'^[a-zA-Z0-9_-]+$', username):
                self.logger.warn(f"Invalid username format: '{username}'. Only a-z, A-Z, 0-9, '_', '-' are allowed")
                return None
            
            # Validate email format if provided
            if email and not re.match(r'^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$', email):
                self.logger.warn(f"Invalid email format: '{email}'")
                return None
            
            # Ensure user_group is not empty
            if user_group is None or len(user_group) == 0:
                user_group = ["user"]
            
            # Generate unique user_id (UUID collisions are extremely rare, but we'll still check)
            user_id = str(uuid.uuid4())
            
            user_group_json = json.dumps(user_group)
            
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    INSERT INTO users (user_id, username, name, email, hashed_password, salt, user_group, is_activated)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
                ''', (user_id, username, name, email, hashed_password, salt, user_group_json, is_activated))
                
                conn.commit()
                
                self.logger.info(f"Created new user: {username} (ID: {user_id}, activated: {is_activated})")
                
                # Return the created user data directly to avoid deadlock
                return {
                    'user_id': user_id,
                    'username': username,
                    'name': name,
                    'email': email,
                    'hashed_password': hashed_password,
                    'salt': salt,
                    'user_group': user_group,  # Already parsed list
                    'last_active': datetime.now().isoformat(),
                    'is_activated': is_activated
                }
                
        except sqlite3.IntegrityError as e:
            if 'username' in str(e):
                self.logger.warn(f"Username '{username}' already exists")
            elif 'email' in str(e):
                self.logger.warn(f"Email '{email}' already exists")
            else:
                self.logger.warn(f"User creation failed due to constraint: {e}")
            return None
        except Exception as e:
            self.logger.exception(f"Failed to create user '{username}': {e}")
            return None
    
    def update_user_last_active(self, user_id: str) -> Optional[Dict[str, Any]]:
        """Update user's last active timestamp and return updated user data"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                current_time = datetime.now()
                cursor.execute('''
                    UPDATE users 
                    SET last_active = ?
                    WHERE user_id = ?
                ''', (current_time, user_id))
                conn.commit()
                
                self.logger.debug(f"Updated last_active for user_id: {user_id}")
                # Get the updated user data in the same connection to avoid deadlock
                cursor.execute('''
                    SELECT user_id, username, name, email, hashed_password, salt, user_group, last_active, is_activated 
                    FROM users 
                    WHERE user_id = ?
                ''', (user_id,))
                
                row = cursor.fetchone()
                if row:
                    user_data = dict(row)
                    user_data['user_group'] = json.loads(user_data['user_group'])
                    return user_data
                else:
                    self.logger.warn(f"No user found with user_id: {user_id}")
                    return None
                    
        except Exception as e:
            self.logger.exception(f"Failed to update last_active for user_id {user_id}: {e}")
            return None
    
    def update_user_activation(self, user_id: str, activation: bool) -> Optional[Dict[str, Any]]:
        """Update user's activation status and cleanup sessions if deactivated, return updated user data"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    UPDATE users 
                    SET is_activated = ?
                    WHERE user_id = ?
                ''', (activation, user_id))
                conn.commit()
                
                self.logger.info(f"Updated activation status for user_id {user_id}: {activation}")
                
                # Get the updated user data in the same connection to avoid deadlock
                cursor.execute('''
                    SELECT user_id, username, name, email, hashed_password, salt, user_group, last_active, is_activated 
                    FROM users 
                    WHERE user_id = ?
                ''', (user_id,))
                
                row = cursor.fetchone()
                if row:
                    # Clean up sessions after updating activation status (outside the transaction)
                    if not activation: self.cleanup_expired_sessions()
                    
                    user_data = dict(row)
                    user_data['user_group'] = json.loads(user_data['user_group'])
                    return user_data
                else:
                    self.logger.warn(f"No user found with user_id: {user_id}")
                    return None                
                    
        except Exception as e:
            self.logger.exception(f"Failed to update activation for user_id {user_id}: {e}")
            return None
    
    def update_user_password(self, user_id: str, hashed_password: str, salt: str) -> Optional[Dict[str, Any]]:
        """Update user's password and salt, return updated user data"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    UPDATE users 
                    SET hashed_password = ?, salt = ?
                    WHERE user_id = ?
                ''', (hashed_password, salt, user_id))
                conn.commit()
                
                if cursor.rowcount == 0:
                    self.logger.warn(f"No user found with user_id: {user_id}")
                    return None
                
                self.logger.info(f"Updated password for user_id: {user_id}")
                
                # Get the updated user data in the same connection to avoid deadlock
                cursor.execute('''
                    SELECT user_id, username, name, email, hashed_password, salt, user_group, last_active, is_activated 
                    FROM users 
                    WHERE user_id = ?
                ''', (user_id,))
                
                row = cursor.fetchone()
                if row:
                    user_data = dict(row)
                    user_data['user_group'] = json.loads(user_data['user_group'])
                    return user_data
                else:
                    self.logger.warn(f"No user found with user_id: {user_id}")
                    return None                
                    
        except Exception as e:
            self.logger.exception(f"Failed to update password for user_id {user_id}: {e}")
            return None
    
    def delete_user(self, user_id: str) -> bool:
        """Delete user and all associated data from all tables"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                
                # Check if user exists first
                cursor.execute('SELECT username FROM users WHERE user_id = ?', (user_id,))
                user_row = cursor.fetchone()
                if not user_row:
                    self.logger.warn(f"No user found with user_id: {user_id}")
                    return False
                
                username = user_row[0]
                
                # Delete auth sessions (foreign key constraints will handle this automatically, but we'll be explicit)
                cursor.execute('DELETE FROM auth_session WHERE user_id = ?', (user_id,))
                auth_deleted = cursor.rowcount
                
                # Delete refresh sessions
                cursor.execute('DELETE FROM refresh_session WHERE user_id = ?', (user_id,))
                refresh_deleted = cursor.rowcount

                # Delete user
                cursor.execute('DELETE FROM users WHERE user_id = ?', (user_id,))
                user_deleted = cursor.rowcount
                
                conn.commit()
                
                if user_deleted > 0:
                    self.logger.info(f"Deleted user '{username}' (ID: {user_id}) and all associated data: {auth_deleted} auth sessions, {refresh_deleted} refresh sessions")
                    return True
                else:
                    self.logger.warn(f"Failed to delete user with user_id: {user_id}")
                    return False
                    
        except Exception as e:
            self.logger.exception(f"Failed to delete user with user_id {user_id}: {e}")
            return False
    
    # ============================================================================
    # Session management functions
    # ============================================================================

    def create_auth_session(self, user_id: str, hashed_authkey: str, salt: str, 
                           expire_time: datetime) -> Optional[Dict[str, Any]]:
        """Create an auth session and return session data"""
        try:
            # Generate unique session_id (UUID collisions are extremely rare)
            session_id = str(uuid.uuid4())
            
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    INSERT INTO auth_session (session_id, user_id, hashed_authkey, salt, expire_time)
                    VALUES (?, ?, ?, ?, ?)
                ''', (session_id, user_id, hashed_authkey, salt, expire_time))
                
                conn.commit()
                
                self.logger.debug(f"Created auth session for user_id {user_id}, session_id: {session_id}")
                
                # Return the created session data
                return {
                    'session_id': session_id,
                    'user_id': user_id,
                    'hashed_authkey': hashed_authkey,
                    'salt': salt,
                    'expire_time': expire_time.isoformat()
                }
                
        except Exception as e:
            self.logger.exception(f"Failed to create auth session for user_id {user_id}: {e}")
            return None
    
    def create_refresh_session(self, user_id: str, hashed_refreshkey: str, salt: str, 
                              expire_time: datetime) -> Optional[Dict[str, Any]]:
        """Create a refresh session and return session data"""
        try:
            # Generate unique session_id (UUID collisions are extremely rare)
            session_id = str(uuid.uuid4())
                    
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    INSERT INTO refresh_session (session_id, user_id, hashed_refreshkey, salt, expire_time)
                    VALUES (?, ?, ?, ?, ?)
                ''', (session_id, user_id, hashed_refreshkey, salt, expire_time))
                
                conn.commit()
                
                self.logger.debug(f"Created refresh session for user_id {user_id}, session_id: {session_id}")
                
                # Return the created session data
                return {
                    'session_id': session_id,
                    'user_id': user_id,
                    'hashed_refreshkey': hashed_refreshkey,
                    'salt': salt,
                    'expire_time': expire_time.isoformat()
                }
                
        except Exception as e:
            self.logger.exception(f"Failed to create refresh session for user_id {user_id}: {e}")
            return None
    
    def get_auth_session(self, session_id: str) -> Optional[Dict[str, Any]]:
        """Get valid auth session by session_id"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    SELECT session_id, user_id, hashed_authkey, salt, expire_time
                    FROM auth_session 
                    WHERE session_id = ? AND expire_time > ?
                ''', (session_id, datetime.now()))
                
                row = cursor.fetchone()
                if row:
                    return dict(row)
                return None
                
        except Exception as e:
            self.logger.exception(f"Failed to get auth session for session_id {session_id}: {e}")
            return None
    
    def get_refresh_session(self, session_id: str) -> Optional[Dict[str, Any]]:
        """Get valid refresh session by session_id"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    SELECT session_id, user_id, hashed_refreshkey, salt, expire_time
                    FROM refresh_session 
                    WHERE session_id = ? AND expire_time > ?
                ''', (session_id, datetime.now()))
                
                row = cursor.fetchone()
                if row:
                    return dict(row)
                return None
                
        except Exception as e:
            self.logger.exception(f"Failed to get refresh session for session_id {session_id}: {e}")
            return None
    
    def delete_auth_session(self, session_id: str) -> bool:
        """Delete an auth session"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('DELETE FROM auth_session WHERE session_id = ?', (session_id,))
                conn.commit()
                
                if cursor.rowcount > 0:
                    self.logger.debug(f"Deleted auth session_id: {session_id}")
                    return True
                return False
                
        except Exception as e:
            self.logger.exception(f"Failed to delete auth session {session_id}: {e}")
            return False
    
    def delete_refresh_session(self, session_id: str) -> bool:
        """Delete a refresh session"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('DELETE FROM refresh_session WHERE session_id = ?', (session_id,))
                conn.commit()
                
                if cursor.rowcount > 0:
                    self.logger.debug(f"Deleted refresh session_id: {session_id}")
                    return True
                return False
                
        except Exception as e:
            self.logger.exception(f"Failed to delete refresh session {session_id}: {e}")
            return False
    
    def delete_user_sessions(self, user_id: str) -> bool:
        """Delete all sessions for a user (logout)"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                # Delete auth sessions
                cursor.execute('DELETE FROM auth_session WHERE user_id = ?', (user_id,))
                auth_deleted = cursor.rowcount
                
                # Delete refresh sessions
                cursor.execute('DELETE FROM refresh_session WHERE user_id = ?', (user_id,))
                refresh_deleted = cursor.rowcount
                conn.commit()
                
                self.logger.info(f"Deleted all sessions for user_id {user_id}: {auth_deleted} auth, {refresh_deleted} refresh")
                return True
                
        except Exception as e:
            self.logger.exception(f"Failed to delete sessions for user_id {user_id}: {e}")
            return False

    # ============================================================================
    # System statistics functions
    # ============================================================================

    def insert_system_stats(self, 
                            srs_uptime: float, srs_cpu_percent: float, 
                            srs_memory_percent: float, srs_recv_KBps: float, 
                            srs_send_KBps: float, disk_read_KBps: float, 
                            disk_write_KBps: float, os_uptime: float, 
                            os_cpu_percent: float, os_memory_percent: float) -> bool:
        """Insert system statistics into the database"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                timestamp = datetime.now()

                cursor.execute('''
                    INSERT INTO system_stats (timestamp, 
                                              srs_uptime, srs_cpu_percent, srs_memory_percent, 
                                              srs_recv_KBps, srs_send_KBps, 
                                              disk_read_KBps, disk_write_KBps, 
                                              os_uptime, os_cpu_percent, os_memory_percent)
                    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                ''', (timestamp, 
                      srs_uptime, srs_cpu_percent, srs_memory_percent,
                      srs_recv_KBps, srs_send_KBps,
                      disk_read_KBps, disk_write_KBps,
                      os_uptime, os_cpu_percent, os_memory_percent))

                conn.commit()
                self.logger.debug(f"Inserted system stats at {timestamp.isoformat()}")
                return True
        except Exception as e:
            self.logger.exception(f"Failed to insert system stats: {e}")
            return False
    
    def get_system_stats(self, time_delta: int) -> List[Dict[str, Any]]:
        """Get system statistics from the database"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    SELECT * FROM system_stats WHERE timestamp >= ?
                ''', (datetime.now() - timedelta(seconds=time_delta),))
                rows = cursor.fetchall()
                columns = [column[0] for column in cursor.description]
                return [dict(zip(columns, row)) for row in rows]
        except Exception as e:
            self.logger.exception(f"Failed to get system stats: {e}")
            return []
    
    def delete_expired_system_stats(self) -> bool:
        """Delete expired system statistics older than 20 minutes"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    DELETE FROM system_stats WHERE timestamp < ?
                ''', (datetime.now() - timedelta(minutes=20),))
                conn.commit()
                
                if cursor.rowcount > 0:
                    self.logger.info(f"Deleted {cursor.rowcount} expired system stats")
                    return True
                else:
                    self.logger.debug("No expired system stats to delete")
                    return False
                
        except Exception as e:
            self.logger.exception(f"Failed to delete expired system stats: {e}")
            return False

    # ============================================================================
    # Streams management functions
    # ============================================================================

    def create_stream(self, streamer_id: str, stream_title: str, stream_description: Optional[str] = None, stream_tags: Optional[List[str]] = [], stream_visibility: str = 'public') -> Dict[str, Any]:
        """Create a new stream and return stream data"""
        try:
            # Validate streamer_id exists
            if not self.get_user_by_id(streamer_id):
                self.logger.warn(f"Streamer with user_id {streamer_id} does not exist")
                return None
            
            # Generate unique stream_id (UUID collisions are extremely rare)
            stream_id = str(uuid.uuid4())
            # Generate stream_code in form of xxx-xxxx (only include letters and numbers)
            def random_code():
                part1 = ''.join(random.choices(string.ascii_lowercase + string.digits, k=3))
                part2 = ''.join(random.choices(string.ascii_lowercase + string.digits, k=4))
                return f"{part1}-{part2}"
            stream_code = random_code()
            
            # Convert tags to JSON string
            stream_tags_json = json.dumps(stream_tags)
            
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    INSERT INTO streams (stream_id, stream_code, streamer_id, stream_title, 
                                         stream_description, stream_tags, 
                                         stream_visibility)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                ''', (stream_id, stream_code, streamer_id, stream_title, 
                      stream_description, stream_tags_json, 
                      stream_visibility))
                
                conn.commit()
                
                self.logger.info(f"Created new stream: {stream_title} (ID: {stream_id}, Streamer ID: {streamer_id})")
                
                return {
                    'stream_id': stream_id,
                    'stream_code': stream_code,
                    'streamer_id': streamer_id,
                    'stream_title': stream_title,
                    'stream_description': stream_description,
                    'stream_tags': stream_tags,
                    'stream_visibility': stream_visibility,
                    'stream_status': 'planned',  # Default status
                }
                
        except sqlite3.IntegrityError as e:
            self.logger.warn(f"Stream creation failed due to constraint: {e}")
            return None
        except Exception as e:
            self.logger.exception(f"Failed to create stream: {e}")
            return None
    
    def get_stream_by_vis(self, stream_visibility: str = 'public') -> Optional[List[Dict[str, Any]]]:
        """Get streams by visibility"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute('''
                    SELECT * FROM streams 
                    WHERE stream_visibility = ? AND stream_status = 'streaming'
                ''', (stream_visibility,))
                
                rows = cursor.fetchall()
                columns = [column[0] for column in cursor.description]
                return [dict(zip(columns, row)) for row in rows]
                
        except Exception as e:
            self.logger.exception(f"Failed to get streams by visibility '{stream_visibility}': {e}")
            return None
    
    # ============================================================================
    # Default database statistics functions
    # ============================================================================

    def get_database_stats(self) -> Dict[str, int]:
        """Get database statistics"""
        try:
            with self.get_connection() as conn:
                cursor = conn.cursor()
                
                stats = {}
                
                # Count users
                cursor.execute('SELECT COUNT(*) FROM users')
                stats['users'] = cursor.fetchone()[0]
                
                # Count active auth sessions
                cursor.execute('SELECT COUNT(*) FROM auth_session WHERE expire_time > ?', (datetime.now(),))
                stats['active_auth_sessions'] = cursor.fetchone()[0]
                
                # Count active refresh sessions
                cursor.execute('SELECT COUNT(*) FROM refresh_session WHERE expire_time > ?', (datetime.now(),))
                stats['active_refresh_sessions'] = cursor.fetchone()[0]
                
                # Count expired auth sessions
                cursor.execute('SELECT COUNT(*) FROM auth_session WHERE expire_time <= ?', (datetime.now(),))
                stats['expired_auth_sessions'] = cursor.fetchone()[0]
                
                # Count expired refresh sessions
                cursor.execute('SELECT COUNT(*) FROM refresh_session WHERE expire_time <= ?', (datetime.now(),))
                stats['expired_refresh_sessions'] = cursor.fetchone()[0]
                
                return stats
                
        except Exception as e:
            self.logger.exception(f"Failed to get database statistics: {e}")
            return {}

# Global database instance
_global_db: Optional[Database] = None

def get_database(db_path: str = "./objs/srs_database.db") -> Database:
    """
    Get the global database instance
    
    Args:
        db_path: Path to SQLite database file
        
    Returns:
        Database instance
    """
    global _global_db
    
    if _global_db is None:
        # run_comprehensive_tests()
        _global_db = Database(db_path)
    
    return _global_db

if __name__ == "__main__":
    # Comprehensive test suite for database functionality
    import argparse
    import uuid
    from datetime import datetime, timedelta
    from srs_logger import init_logger

    logger = init_logger()

    # Main execution
    parser = argparse.ArgumentParser(description="Database Module Test")
    parser.add_argument("--db-path", default="./objs/srs_database.db", help="Database file path")
    args = parser.parse_args()

    db = get_database(args.db_path)

    # Show database statistics
    stats = db.get_database_stats()
    logger.info(f"Database statistics: {stats}")
    
    # Clean up expired sessions
    db.cleanup_expired_sessions()