#include "../include/logger.h"


/* 전역 변수 */
static FILE* log_file = NULL;
static LogLevel current_log_level = LOG_INFO; // 기본 로그 레벨은 INFO
static pthread_mutex_t log_mutex;

/* 정적 함수 선언 */
static void write_to_log_file(LogLevel level, const char* category, const char* message);
static const char* get_log_level_string_internal(LogLevel level);

/* 로거 초기화 함수 */
int init_logger(const char* filename) {
    if (log_file != NULL) {
        return -1;
    }

    log_file = fopen(filename, "a");
    if (log_file == NULL) {
        return -1;
    }

    pthread_mutex_init(&log_mutex, NULL);
    log_message(LOG_INFO, "System", "로거 초기화 완료. 로그 파일: %s", filename);
    return 0;
}

/* 로거 정리 함수 */
void cleanup_logger(void) {
    if (log_file != NULL) {
        log_message(LOG_INFO, "System", "로거 정리 중...");
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_destroy(&log_mutex);
}

/* 로그 레벨 설정 함수 */
void set_log_level(LogLevel level) {
    current_log_level = level;
    log_message(LOG_INFO, "System", "로그 레벨이 %s(으)로 설정되었습니다.", get_log_level_string_internal(level));
}

/* 일반 로그 작성 함수 */
void log_message(LogLevel level, const char* category, const char* format, ...) {
    if (level > current_log_level) {
        return;
    }

    char message_buffer[MAX_LOG_MSG];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);

    write_to_log_file(level, category, message_buffer);
}

/* 카테고리별 로그 작성 함수 */
void log_system(const char* format, ...) {
    char message_buffer[MAX_LOG_MSG];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    write_to_log_file(LOG_INFO, "System", message_buffer);
}

void log_network(const char* format, ...) {
    char message_buffer[MAX_LOG_MSG];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    write_to_log_file(LOG_INFO, "Network", message_buffer);
}

void log_security(const char* format, ...) {
    char message_buffer[MAX_LOG_MSG];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    write_to_log_file(LOG_WARNING, "Security", message_buffer); // 보안 관련은 최소 WARNING
}

void log_performance(const char* format, ...) {
    char message_buffer[MAX_LOG_MSG];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);
    write_to_log_file(LOG_DEBUG, "Performance", message_buffer); // 성능 관련은 DEBUG
}

/* 실제 로그 파일에 쓰는 함수 */
static void write_to_log_file(LogLevel level, const char* category, const char* message) {
    pthread_mutex_lock(&log_mutex);

    if (log_file != NULL) {
        time_t rawtime;
        struct tm *timeinfo;
        char timestamp_str[64];

        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S %z", timeinfo);

        fprintf(log_file, "[%s] [%s] [%s] %s\n",
                timestamp_str,
                get_log_level_string_internal(level),
                category,
                message);
        fflush(log_file);
    } else {
        fprintf(stderr, "경고: 로그 파일이 열려 있지 않습니다. 메시지: [%s] [%s] %s\n",
                get_log_level_string_internal(level), category, message);
    }

    pthread_mutex_unlock(&log_mutex);
}

/* 로그 레벨을 문자열로 변환하는 내부 함수 */
static const char* get_log_level_string_internal(LogLevel level) {
    switch (level) {
        case LOG_ERROR: return "ERROR"; // common.h에 정의된 LogLevel 사용
        case LOG_WARNING: return "WARNING";
        case LOG_INFO: return "INFO";
        case LOG_DEBUG: return "DEBUG";
        default: return "UNKNOWN";
    }
}

/* 로그 레벨을 문자열로 변환하는 외부 함수 */
const char* get_log_level_string(LogLevel level) {
    return get_log_level_string_internal(level);
}

/* 로그 타임스탬프를 문자열로 변환하는 함수 */
const char* get_timestamp_string(time_t timestamp) {
    static char buffer[64];
    struct tm *timeinfo = localtime(&timestamp);
    if (timeinfo == NULL) {
        return "Invalid Time";
    }
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);
    return buffer;
}

/* 로그 메시지를 포맷팅하는 함수 */
void format_log_message(char* buffer, size_t size, const LogMessage* message) {
    if (!buffer || !message) return;
    snprintf(buffer, size, "[%s] [%s] [%s] %s",
             get_timestamp_string(message->timestamp),
             get_log_level_string(message->level),
             message->category,
             message->message);
}

/* 로그 버퍼 관리 함수 구현 */
int init_log_buffer(LogBuffer* buffer, int max_messages) {
    if (!buffer || max_messages <= 0) return -1;
    buffer->messages = (LogMessage*)calloc(max_messages, sizeof(LogMessage));
    if (!buffer->messages) return -1;
    buffer->max_messages = max_messages;
    buffer->current_count = 0;
    pthread_mutex_init(&buffer->mutex, NULL);
    return 0;
}

