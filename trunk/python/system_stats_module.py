from database import Database
from srs_logger import get_logger
import requests
import time
import threading
from datetime import datetime, timedelta
from typing import Optional, Dict, Any
from datetime import datetime, timedelta

# ============================================================================
# System Statistics Management Module
# ============================================================================

class SystemStatsManager:
    """系统统计管理器"""
    
    def __init__(self, db: Database):
        self.db = db
        self.logger = get_logger()
        self.api_url = "http://python_stats:wMePq3ahpoLRzgsVg7BY9eE82uuJHT0YukD2ZE1JfMY2RjP4e6QnUaKg3V9x5s9M@localhost:1985/api/v1/summaries"
        self.previous_data: Optional[Dict[str, Any]] = None
        self.previous_timestamp: Optional[float] = None
        self.is_running = False
        self.poll_thread: Optional[threading.Thread] = None
        self.cleanup_thread: Optional[threading.Thread] = None
        
        # 启动定期清理过期数据的线程
        self._start_cleanup_thread()

    def _fetch_system_stats(self):
        """获取系统统计数据并插入数据库"""
        max_retries = 5
        retry_count = 0
        
        while retry_count < max_retries:
            try:
                # 发送HTTP请求获取SRS统计数据
                response = requests.get(self.api_url, timeout=10)
                response.raise_for_status()
                
                data = response.json()
                
                # 检查返回状态
                if data.get('code') != 0:
                    self.logger.error(f"SRS API returned error code: {data.get('code')}")
                    retry_count += 1
                    time.sleep(2 ** retry_count)  # 指数退避
                    continue
                
                # 提取有用的信息
                stats_data = self._extract_stats_data(data)
                if stats_data:
                    # 插入数据库
                    success = self.db.insert_system_stats(**stats_data)
                    if success:
                        self.logger.debug("Successfully inserted system stats")
                        return True
                    else:
                        self.logger.error("Failed to insert system stats to database")
                        retry_count += 1
                else:
                    self.logger.error("Failed to extract stats data")
                    retry_count += 1
                    
            except requests.exceptions.RequestException as e:
                self.logger.error(f"HTTP request failed: {e}")
                retry_count += 1
            except Exception as e:
                self.logger.exception(f"Unexpected error in fetch_system_stats: {e}")
                retry_count += 1
                
            if retry_count < max_retries:
                time.sleep(2 ** retry_count)  # 指数退避重试
        
        self.logger.error(f"Failed to fetch system stats after {max_retries} retries")
        return False

    def _extract_stats_data(self, api_data: Dict[str, Any]) -> Optional[Dict[str, Any]]:
        """从API返回数据中提取有用的统计信息"""
        try:
            data_section = api_data.get('data', {})
            self_section = data_section.get('self', {})
            system_section = data_section.get('system', {})
            
            current_timestamp = data_section.get('now_ms', 0) / 1000.0  # 转换为秒
            
            # SRS相关数据
            srs_uptime = self_section.get('srs_uptime', 0.0)
            srs_cpu_percent = self_section.get('cpu_percent', 0.0)
            srs_memory_percent = self_section.get('mem_percent', 0.0)
            
            # 网络流量数据（需要计算KBps）
            srs_recv_bytes = system_section.get('srs_recv_bytes', 0)
            srs_send_bytes = system_section.get('srs_send_bytes', 0)
            srs_sample_time = system_section.get('srs_sample_time', 0) / 1000.0  # 转换为秒
            
            # 计算KBps（需要与上一次的数据比较）
            srs_recv_KBps = 0.0
            srs_send_KBps = 0.0
            
            if (self.previous_data and 
                self.previous_timestamp and 
                current_timestamp > self.previous_timestamp):
                
                time_diff = current_timestamp - self.previous_timestamp
                prev_recv = self.previous_data.get('srs_recv_bytes', 0)
                prev_send = self.previous_data.get('srs_send_bytes', 0)
                
                if time_diff > 0:
                    # 计算字节差值并转换为KBps
                    srs_recv_KBps = max(0, (srs_recv_bytes - prev_recv) / time_diff / 1024)
                    srs_send_KBps = max(0, (srs_send_bytes - prev_send) / time_diff / 1024)
            
            # print(srs_recv_KBps, srs_send_KBps) # DEBUG

            # 磁盘IO数据
            disk_read_KBps = system_section.get('disk_read_KBps', 0.0)
            disk_write_KBps = system_section.get('disk_write_KBps', 0.0)
            
            # 操作系统相关数据
            os_uptime = system_section.get('uptime', 0.0)
            os_cpu_percent = system_section.get('cpu_percent', 0.0)
            os_memory_percent = system_section.get('mem_ram_percent', 0.0)
            
            # 更新上一次的数据
            self.previous_data = {
                'srs_recv_bytes': srs_recv_bytes,
                'srs_send_bytes': srs_send_bytes,
                'srs_sample_time': srs_sample_time
            }
            self.previous_timestamp = current_timestamp
            
            return {
                'srs_uptime': srs_uptime,
                'srs_cpu_percent': srs_cpu_percent,
                'srs_memory_percent': srs_memory_percent,
                'srs_recv_KBps': srs_recv_KBps,
                'srs_send_KBps': srs_send_KBps,
                'disk_read_KBps': disk_read_KBps,
                'disk_write_KBps': disk_write_KBps,
                'os_uptime': os_uptime,
                'os_cpu_percent': os_cpu_percent,
                'os_memory_percent': os_memory_percent
            }
            
        except Exception as e:
            self.logger.exception(f"Failed to extract stats data: {e}")
            return None

    def start_polling(self, interval: int = 3):
        """启动长轮询统计数据收集"""
        if self.is_running:
            self.logger.warn("Polling is already running")
            return
            
        self.is_running = True
        self.poll_thread = threading.Thread(target=self._polling_loop, args=(interval,))
        self.poll_thread.daemon = True
        self.poll_thread.start()
        self.logger.info(f"Started system stats polling with {interval}s interval")

    def stop_polling(self):
        """停止长轮询"""
        if not self.is_running:
            self.logger.warn("Polling is not running")
            return
            
        self.is_running = False
        if self.poll_thread:
            self.poll_thread.join(timeout=5)
        self.logger.info("Stopped system stats polling")

    def _polling_loop(self, interval: int):
        """轮询循环"""
        while self.is_running:
            try:
                self._fetch_system_stats()
            except Exception as e:
                self.logger.exception(f"Error in polling loop: {e}")
            
            # 等待指定间隔，但允许提前停止
            for _ in range(interval):
                if not self.is_running:
                    break
                time.sleep(1)

    def _start_cleanup_thread(self):
        """启动清理过期数据的线程"""
        self.cleanup_thread = threading.Thread(target=self._cleanup_loop)
        self.cleanup_thread.daemon = True
        self.cleanup_thread.start()
        self.logger.info("Started system stats cleanup thread (5 minute interval)")

    def _cleanup_loop(self):
        """定期清理过期数据的循环"""
        while True:
            try:
                # 执行清理
                success = self.db.delete_expired_system_stats()
                if success:
                    self.logger.debug("Successfully cleaned up expired system stats")
                else:
                    self.logger.warn("Failed to clean up expired system stats")

                time.sleep(300)
                    
            except Exception as e:
                self.logger.exception(f"Error in cleanup loop: {e}")
                # 发生错误后等待一分钟再继续
                time.sleep(60)

    def _process_stats_data(self, raw_stats: list, time_delta: int, time_interval: int) -> list:
        """
        处理原始统计数据，按指定时间间隔进行插值和过滤
        
        Args:
            raw_stats: 原始统计数据列表
            time_delta: 时间范围（秒）
            time_interval: 时间间隔（秒）
        
        Returns:
            处理后的统计数据列表
        """
        try:
            if not raw_stats:
                return []
            
            # 参数验证
            if time_interval <= 0:
                self.logger.warn("Invalid time_interval, using 1 second")
                time_interval = 1
            if time_delta <= 0:
                self.logger.warn("Invalid time_delta, using 60 seconds")
                time_delta = 60
            
            # 转换时间戳并排序
            for stat in raw_stats:
                if isinstance(stat['timestamp'], str):
                    stat['timestamp'] = datetime.fromisoformat(stat['timestamp'])
                elif not isinstance(stat['timestamp'], datetime):
                    # 如果是其他格式，尝试解析
                    stat['timestamp'] = datetime.fromisoformat(str(stat['timestamp']))
            
            # 按时间戳排序
            raw_stats.sort(key=lambda x: x['timestamp'])
            
            # 确定时间范围
            now = datetime.now().replace(microsecond=0)  # 取整到秒
            start_time = now - timedelta(seconds=time_delta)
            
            # 过滤掉超出时间范围的数据
            filtered_stats = [stat for stat in raw_stats if stat['timestamp'] >= start_time]
            
            if not filtered_stats:
                self.logger.warn("No data points in specified time range")
                return []
            
            # 生成目标时间点列表（从最近一次记录向前）
            target_times = []
            current_time = now
            while current_time >= start_time:
                target_times.append(current_time)
                current_time -= timedelta(seconds=time_interval)
            
            # 反转列表，使其按时间顺序排列
            target_times.reverse()
            
            # 对每个目标时间点进行插值
            processed_stats = []
            numeric_fields = [
                'srs_uptime', 'srs_cpu_percent', 'srs_memory_percent',
                'srs_recv_KBps', 'srs_send_KBps', 'disk_read_KBps', 
                'disk_write_KBps', 'os_uptime', 'os_cpu_percent', 'os_memory_percent'
            ]
            
            for target_time in target_times:
                interpolated_data = self._interpolate_data_point(filtered_stats, target_time, numeric_fields)
                if interpolated_data:
                    processed_stats.append(interpolated_data)
            
            self.logger.debug(f"Processed {len(raw_stats)} raw points into {len(processed_stats)} interpolated points")
            return processed_stats
            
        except Exception as e:
            self.logger.exception(f"Error processing stats data: {e}")
            return raw_stats  # 如果处理失败，返回原始数据
    
    def _interpolate_data_point(self, raw_stats: list, target_time: datetime, numeric_fields: list) -> Optional[Dict[str, Any]]:
        """
        为指定时间点插值数据
        
        Args:
            raw_stats: 原始统计数据列表（已排序）
            target_time: 目标时间点
            numeric_fields: 需要插值的数值字段列表
        
        Returns:
            插值后的数据点
        """
        try:
            # 查找最接近的前后两个数据点
            before_point = None
            after_point = None
            
            for i, stat in enumerate(raw_stats):
                stat_time = stat['timestamp']
                
                if stat_time <= target_time:
                    before_point = stat
                elif stat_time > target_time:
                    after_point = stat
                    break
            
            # 如果没有找到合适的数据点，使用最近的点
            if before_point is None and after_point is None:
                return None
            elif before_point is None:
                # 只有后面的点，直接使用
                result = after_point.copy()
                result['timestamp'] = target_time.replace(microsecond=0)
                return result
            elif after_point is None:
                # 只有前面的点，直接使用
                result = before_point.copy()
                result['timestamp'] = target_time.replace(microsecond=0)
                return result
            
            # 计算时间差和权重
            before_time = before_point['timestamp']
            after_time = after_point['timestamp']
            
            # 如果时间点完全匹配，直接返回
            if before_time == target_time:
                return before_point.copy()
            if after_time == target_time:
                return after_point.copy()
            
            # 计算插值权重
            total_diff = (after_time - before_time).total_seconds()
            if total_diff == 0:
                # 两个点时间相同，使用前一个点
                result = before_point.copy()
                result['timestamp'] = target_time.replace(microsecond=0)
                return result
            
            target_diff = (target_time - before_time).total_seconds()
            weight = target_diff / total_diff
            
            # 执行线性插值
            result = {'timestamp': target_time.replace(microsecond=0)}  # 确保时间戳取整到秒
            
            for field in numeric_fields:
                if field in before_point and field in after_point:
                    before_val = float(before_point[field]) if before_point[field] is not None else 0.0
                    after_val = float(after_point[field]) if after_point[field] is not None else 0.0
                    
                    # 线性插值
                    interpolated_val = before_val + (after_val - before_val) * weight
                    result[field] = round(interpolated_val, 4)  # 保留4位小数
                else:
                    # 如果字段不存在，使用前一个点的值
                    result[field] = before_point.get(field, 0.0)
            
            return result
            
        except Exception as e:
            self.logger.exception(f"Error interpolating data point: {e}")
            return None

    def get_recent_stats(self, user_group: list, time_delta: int = 5, time_interval: int = 1):
        """获取最近的统计数据"""
        try:
            if not any(group in user_group for group in ['streamer', 'manager', 'admin']):
                return {"success": False, "message": "权限不足，无法获取统计数据"}
            
            # 获取原始数据，多获取一些以便插值
            raw_stats = self.db.get_system_stats(time_delta + 5)  # 多获取60秒数据用于插值
            if not raw_stats:
                return {"success": False, "message": "没有找到相关的统计数据"}
            
            # 处理数据，按指定间隔进行插值和过滤
            processed_stats = self._process_stats_data(raw_stats, time_delta, time_interval)
            
            # 转换时间戳为字符串格式，便于JSON序列化
            for stat in processed_stats:
                if isinstance(stat['timestamp'], datetime):
                    stat['timestamp'] = stat['timestamp'].isoformat()
            
            return {
                "success": True, 
                "system_stats": processed_stats,
                "metadata": {
                    "time_delta": time_delta,
                    "time_interval": time_interval,
                    "data_points": len(processed_stats),
                    "original_points": len(raw_stats)
                }
            }
        
        except Exception as e:
            self.logger.exception(f"Error in get_recent_stats: {e}")
            return {"success": False, "message": "服务器内部错误"}