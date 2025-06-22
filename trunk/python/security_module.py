import hashlib
import secrets
import hmac
from datetime import datetime, timedelta
from typing import Optional, Dict, Any

from database import Database
from srs_logger import get_logger

# ============================================================================
# Security and Utility Functions
# ============================================================================

class AuthManager:
    """认证管理器"""
    
    def __init__(self, db: Database):
        self.db = db
        self.logger = get_logger()
        
        # Token 配置
        self.AUTH_TOKEN_EXPIRE_HOURS = 1    # 认证令牌1小时过期
        self.REFRESH_TOKEN_EXPIRE_DAYS = 7  # 刷新令牌7天过期
        self.TOKEN_LENGTH = 128              # 令牌长度
        
    def generate_secure_token(self) -> str:
        """生成安全的随机令牌"""
        return secrets.token_urlsafe(self.TOKEN_LENGTH)
    
    def generate_salt(self) -> str:
        """生成盐值"""
        return secrets.token_hex(16)
    
    def hash_password(self, password: str, salt: str) -> str:
        """对密码进行加盐哈希"""
        return hashlib.pbkdf2_hmac('sha256', password.encode(), salt.encode(), 100000).hex()
    
    def verify_password(self, password: str, salt: str, hashed_password: str) -> bool:
        """验证密码"""
        return hmac.compare_digest(
            self.hash_password(password, salt),
            hashed_password
        )
    
    def hash_token(self, token: str, salt: str) -> str:
        """对令牌进行加盐哈希"""
        return hashlib.pbkdf2_hmac('sha256', token.encode(), salt.encode(), 100000).hex()
    
    def verify_token(self, token: str, salt: str, hashed_token: str) -> bool:
        """验证令牌"""
        return hmac.compare_digest(
            self.hash_token(token, salt),
            hashed_token
        )
    
    def register_user(self, username: str, name: str, email: Optional[str], password: str) -> bool:
        """注册新用户"""
        try:
            # 生成密码盐值和哈希
            salt = self.generate_salt()
            hashed_password = self.hash_password(password, salt)
            
            # 创建用户
            user_data = self.db.create_user(
                username=username,
                name=name,
                email=email,
                hashed_password=hashed_password,
                salt=salt,
                user_group=["user"],
                is_activated=True
            )
            
            if user_data:
                self.logger.info(f"User registered successfully: {username}")
                return True
            else:
                self.logger.warn(f"Failed to register user: {username}")
                return False
                
        except Exception as e:
            self.logger.exception(f"Error during user registration: {e}")
            return False
    
    def authenticate_user(self, username_or_email: str, password: str) -> Optional[Dict[str, Any]]:
        """认证用户并返回用户数据"""
        try:
            # 获取用户信息
            user_data = self.db.get_user(username_or_email)
            if not user_data:
                self.logger.warn(f"User not found: {username_or_email}")
                return None
            
            # 检查用户是否激活
            if not user_data.get('is_activated', False):
                self.logger.warn(f"User account deactivated: {username_or_email}")
                return None
            
            # 验证密码
            if self.verify_password(password, user_data['salt'], user_data['hashed_password']):
                self.logger.info(f"User authenticated successfully: {username_or_email}")
                return user_data
            else:
                self.logger.warn(f"Invalid password for user: {username_or_email}")
                return None
                
        except Exception as e:
            self.logger.exception(f"Error during user authentication: {e}")
            return None
    
    def create_auth_tokens(self, user_id: str) -> Optional[Dict[str, Any]]:
        """为用户创建认证和刷新令牌"""
        try:
            # 清理过期会话
            self.db.cleanup_expired_sessions()
            
            # 生成认证令牌
            auth_key = self.generate_secure_token()
            auth_salt = self.generate_salt()
            hashed_auth_key = self.hash_token(auth_key, auth_salt)
            auth_expire_time = datetime.now() + timedelta(hours=self.AUTH_TOKEN_EXPIRE_HOURS)
            
            # 生成刷新令牌
            refresh_key = self.generate_secure_token()
            refresh_salt = self.generate_salt()
            hashed_refresh_key = self.hash_token(refresh_key, refresh_salt)
            refresh_expire_time = datetime.now() + timedelta(days=self.REFRESH_TOKEN_EXPIRE_DAYS)
            
            # 创建认证会话
            auth_session = self.db.create_auth_session(
                user_id=user_id,
                hashed_authkey=hashed_auth_key,
                salt=auth_salt,
                expire_time=auth_expire_time
            )
            
            if not auth_session:
                self.logger.error(f"Failed to create auth session for user: {user_id}")
                return None
            
            # 创建刷新会话
            refresh_session = self.db.create_refresh_session(
                user_id=user_id,
                hashed_refreshkey=hashed_refresh_key,
                salt=refresh_salt,
                expire_time=refresh_expire_time
            )
            
            if not refresh_session:
                self.logger.error(f"Failed to create refresh session for user: {user_id}")
                # 清理已创建的认证会话
                self.db.delete_auth_session(auth_session['session_id'])
                return None
            
            # 更新用户最后活跃时间
            self.db.update_user_last_active(user_id)
            
            return {
                'auth_key': auth_key,
                'auth_key_session_id': auth_session['session_id'],
                'refresh_key': refresh_key,
                'refresh_key_session_id': refresh_session['session_id']
            }
            
        except Exception as e:
            self.logger.exception(f"Error creating auth tokens for user: {user_id}")
            return None
    
    def refresh_tokens(self, refresh_key_session_id: str, refresh_key: str, 
                      auth_key_session_id: Optional[str] = None) -> Optional[Dict[str, Any]]:
        """刷新认证令牌"""
        try:
            # 清理过期会话
            self.db.cleanup_expired_sessions()
            
            # 验证刷新会话
            refresh_session = self.db.get_refresh_session(refresh_key_session_id)
            if not refresh_session:
                self.logger.warn(f"Invalid or expired refresh session: {refresh_key_session_id}")
                return None
            
            # 验证刷新令牌
            if not self.verify_token(refresh_key, refresh_session['salt'], refresh_session['hashed_refreshkey']):
                self.logger.warn(f"Invalid refresh token for session: {refresh_key_session_id}")
                return None
            
            user_id = refresh_session['user_id']
            
            # 删除旧的认证会话（如果提供了session_id）
            if auth_key_session_id:
                self.db.delete_auth_session(auth_key_session_id)
            
            # 删除旧的刷新会话
            self.db.delete_refresh_session(refresh_key_session_id)
            
            # 创建新的令牌
            new_tokens = self.create_auth_tokens(user_id)
            if new_tokens:
                self.logger.info(f"Tokens refreshed successfully for user: {user_id}")
                return new_tokens
            else:
                self.logger.error(f"Failed to create new tokens during refresh for user: {user_id}")
                return None
                
        except Exception as e:
            self.logger.exception(f"Error during token refresh: {e}")
            return None
    
    def logout_session(self, refresh_key_session_id: str, auth_key_session_id: str = None) -> bool:
        """登出单个会话，通过删除指定的refresh session和对应的auth session"""
        try:
            # 删除指定的刷新会话
            refresh_success = self.db.delete_refresh_session(refresh_key_session_id)
            if refresh_success:
                self.logger.info(f"Refresh session logged out successfully: {refresh_key_session_id}")
            
            auth_success = self.db.delete_auth_session(auth_key_session_id)
            if auth_success:
                self.logger.info(f"Auth session logged out successfully: {auth_key_session_id}")
            else:
                self.logger.warn(f"Failed to delete auth session: {auth_key_session_id}")
            
            return refresh_success and auth_success
            
        except Exception as e:
            self.logger.exception(f"Error during session logout: {e}")
            return False
    
    def logout_all_sessions(self, user_id: str) -> bool:
        """登出用户的所有会话，通过refresh session确定用户"""
        try:
            # 删除用户的所有会话
            success = self.db.delete_user_sessions(user_id)
            if success:
                self.logger.info(f"All sessions logged out successfully for user: {user_id}")
            
            return success
            
        except Exception as e:
            self.logger.exception(f"Error during logout all sessions: {e}")
            return False
    
    def update_user_password_with_verification(self, user_id: str, original_password: str, new_password: str) -> Dict[str, Any]:
        """验证原密码后更新用户密码，并登出所有会话"""
        try:
            # 获取用户数据
            user_data = self.db.get_user_by_id(user_id)
            if not user_data:
                return {"success": False, "message": "用户未找到"}
            
            # 验证原密码
            if not self.verify_password(original_password, user_data['salt'], user_data['hashed_password']):
                return {"success": False, "message": "原密码错误"}
            
            # 生成新的盐值和密码哈希
            new_salt = self.generate_salt()
            new_hashed_password = self.hash_password(new_password, new_salt)
            
            # 更新密码
            updated_user = self.db.update_user_password(user_id, new_hashed_password, new_salt)
            if not updated_user:
                return {"success": False, "message": "密码更新失败"}
            
            # 密码更新成功后，登出用户的所有会话（强制重新登录）
            logout_success = self.db.delete_user_sessions(user_id)
            if logout_success:
                self.logger.info(f"All sessions logged out after password update for user: {user_data['username']}")
            else:
                self.logger.warn(f"Failed to logout all sessions after password update for user: {user_data['username']}")
            
            self.logger.info(f"Password updated successfully for user: {user_data['username']}")
            return {"success": True, "message": "密码更新成功，请重新登录", "logout_all": True}
            
        except Exception as e:
            self.logger.exception(f"Error updating user password: {e}")
            return {"success": False, "message": "服务器内部错误"}
    
    def delete_own_account(self, user_id: str) -> Dict[str, Any]:
        """删除用户自己的账户"""
        try:
            success = self.db.delete_user(user_id)
            if not success:
                return {"success": False, "message": "账户删除失败"}
            
            self.logger.info(f"User-ID: '{user_id}' deleted his/her own account")
            return {"success": True, "message": "您的账户已成功删除"}
            
        except Exception as e:
            self.logger.exception(f"Error deleting own account: {e}")
            return {"success": False, "message": "服务器内部错误"}
    
    def admin_delete_user(self, admin_user_id: str, admin_user_groups: list, target_user_id: str, ) -> Dict[str, Any]:
        """管理员删除指定用户账户"""
        try:
            # 检查管理员权限
            if not any(group in admin_user_groups for group in ['manager', 'admin']):
                return {
                    "success": False, 
                    "message": "权限不足，只有管理员可以删除其他用户账户"
                }
            
            # 获取目标用户数据
            target_user = self.db.get_user_by_id(target_user_id)
            if not target_user:
                return {"success": False, "message": "目标删除的用户未找到"}
            
            # 执行删除操作
            success = self.db.delete_user(target_user_id)
            if not success:
                return {"success": False, "message": "用户删除失败"}

            self.logger.info(f"User-ID '{target_user_id}' deleted by admin-ID: '{admin_user_id}'")
            return {"success": True, "message": f"用户 '{target_user_id}' 已成功删除"}
            
        except Exception as e:
            self.logger.exception(f"Error in admin delete user: {e}")
            return {"success": False, "message": "服务器内部错误"}