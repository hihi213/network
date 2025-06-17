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




/* 실제 로그 파일에 쓰는 함수 */
static void write_to_log_file(LogLevel level, const char* category, const char* message) {
    pthread_mutex_lock(&log_mutex);

    if (log_file != NULL) {
        time_t rawtime;
        struct tm *timeinfo;
        char timestamp_str[64];

        time(&rawtime);
        timeinfo = localtime(&rawtime);
        strftime(timestamp_str, sizeof(timestamp_str), "%m월 %d일 %H:%M", timeinfo);

        fprintf(log_file, "[%s] [%s] %s [%s]\n",
                get_log_level_string_internal(level),
                category,
                message,
                timestamp_str);
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
