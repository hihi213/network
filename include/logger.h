#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

/* 로그 레벨 열거형 */
typedef enum {
    LOG_ERROR = 0,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

/* 로그 매크로 */
#define LOG_ERROR(category, format, ...) log_message(LOG_ERROR, category, format, ##__VA_ARGS__)
#define LOG_WARNING(category, format, ...) log_message(LOG_WARNING, category, format, ##__VA_ARGS__)
#define LOG_INFO(category, format, ...) log_message(LOG_INFO, category, format, ##__VA_ARGS__)
#define LOG_DEBUG(category, format, ...) log_message(LOG_DEBUG, category, format, ##__VA_ARGS__)

/* 로그 메시지 구조체 */
typedef struct {
    time_t timestamp;
    LogLevel level;
    char category[32];
    char message[MAX_LOG_MSG];
} LogMessage;

/* 로그/명령 버퍼 구조체 */
typedef struct {
    LogMessage* messages;
    int max_messages;
    int current_count;
    pthread_mutex_t mutex;
} LogBuffer;

typedef struct {
    time_t timestamp;
    char username[MAX_USERNAME_LENGTH];
    char command[64];
    char args[5][MAX_LOG_MSG];
    int arg_count;
} CommandLog;

typedef struct {
    CommandLog* logs;
    int max_logs;
    int current_count;
    pthread_mutex_t mutex;
} CommandBuffer;

/* 로그 함수 선언 */
int init_logger(const char* filename);
void cleanup_logger(void);
void set_log_level(LogLevel level);
void log_message(LogLevel level, const char* category, const char* format, ...);

/* 로그 버퍼 관리 함수 */
int init_log_buffer(LogBuffer* buffer, int max_messages);
void cleanup_log_buffer(LogBuffer* buffer);
int add_log_message(LogBuffer* buffer, LogLevel level, const char* category, const char* format, ...);

/* 명령 로그 관리 함수 */
int init_command_buffer(CommandBuffer* buffer, int max_logs);
void cleanup_command_buffer(CommandBuffer* buffer);
int add_command_log(CommandBuffer* buffer, const char* username, const char* command, const char* args[], int arg_count);

/* 유틸리티 함수 */
const char* get_log_level_string(LogLevel level);

#endif /* LOGGER_H */