void cleanup_log_buffer(LogBuffer* buffer) {
    if (!buffer) return;
    pthread_mutex_destroy(&buffer->mutex);
    if (buffer->messages) free(buffer->messages);
    buffer->messages = NULL;
    buffer->max_messages = 0;
    buffer->current_count = 0;
}

int add_log_message(LogBuffer* buffer, LogLevel level, const char* category, const char* format, ...) {
    if (!buffer || !category || !format) return -1;
    pthread_mutex_lock(&buffer->mutex);
    if (buffer->current_count >= buffer->max_messages) {
        // 가장 오래된 로그를 덮어씀 (순환 버퍼)
        memmove(buffer->messages, buffer->messages + 1, (buffer->max_messages - 1) * sizeof(LogMessage));
        buffer->current_count = buffer->max_messages - 1;
    }
    LogMessage* msg = &buffer->messages[buffer->current_count];
    msg->timestamp = time(NULL);
    msg->level = level;
    strncpy(msg->category, category, sizeof(msg->category) - 1);
    msg->category[sizeof(msg->category) - 1] = '\0';
    va_list args;
    va_start(args, format);
    vsnprintf(msg->message, sizeof(msg->message), format, args);
    va_end(args);
    buffer->current_count++;
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

LogMessage* get_log_messages(LogBuffer* buffer, int* count) {
    if (!buffer || !count) return NULL;
    pthread_mutex_lock(&buffer->mutex);
    *count = buffer->current_count;
    LogMessage* result = buffer->messages;
    pthread_mutex_unlock(&buffer->mutex);
    return result;
}

/* 명령 로그 관리 함수 구현 */
int init_command_buffer(CommandBuffer* buffer, int max_logs) {
    if (!buffer || max_logs <= 0) return -1;
    buffer->logs = (CommandLog*)calloc(max_logs, sizeof(CommandLog));
    if (!buffer->logs) return -1;
    buffer->max_logs = max_logs;
    buffer->current_count = 0;
    pthread_mutex_init(&buffer->mutex, NULL);
    return 0;
}

void cleanup_command_buffer(CommandBuffer* buffer) {
    if (!buffer) return;
    pthread_mutex_destroy(&buffer->mutex);
    if (buffer->logs) free(buffer->logs);
    buffer->logs = NULL;
    buffer->max_logs = 0;
    buffer->current_count = 0;
}

int add_command_log(CommandBuffer* buffer, const char* username, const char* command, const char* args[], int arg_count) {
    if (!buffer || !username || !command) return -1;
    pthread_mutex_lock(&buffer->mutex);
    if (buffer->current_count >= buffer->max_logs) {
        // 가장 오래된 로그를 덮어씀 (순환 버퍼)
        memmove(buffer->logs, buffer->logs + 1, (buffer->max_logs - 1) * sizeof(CommandLog));
        buffer->current_count = buffer->max_logs - 1;
    }
    CommandLog* log = &buffer->logs[buffer->current_count];
    log->timestamp = time(NULL);
    strncpy(log->username, username, sizeof(log->username) - 1);
    log->username[sizeof(log->username) - 1] = '\0';
    strncpy(log->command, command, sizeof(log->command) - 1);
    log->command[sizeof(log->command) - 1] = '\0';
    log->arg_count = (arg_count > 5) ? 5 : arg_count;
    for (int i = 0; i < log->arg_count; i++) {
        strncpy(log->args[i], args[i], sizeof(log->args[i]) - 1);
        log->args[i][sizeof(log->args[i]) - 1] = '\0';
    }
    buffer->current_count++;
    pthread_mutex_unlock(&buffer->mutex);
    return 0;
}

CommandLog* get_command_logs(CommandBuffer* buffer, int* count) {
    if (!buffer || !count) return NULL;
    pthread_mutex_lock(&buffer->mutex);
    *count = buffer->current_count;
    CommandLog* result = buffer->logs;
    pthread_mutex_unlock(&buffer->mutex);
    return result;
}

/* 로그 파일 관리 함수 구현 (간단 버전) */
int rotate_log_file(void) {
    // 실제 구현에서는 파일 이름 변경 및 새 파일 생성 필요
    // 여기서는 성공만 반환
    return 0;
}

int compress_log_file(const char* filename) {
    // 실제 구현에서는 압축 라이브러리 사용 필요
    // 여기서는 성공만 반환
    (void)filename;
    return 0;
}

int cleanup_old_logs(int max_age_days) {
    // 실제 구현에서는 파일 시스템에서 오래된 로그 삭제 필요
    // 여기서는 성공만 반환
    (void)max_age_days;
    return 0;
}