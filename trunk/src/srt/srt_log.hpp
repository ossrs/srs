#ifndef SRT_LOG_HPP
#define SRT_LOG_HPP
#include <stdint.h>
#include <stddef.h>

#define LOGGER_BUFFER_SIZE (10*1024)

typedef enum {
    SRT_LOGGER_INFO_LEVEL,
    SRT_LOGGER_TRACE_LEVEL,
    SRT_LOGGER_WARN_LEVEL,
    SRT_LOGGER_ERROR_LEVEL
} LOGGER_LEVEL;

void set_srt_log_level(LOGGER_LEVEL level);
LOGGER_LEVEL get_srt_log_level();
char* get_srt_log_buffer();
void srt_log_output(LOGGER_LEVEL level, const char* buffer);
void snprintbuffer(char* buffer, size_t size, const char* fmt, ...);

#define srt_log_error(...)                                      \
    if (get_srt_log_level() <= SRT_LOGGER_ERROR_LEVEL)            \
    {                                                           \
        char* buffer = get_srt_log_buffer();                      \
        snprintbuffer(buffer, LOGGER_BUFFER_SIZE, __VA_ARGS__); \
        srt_log_output(SRT_LOGGER_ERROR_LEVEL, buffer);         \
    }

#define srt_log_warn(...)                                       \
    if (get_srt_log_level() <= SRT_LOGGER_WARN_LEVEL)             \
    {                                                           \
        char* buffer = get_srt_log_buffer();                      \
        snprintbuffer(buffer, LOGGER_BUFFER_SIZE, __VA_ARGS__); \
        srt_log_output(SRT_LOGGER_WARN_LEVEL, buffer);         \
    }

#define srt_log_trace(...)                                      \
    if (get_srt_log_level() <= SRT_LOGGER_TRACE_LEVEL)            \
    {                                                           \
        char* buffer = get_srt_log_buffer();                      \
        snprintbuffer(buffer, LOGGER_BUFFER_SIZE, __VA_ARGS__); \
        srt_log_output(SRT_LOGGER_TRACE_LEVEL, buffer);         \
    }

#define srt_log_info(...)                                       \
    if (get_srt_log_level() <= SRT_LOGGER_INFO_LEVEL)             \
    {                                                           \
        char* buffer = get_srt_log_buffer();                      \
        snprintbuffer(buffer, LOGGER_BUFFER_SIZE, __VA_ARGS__); \
        srt_log_output(SRT_LOGGER_INFO_LEVEL, buffer);         \
    }
#endif //SRT_LOG_HPP