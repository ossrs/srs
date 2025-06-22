#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
FastAPI Authentication API Server
Provides secure authentication endpoints with token-based authentication
"""
from datetime import datetime
from typing import Optional, Dict, Any, List
from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, Depends, Cookie, Response, Query
from fastapi.security import HTTPBearer, HTTPAuthorizationCredentials
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, EmailStr, Field, field_validator
import uvicorn

# Import our database and logger
from database import get_database, Database
from srs_logger import get_logger

# Import Function Class
from security_module import AuthManager
from system_stats_module import SystemStatsManager
from avatar_module import AvatarGenerator

# ============================================================================
# Pydantic Models for Request/Response
# ============================================================================

class RegisterRequest(BaseModel):
    """用户注册请求模型"""
    username: str = Field(..., min_length=1, max_length=50, 
                         description="用户名，只能包含字母、数字、下划线和连字符")
    name: str = Field(..., min_length=1, max_length=100, description="用户真实姓名")
    email: Optional[EmailStr] = Field(None, description="用户邮箱（可选）")
    password: str = Field(..., min_length=6, max_length=128, description="用户密码")
    
    @field_validator('username')
    def validate_username(cls, v):
        if not v.replace('_', '').replace('-', '').isalnum():
            raise ValueError('用户名只能包含字母、数字、下划线和连字符')
        return v

class LoginRequest(BaseModel):
    """用户登录请求模型"""
    username_or_email: str = Field(..., min_length=1, description="用户名或邮箱")
    password: str = Field(..., min_length=1, description="用户密码")
    remember_me: Optional[bool] = Field(True, description="是否记住登录状态（默认为False）")

class RefreshRequest(BaseModel):
    """令牌刷新请求模型"""
    auth_key_session_id: Optional[str] = Field(None, description="认证令牌会话ID（可选，用于删除旧的认证会话）")

class UpdatePasswordRequest(BaseModel):
    """更新密码请求模型"""
    original_password: str = Field(..., min_length=1, description="原密码")
    password: str = Field(..., min_length=6, max_length=128, description="新密码")

class UserIDRequest(BaseModel):
    """用户ID请求模型"""
    user_id: str = Field(..., min_length=1, description="传入的用户ID")

class AuthResponse(BaseModel):
    """认证响应模型"""
    success: bool = Field(..., description="操作是否成功")
    message: str = Field(..., description="响应消息")
    auth_key: Optional[str] = Field(None, description="认证令牌")
    auth_key_session_id: Optional[str] = Field(None, description="认证令牌会话ID")
    refresh_key: Optional[str] = Field(None, description="刷新令牌")
    refresh_key_session_id: Optional[str] = Field(None, description="刷新令牌会话ID")

class SimpleResponse(BaseModel):
    """简单响应模型"""
    success: bool = Field(..., description="操作是否成功")
    message: str = Field(..., description="响应消息")

class SystemStatsResponse(BaseModel):
    """系统统计响应模型"""
    system_stats: List[Dict[str, Any]] = Field(..., description="系统统计信息列表，每个元素包含时间戳和统计数据")

# ============================================================================
# FastAPI Application Setup
# ============================================================================

@asynccontextmanager
async def lifespan(app: FastAPI):
    """应用程序生命周期管理"""
    logger = get_logger()
    logger.info("Starting FastAPI Authentication Server...")
    
    # 初始化数据库
    db = get_database()
    logger.info("Database initialized")
    
    # 创建认证管理器
    auth_manager = AuthManager(db)
    system_stats_manager = SystemStatsManager(db)
    avatar_generator = AvatarGenerator()
    
    app.state.auth_manager = auth_manager
    logger.info("Auth manager initialized")
    system_stats_manager.start_polling()  # 启动系统统计轮询
    app.state.system_stats_manager = system_stats_manager
    logger.info("System stats manager initialized")
    app.state.avatar_generator = avatar_generator
    logger.info("Avatar generator initialized")
    app.state.db = db
    logger.info("Database connection established")

    yield
    
    logger.info("Shutting down FastAPI Authentication Server...")

# 创建FastAPI应用
app = FastAPI(
    title="认证API服务器",
    description="基于FastAPI的安全认证系统，提供用户注册、登录、令牌刷新和登出功能",
    version="1.0.0",
    docs_url="/api/docs",
    redoc_url="/api/redoc",
    lifespan=lifespan
)

# 添加CORS中间件
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],  # 在生产环境中应该限制允许的源
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 获取认证管理器
def get_auth_manager() -> AuthManager:
    return app.state.auth_manager

def get_system_stats_manager() -> SystemStatsManager:
    return app.state.system_stats_manager

def get_avatar_generator() -> AvatarGenerator:
    return app.state.avatar_generator

def get_db() -> Database:
    return app.state.db

# HTTP Bearer认证方案
security = HTTPBearer()

# 认证依赖项
async def verify_auth_token(
    credentials: HTTPAuthorizationCredentials = Depends(security),
    auth_manager: AuthManager = Depends(get_auth_manager)
) -> Dict[str, Any]:
    """验证Bearer令牌并返回认证会话信息"""
    try:
        # Bearer token格式: "auth_key:auth_key_session_id"
        token_parts = credentials.credentials.split(':')
        if len(token_parts) != 2:
            raise HTTPException(
                status_code=401,
                detail="无效的Bearer令牌格式，应为 'auth_key:auth_key_session_id'"
            )
        
        auth_key, auth_key_session_id = token_parts
        
        # 获取认证会话
        auth_manager.db.cleanup_expired_sessions()  # 清理过期会话
        auth_session = auth_manager.db.get_auth_session(auth_key_session_id)
        if not auth_session:
            raise HTTPException(
                status_code=401,
                detail="认证会话无效或已过期"
            )
        
        # 验证认证令牌
        if not auth_manager.verify_token(auth_key, auth_session['salt'], auth_session['hashed_authkey']):
            raise HTTPException(
                status_code=401,
                detail="认证令牌无效"
            )
        
        # 获取用户信息以获取用户组
        user_data = auth_manager.db.get_user_by_id(auth_session['user_id'])
        auth_session['user_group'] = user_data.get('user_group', ['user'])

        # 更新用户最后活跃时间
        auth_manager.db.update_user_last_active(auth_session['user_id'])
               
        return auth_session
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Token verification error: {e}")
        raise HTTPException(
            status_code=401,
            detail="令牌验证失败"
        )

# ============================================================================
# Cookie Configuration
# ============================================================================

def set_auth_cookies(response: Response, tokens: Dict[str, str]) -> None:
    """设置安全的认证cookie"""
    response.set_cookie(
        key="refresh_key", 
        value=tokens['refresh_key'],
        httponly=True,
        secure=False,
        samesite="strict",
        max_age=604800  # 7天
    )

    response.set_cookie(
        key="refresh_key_session_id",
        value=tokens['refresh_key_session_id'],
        httponly=True,
        secure=False,
        samesite="strict",
        max_age=604800  # 7天
    )

def clear_auth_cookies(response: Response) -> None:
    """清除认证cookie"""
    response.delete_cookie(key="refresh_key", httponly=True, secure=True, samesite="strict")
    response.delete_cookie(key="refresh_key_session_id", httponly=True, secure=True, samesite="strict")

# ============================================================================
# Authentication API Endpoints
# ============================================================================

@app.post("/api/register", response_model=SimpleResponse, 
          tags=["Authentication API Endpoints"], summary="用户注册", description="注册新用户账户")
async def register(request: RegisterRequest, auth_manager: AuthManager = Depends(get_auth_manager)):
    """
    用户注册端点
    
    - **username**: 用户名（必需，只能包含字母、数字、下划线和连字符）
    - **name**: 用户真实姓名（必需）
    - **email**: 用户邮箱（可选）
    - **password**: 用户密码（必需，最少6个字符）
    
    返回注册是否成功的布尔值
    """
    try:
        success = auth_manager.register_user(
            username=request.username,
            name=request.name,
            email=request.email,
            password=request.password
        )
        
        if success:
            return SimpleResponse(success=True, message="用户注册成功")
        else:
            raise HTTPException(
                status_code=400, 
                detail="用户注册失败，用户名或邮箱可能已存在"
            )
            
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Register endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.post("/api/login", response_model=AuthResponse,
          tags=["Authentication API Endpoints"], summary="用户登录", description="用户登录并获取认证令牌")
async def login(request: LoginRequest, response: Response, 
                auth_manager: AuthManager = Depends(get_auth_manager)):
    """
    用户登录端点
    
    - **username_or_email**: 用户名或邮箱（必需）
    - **password**: 用户密码（必需）
    
    成功登录后返回认证令牌和刷新令牌，并设置安全cookie
    """
    try:
        # 认证用户
        user_data = auth_manager.authenticate_user(
            username_or_email=request.username_or_email,
            password=request.password
        )
        
        if not user_data:
            raise HTTPException(
                status_code=401,
                detail="用户名/邮箱或密码错误"
            )
        
        # 创建认证令牌
        tokens = auth_manager.create_auth_tokens(user_data['user_id'])
        if not tokens:
            raise HTTPException(
                status_code=500,
                detail="创建认证令牌失败"
            )
        
        # 设置安全cookie
        if request.remember_me:
            set_auth_cookies(response, tokens)
        
        return AuthResponse(
            success=True,
            message="登录成功",
            auth_key=tokens['auth_key'],
            auth_key_session_id=tokens['auth_key_session_id'],
            refresh_key=tokens['refresh_key'],
            refresh_key_session_id=tokens['refresh_key_session_id']
        )
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Login endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.post("/api/refresh", response_model=AuthResponse,
          tags=["Authentication API Endpoints"], summary="刷新令牌", description="使用刷新令牌获取新的认证令牌")
async def refresh_tokens(request: RefreshRequest, response: Response,
                        refresh_key: Optional[str] = Cookie(None),
                        refresh_key_session_id: Optional[str] = Cookie(None),
                        auth_manager: AuthManager = Depends(get_auth_manager)):
    """
    令牌刷新端点
    
    - **auth_key_session_id**: 认证令牌会话ID（可选，用于删除旧的认证会话）
    
    refresh_key和refresh_key_session_id从httponly Cookie中自动获取
    验证刷新令牌后返回新的认证令牌和刷新令牌，并更新cookie
    """
    try:
        # 检查Cookie中的刷新令牌信息
        if not refresh_key or not refresh_key_session_id:
            raise HTTPException(
                status_code=401,
                detail="未找到刷新令牌，请重新登录"
            )
        
        # 刷新令牌
        new_tokens = auth_manager.refresh_tokens(
            refresh_key_session_id=refresh_key_session_id,
            refresh_key=refresh_key,
            auth_key_session_id=request.auth_key_session_id
        )
        
        if not new_tokens:
            # 清除cookie
            clear_auth_cookies(response)
            raise HTTPException(
                status_code=401,
                detail="刷新令牌无效或已过期"
            )
        
        # 设置新的安全cookie
        set_auth_cookies(response, new_tokens)
        
        return AuthResponse(
            success=True,
            message="令牌刷新成功",
            auth_key=new_tokens['auth_key'],
            auth_key_session_id=new_tokens['auth_key_session_id'],
            refresh_key=new_tokens['refresh_key'],
            refresh_key_session_id=new_tokens['refresh_key_session_id']
        )
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Refresh endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.post("/api/logout", response_model=SimpleResponse,
          tags=["Authentication API Endpoints"], summary="登出", description="登出当前会话")
async def logout(response: Response,
                refresh_key_session_id: Optional[str] = Cookie(None),
                auth_session: Dict[str, Any] = Depends(verify_auth_token),
                auth_manager: AuthManager = Depends(get_auth_manager)):
    """
    登出端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    refresh_key_session_id从httponly Cookie中自动获取
    
    登出指定的会话并清除相关cookie
    """
    try:
        # 检查Cookie中的刷新令牌会话ID
        if not refresh_key_session_id:
            # 清除cookie（如果有的话）
            clear_auth_cookies(response)
            raise HTTPException(
                status_code=401,
                detail="未找到刷新令牌会话ID"
            )
        
        success = auth_manager.logout_session(
            refresh_key_session_id=refresh_key_session_id,
            auth_key_session_id=auth_session['session_id']
        )
        
        # 清除cookie
        clear_auth_cookies(response)
        
        if success:
            return SimpleResponse(success=True, message="登出成功")
        else:
            return SimpleResponse(success=False, message="会话不存在或已过期")
            
    except Exception as e:
        auth_manager.logger.exception(f"Logout endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.post("/api/logout-all", response_model=SimpleResponse,
          tags=["Authentication API Endpoints"], summary="登出所有会话", description="登出用户的所有会话")
async def logout_all(response: Response,
                    auth_session: Dict[str, Any] = Depends(verify_auth_token),
                    auth_manager: AuthManager = Depends(get_auth_manager)):
    """
    登出所有会话端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    
    登出用户的所有会话并清除相关cookie
    """
    try:
        success = auth_manager.logout_all_sessions(auth_session['user_id'])
        
        # 清除cookie
        clear_auth_cookies(response)
        
        if success:
            return SimpleResponse(success=True, message="所有会话已登出")
        else:
            return SimpleResponse(success=False, message="会话不存在或已过期")
            
    except Exception as e:
        auth_manager.logger.exception(f"Logout all endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.get("/api/profile", response_model=Dict[str, Any],
         tags=["Authentication API Endpoints"], summary="获取用户信息", description="获取当前认证用户的基本信息")
async def get_profile(
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    auth_manager: AuthManager = Depends(get_auth_manager)
):
    """
    获取用户信息端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    
    返回当前认证用户的基本信息
    """
    try:
        user_data = auth_manager.db.get_user_by_id(auth_session['user_id'])
        if not user_data:
            raise HTTPException(status_code=404, detail="用户未找到")
        
        return {
            "user_id": user_data['user_id'],
            "username": user_data['username'],
            "name": user_data['name'],
            "email": user_data.get('email', None),
            'user_group': user_data.get('user_group', ['user']),
            "is_activated": user_data.get('is_activated', False),
            "last_active": user_data.get('last_active', None)
        }
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Get profile endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.put("/api/update-password", response_model=SimpleResponse,
         tags=["Authentication API Endpoints"], summary="更新密码", description="更新当前用户的密码")
async def update_password(
    request: UpdatePasswordRequest,
    response: Response,
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    auth_manager: AuthManager = Depends(get_auth_manager)
):
    """
    更新密码端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    
    - **original_password**: 原密码（必需）
    - **password**: 新密码（必需，最少6个字符）
    
    验证原密码后更新为新密码，并自动登出所有会话
    """
    try:
        user_id = auth_session['user_id']
        
        # 调用AuthManager处理密码更新逻辑
        result = auth_manager.update_user_password_with_verification(
            user_id=user_id,
            original_password=request.original_password,
            new_password=request.password
        )
        
        if result["success"]:
            # 如果密码更新成功且包含logout_all标志，清除cookie
            if result.get("logout_all", False):
                clear_auth_cookies(response)
            
            return SimpleResponse(success=True, message=result["message"])
        else:
            # 根据错误类型返回相应的HTTP状态码
            if "用户未找到" in result["message"]:
                raise HTTPException(status_code=404, detail=result["message"])
            elif "原密码错误" in result["message"]:
                raise HTTPException(status_code=401, detail=result["message"])
            else:
                raise HTTPException(status_code=500, detail=result["message"])
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Update password endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")

@app.delete("/api/delete-user", response_model=SimpleResponse,
           tags=["Authentication API Endpoints"], summary="删除自己的账户", description="删除当前登录用户的账户")
async def delete_own_account(
    response: Response,
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    auth_manager: AuthManager = Depends(get_auth_manager)
):
    """
    删除自己账户端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    只能删除当前登录用户本人账户，删除成功后将清除所有认证cookie
    """
    try:      
        user_id = auth_session['user_id']
        # 调用AuthManager处理自己账户删除逻辑
        result = auth_manager.delete_own_account(user_id)
        
        if result["success"]:
            # 删除成功后清除认证cookie
            clear_auth_cookies(response)
            return SimpleResponse(success=True, message=result["message"])
        else:
            # 根据错误类型返回相应的HTTP状态码
            if "未找到" in result["message"]:
                raise HTTPException(status_code=404, detail=result["message"])
            else:
                raise HTTPException(status_code=500, detail=result["message"])
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Delete own account endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")


@app.delete("/api/admin/delete-user", response_model=SimpleResponse,
           tags=["Admin API Endpoints"], summary="管理员删除用户", description="管理员删除指定用户账户")
async def admin_delete_user(
    request: UserIDRequest,
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    auth_manager: AuthManager = Depends(get_auth_manager)
):
    """
    管理员删除用户端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    需要管理员权限（manager或admin用户组）
    - **user_id**: 要删除的用户ID（必需）
    """
    try:
        admin_user_id = auth_session['user_id']
        target_user_id = request.user_id
        
        # 调用AuthManager处理管理员删除用户逻辑
        result = auth_manager.admin_delete_user(
            admin_user_id=admin_user_id,
            admin_user_groups=auth_session.get('user_group', ["user"]),
            target_user_id=target_user_id
        )
        
        if result["success"]:
            return SimpleResponse(success=True, message=result["message"])
        else:
            # 根据错误类型返回相应的HTTP状态码
            if "未找到" in result["message"]:
                raise HTTPException(status_code=404, detail=result["message"])
            elif "权限不足" in result["message"]:
                raise HTTPException(status_code=403, detail=result["message"])
            else:
                raise HTTPException(status_code=400, detail=result["message"])
        
    except HTTPException:
        raise
    except Exception as e:
        auth_manager.logger.exception(f"Admin delete user endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")

# ============================================================================
# Avatar Generation Endpoint
# ============================================================================

@app.get("/avatar/{variant}",
          tags=["Avatar API Endpoints"], summary="生成头像", description="根据变体生成头像")
async def generate_avatar_variant_only(
    variant: str,
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    name: Optional[str] = Query(default="John Doe", description="Name to generate avatar for"),
    colors: Optional[str] = Query(default=None, description="Comma-separated hex colors"),
    size: int = Query(default=80, description="Avatar size"),
    square: bool = Query(default=False, description="Square avatar"),
    title: bool = Query(default=False, description="Include title element"),

    avatar_generator: AvatarGenerator = Depends(get_avatar_generator),
):
    """Generate avatar with variant only"""
    try:
        if variant not in avatar_generator.VALID_VARIANTS:
            raise HTTPException(status_code=400, detail=f"Invalid variant. Must be one of: {', '.join(avatar_generator.VALID_VARIANTS)}")
        
        # Validate and sanitize inputs
        if not name or not isinstance(name, str):
            name = auth_session['user_id']

        if not isinstance(size, int) or size < 10 or size > 1000:
            size = avatar_generator.DEFAULT_SIZE
        
        color_palette = avatar_generator.normalize_colors(colors)
        generator = avatar_generator.AVATAR_GENERATORS[variant]
        svg_content = generator(name, color_palette, size, square, title)
        
        return Response(
            content=svg_content,
            media_type="image/svg+xml",
            headers={
                "Cache-Control": "s-max-age=1, stale-while-revalidate",
                "Content-Type": "image/svg+xml"
            }
        )
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Avatar generation failed: {str(e)}")

@app.get("/avatar/{variant}/{size}",
          tags=["Avatar API Endpoints"], summary="生成头像", description="根据变体生成头像")
async def generate_avatar_variant_size(
    variant: str,
    size: int,
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    name: Optional[str] = Query(default="Clara Barton", description="Name to generate avatar for"),
    colors: Optional[str] = Query(default=None, description="Comma-separated hex colors"),
    square: bool = Query(default=False, description="Square avatar"),
    title: bool = Query(default=False, description="Include title element"),
    avatar_generator: AvatarGenerator = Depends(get_avatar_generator)
):
    """Generate avatar with variant and size"""
    try:
        if variant not in avatar_generator.VALID_VARIANTS:
            raise HTTPException(status_code=400, detail=f"Invalid variant. Must be one of: {', '.join(avatar_generator.VALID_VARIANTS)}")
        
        # Validate and sanitize inputs
        if not name or not isinstance(name, str):
            name = auth_session['user_id']
            
        if not isinstance(size, int) or size < 10 or size > 1000:
            size = avatar_generator.DEFAULT_SIZE
        
        color_palette = avatar_generator.normalize_colors(colors)
        generator = avatar_generator.AVATAR_GENERATORS[variant]
        svg_content = generator(name, color_palette, size, square, title)
        
        return Response(
            content=svg_content,
            media_type="image/svg+xml",
            headers={
                "Cache-Control": "s-max-age=1, stale-while-revalidate",
                "Content-Type": "image/svg+xml"
            }
        )
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Avatar generation failed: {str(e)}")

@app.get("/avatar/{variant}/{size}/{name}",
          tags=["Avatar API Endpoints"], summary="生成头像", description="根据变体生成头像")
async def generate_avatar_full_path(
    variant: str,
    size: int,
    name: str,
    colors: Optional[str] = Query(default=None, description="Comma-separated hex colors"),
    square: bool = Query(default=False, description="Square avatar"),
    title: bool = Query(default=False, description="Include title element"),
    avatar_generator: AvatarGenerator = Depends(get_avatar_generator),
    
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
):
    """Generate avatar with variant, size, and name in path"""
    try:
        if variant not in avatar_generator.VALID_VARIANTS:
            raise HTTPException(status_code=400, detail=f"Invalid variant. Must be one of: {', '.join(avatar_generator.VALID_VARIANTS)}")
        
        # Validate and sanitize inputs
        if not name or not isinstance(name, str):
            name = auth_session['user_id']
            
        if not isinstance(size, int) or size < 10 or size > 1000:
            size = avatar_generator.DEFAULT_SIZE
        
        color_palette = avatar_generator.normalize_colors(colors)
        generator = avatar_generator.AVATAR_GENERATORS[variant]
        svg_content = generator(name, color_palette, size, square, title)
        
        return Response(
            content=svg_content,
            media_type="image/svg+xml",
            headers={
                "Cache-Control": "s-max-age=1, stale-while-revalidate",
                "Content-Type": "image/svg+xml"
            }
        )
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"Avatar generation failed: {str(e)}")

# ============================================================================
# System Statistics API Endpoints
# ============================================================================
@app.get("/api/system-stats", response_model=SystemStatsResponse,
         tags=["System Statistics API Endpoints"], summary="获取系统统计信息", description="获取服务器的系统统计信息")
async def get_system_stats(
    auth_session: Dict[str, Any] = Depends(verify_auth_token),
    time_delta: Optional[int] = Query(default=5, description="时间范围（秒），默认为5秒"),
    time_interval: Optional[int] = Query(default=1, description="时间间隔（秒），默认为1秒"),
    system_stats_manager: SystemStatsManager = Depends(get_system_stats_manager)
):
    """
    获取系统统计信息端点
    
    需要在Authorization头中提供Bearer令牌，格式为: Bearer auth_key:auth_key_session_id
    - **time_delta**: 时间范围（秒），默认为5秒
    返回服务器的系统统计信息
    """
    try:
        stats = system_stats_manager.get_recent_stats(
            auth_session.get('user_group', ['user']),
            time_delta=time_delta,
            time_interval=time_interval
        )
        if stats["success"]:
            return stats
        else:
            if "未找到" in stats["message"]:
                raise HTTPException(status_code=404, detail=stats["message"])
            elif "权限不足" in stats["message"]:
                raise HTTPException(status_code=403, detail=stats["message"])
            else:
                raise HTTPException(status_code=500, detail=stats["message"])
        
    except HTTPException:
        raise
    except Exception as e:
        system_stats_manager.logger.exception(f"Get system stats endpoint error: {e}")
        raise HTTPException(status_code=500, detail="服务器内部错误")

# ============================================================================
# Additional Utility API Endpoints
# ============================================================================

@app.get("/api/health", response_model=Dict[str, Any],
         tags=["Additional Utility API Endpoints"], summary="健康检查", description="检查API服务器状态")
async def health_check(db: Database = Depends(get_db)):
    """健康检查端点"""
    try:
        stats = db.get_database_stats()
        return {
            "status": "healthy",
            "timestamp": datetime.now().isoformat(),
            "database_stats": stats
        }
    except Exception as e:
        raise HTTPException(status_code=503, detail=f"服务不可用: {str(e)}")

@app.post("/api/clean-cookies", response_model=SimpleResponse,
         tags=["Additional Utility API Endpoints"], summary="清除认证Cookie", description="清除认证相关的Cookie")
async def clean_cookies(response: Response):
    """清除认证Cookie端点"""
    try:
        clear_auth_cookies(response)
        return SimpleResponse(success=True, message="认证Cookie已清除")
    except Exception as e:
        raise HTTPException(status_code=500, detail=f"清除Cookie失败: {str(e)}")

# ============================================================================
# Static Files Configuration
# ============================================================================
# 在所有API路由定义之后挂载静态文件，确保API路由有更高优先级

# Frontend路径配置
frontend_path = r"./livestream_site"  # 前端静态文件目录
app.mount("/", StaticFiles(directory=frontend_path, html=True), name="frontend")

# ============================================================================
# Main Entry Point
# ============================================================================

if __name__ == "__main__":
    logger = get_logger()
    logger.info("Starting FastAPI Authentication Server on port 8000...")
    
    # 检查是否在PyInstaller打包环境中运行
    import sys
    if getattr(sys, 'frozen', False):
        # 在打包环境中，直接传递app对象
        uvicorn.run(
            app,
            host="127.0.0.1",
            port=8000,
            reload=False,
            log_level="info"
        )
    else:
        # 在开发环境中，使用字符串导入（支持热重载）
        uvicorn.run(
            "httpbackend_server:app",
            host="127.0.0.1",
            port=8000,
            reload=True,
            reload_delay=5.0,
            log_level="info"
        )