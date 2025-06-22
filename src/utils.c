/**
 * @file utils.c
 * @brief 유틸리티 모듈 - 성능 측정, 로깅, 해시 테이블 기능
 * @details 시스템 전반에서 사용되는 공통 기능들을 제공합니다.
 */

#include <inttypes.h> // PRIu64 사용을 위해 추가
#include "../include/utils.h"  // 유틸리티 관련 헤더 파일 포함
#include "../include/ui.h"    // ui_manager_t, show_error_message 사용을 위해 추가

// [추가] 비동기 로깅 시스템을 위한 자료구조
typedef struct {
    char message[1024];  // 로그 메시지 (고정 크기로 메모리 관리 단순화)
} log_entry_t;

typedef struct {
    log_entry_t* entries;        // 원형 버퍼
    int capacity;                // 버퍼 크기
    int head;                    // 읽기 위치
    int tail;                    // 쓰기 위치
    int size;                    // 현재 큐 크기
    pthread_mutex_t mutex;       // 뮤텍스
    pthread_cond_t not_empty;    // 큐가 비어있지 않을 때 신호
    pthread_cond_t not_full;     // 큐가 가득 차지 않았을 때 신호
} log_queue_t;

// [추가] 비동기 로깅 시스템 전역 변수
static log_queue_t g_log_queue;
static pthread_t g_logger_thread;
static bool g_logger_running = false;
static FILE* g_log_file = NULL;
static log_level_t current_log_level = LOG_INFO; // 기본 로그 레벨은 INFO

// [추가] 비동기 로깅 시스템 함수 프로토타입
static void* logger_thread_function(void* arg);
static int log_queue_init(int capacity);
static void log_queue_cleanup(void);
static bool log_queue_push(const char* message);
static bool log_queue_pop(char* message);
static void log_queue_signal_shutdown(void);

/*
error handling
*/

/**
 * @brief 에러 코드에 해당하는 메시지를 반환합니다.
 * @param error_code 에러 코드
 * @return 에러 메시지 문자열
 */
static const char* utils_get_error_message(error_code_t error_code) {
    switch (error_code) {
        // 시스템 공통 에러
        case ERROR_NONE: return "성공";
        case ERROR_INVALID_PARAMETER: return "잘못된 매개변수";
        case ERROR_MEMORY_ALLOCATION_FAILED: return "메모리 할당 실패";
        case ERROR_FILE_OPERATION_FAILED: return "파일 작업 실패";
        case ERROR_PERMISSION_DENIED: return "권한 거부";
        case ERROR_TIMEOUT: return "시간 초과";
        case ERROR_NOT_FOUND: return "찾을 수 없음";
        case ERROR_ALREADY_EXISTS: return "이미 존재함";
        case ERROR_INVALID_STATE: return "잘못된 상태";
        case ERROR_UNKNOWN: return "알 수 없는 오류";

        // 네트워크 관련 에러
        case ERROR_NETWORK_SOCKET_CREATION_FAILED: return "소켓 생성 실패";
        case ERROR_NETWORK_BIND_FAILED: return "소켓 바인딩 실패";
        case ERROR_NETWORK_LISTEN_FAILED: return "소켓 리스닝 실패";
        case ERROR_NETWORK_CONNECT_FAILED: return "서버 연결 실패";
        case ERROR_NETWORK_ACCEPT_FAILED: return "클라이언트 연결 수락 실패";
        case ERROR_NETWORK_SEND_FAILED: return "데이터 전송 실패";
        case ERROR_NETWORK_RECEIVE_FAILED: return "데이터 수신 실패";
        case ERROR_NETWORK_SSL_INIT_FAILED: return "SSL 초기화 실패";
        case ERROR_NETWORK_SSL_HANDSHAKE_FAILED: return "SSL 핸드셰이크 실패";
        case ERROR_NETWORK_SSL_CERTIFICATE_FAILED: return "SSL 인증서 로드/검증 실패";
        case ERROR_NETWORK_SSL_CONTEXT_FAILED: return "SSL 컨텍스트 생성 실패";
        case ERROR_NETWORK_IP_CONVERSION_FAILED: return "IP 주소 변환 실패";
        case ERROR_NETWORK_SOCKET_OPTION_FAILED: return "소켓 옵션 설정 실패";

        // 메시지 관련 에러
        case ERROR_MESSAGE_CREATION_FAILED: return "메시지 생성 실패";
        case ERROR_MESSAGE_SERIALIZATION_FAILED: return "메시지 직렬화 실패";
        case ERROR_MESSAGE_DESERIALIZATION_FAILED: return "메시지 역직렬화 실패";
        case ERROR_MESSAGE_INVALID_TYPE: return "잘못된 메시지 타입";
        case ERROR_MESSAGE_INVALID_FORMAT: return "잘못된 메시지 형식";

        // 세션 관련 에러
        case ERROR_SESSION_CREATION_FAILED: return "세션 생성 실패";
        case ERROR_SESSION_NOT_FOUND: return "세션을 찾을 수 없음";
        case ERROR_SESSION_ALREADY_EXISTS: return "세션이 이미 존재함";
        case ERROR_SESSION_INVALID_STATE: return "잘못된 세션 상태";
        case ERROR_SESSION_AUTHENTICATION_FAILED: return "인증 실패";
        case ERROR_SESSION_AUTHORIZATION_FAILED: return "권한 부족";

        // 리소스 관련 에러
        case ERROR_RESOURCE_INIT_FAILED: return "리소스 초기화 실패";
        case ERROR_RESOURCE_NOT_FOUND: return "리소스를 찾을 수 없음";
        case ERROR_RESOURCE_ALREADY_EXISTS: return "리소스가 이미 존재함";
        case ERROR_RESOURCE_IN_USE: return "리소스가 사용 중임";
        case ERROR_RESOURCE_INVALID_STATUS: return "잘못된 리소스 상태";
        case ERROR_RESOURCE_MAINTENANCE_MODE: return "리소스가 점검 모드임";

        // 예약 관련 에러
        case ERROR_RESERVATION_CREATION_FAILED: return "예약 생성 실패";
        case ERROR_RESERVATION_NOT_FOUND: return "예약을 찾을 수 없음";
        case ERROR_RESERVATION_ALREADY_EXISTS: return "예약이 이미 존재함";
        case ERROR_RESERVATION_INVALID_TIME: return "잘못된 예약 시간";
        case ERROR_RESERVATION_CONFLICT: return "예약 시간 충돌";
        case ERROR_RESERVATION_CANCELLATION_FAILED: return "예약 취소 실패";
        case ERROR_RESERVATION_PERMISSION_DENIED: return "예약 권한 없음";
        case ERROR_RESERVATION_MAX_LIMIT_REACHED: return "최대 예약 개수 초과";

        // UI 관련 에러
        case ERROR_UI_INIT_FAILED: return "UI 초기화 실패";
        case ERROR_UI_TERMINAL_TOO_SMALL: return "터미널 크기가 너무 작음";
        case ERROR_UI_DRAW_FAILED: return "UI 그리기 실패";
        case ERROR_UI_INPUT_FAILED: return "UI 입력 실패";

        // 로깅 관련 에러
        case ERROR_LOGGER_INIT_FAILED: return "로거 초기화 실패";
        case ERROR_LOGGER_WRITE_FAILED: return "로그 쓰기 실패";
        case ERROR_LOGGER_FILE_NOT_OPEN: return "로그 파일이 열려있지 않음";

        // 해시 테이블 관련 에러
        case ERROR_HASHTABLE_CREATION_FAILED: return "해시 테이블 생성 실패";
        case ERROR_HASHTABLE_INSERT_FAILED: return "해시 테이블 삽입 실패";
        case ERROR_HASHTABLE_DELETE_FAILED: return "해시 테이블 삭제 실패";
        case ERROR_HASHTABLE_KEY_NOT_FOUND: return "해시 테이블 키를 찾을 수 없음";

        // 성능 측정 관련 에러
        case ERROR_PERFORMANCE_TIME_FAILED: return "성능 시간 측정 실패";
        case ERROR_PERFORMANCE_STATS_INVALID: return "잘못된 성능 통계";

        default: return "알 수 없는 에러 코드";
    }
}

/**
 * @brief 에러를 보고하고 로깅합니다.
 * @param error_code 에러 코드
 * @param module 에러가 발생한 모듈명
 * @param additional_info 추가 정보 (가변 인자)
 * @param ... 가변 인자
 */
void utils_report_error(error_code_t error_code, const char* module, const char* additional_info, ...) {
    if (error_code == ERROR_NONE) return; // 성공인 경우 아무것도 하지 않음

    const char* error_msg = utils_get_error_message(error_code);
    char formatted_message[512];
    
    if (additional_info && strlen(additional_info) > 0) {
        va_list args;
        va_start(args, additional_info);
        vsnprintf(formatted_message, sizeof(formatted_message), additional_info, args);
        va_end(args);
        
        extern ui_manager_t* g_ui_manager;
        if (g_ui_manager) {
            char ui_msg[600];
            snprintf(ui_msg, sizeof(ui_msg), "[%s] %s: %s", module, error_msg, formatted_message);
            ui_show_error_message(ui_msg);
        } else {
            fprintf(stderr, "[%s] %s: %s\n", module, error_msg, formatted_message);
        }
    } else {
        extern ui_manager_t* g_ui_manager;
        if (g_ui_manager) {
            char ui_msg[600];
            snprintf(ui_msg, sizeof(ui_msg), "[%s] %s", module, error_msg);
            ui_show_error_message(ui_msg);
        } else {
            fprintf(stderr, "[%s] %s\n", module, error_msg);
        }
    }
}

/*
performance
 */

/**
 * @brief 현재 시간을 마이크로초 단위로 반환합니다.
 * @return 마이크로초 단위의 현재 시간
 */
uint64_t utils_get_current_time(void) {
    struct timeval tv;  // 시간 구조체 선언
    if (gettimeofday(&tv, NULL) != 0) {  // 현재 시간 가져오기 실패 시
        utils_report_error(ERROR_PERFORMANCE_TIME_FAILED, "Performance", "gettimeofday 실패");  // 에러 로그 출력
        return 0;  // 0 반환
    }
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;  // 마이크로초 단위로 변환하여 반환
}

/**
 * @brief 성능 통계를 안전하게 복사합니다.
 * @param stats 원본 성능 통계 포인터
 * @param output 복사할 대상 포인터
 */
void utils_get_performance_stats(performance_stats_t* stats, performance_stats_t* output) {
    if (!stats || !output) return;  // 유효성 검사

    pthread_mutex_lock(&stats->mutex);  // 뮤텍스 잠금
    memcpy(output, stats, sizeof(performance_stats_t));  // 성능 통계 복사
    pthread_mutex_unlock(&stats->mutex);  // 뮤텍스 해제
}

/**
 * @brief 성능 통계를 콘솔에 출력합니다.
 * @param stats 출력할 성능 통계 포인터
 */
void utils_print_performance_stats(const performance_stats_t* stats) {
    if (!stats) return;
    
    pthread_mutex_lock((pthread_mutex_t*)&stats->mutex);
    
    printf("\n=== 성능 통계 ===\n");
    printf("총 요청 수: %" PRIu64 "\n", stats->total_requests);
    printf("성공 요청 수: %" PRIu64 "\n", stats->successful_requests);
    printf("실패 요청 수: %" PRIu64 "\n", stats->failed_requests);
    printf("성공률: %.2f%%\n", 
           stats->total_requests > 0 ? 
           (double)stats->successful_requests / stats->total_requests * 100.0 : 0.0);
    
    if (stats->total_requests > 0) {
        printf("평균 응답 시간: %" PRIu64 " μs\n", stats->total_response_time / stats->total_requests);
        printf("최대 응답 시간: %" PRIu64 " μs\n", stats->max_response_time);
        printf("최소 응답 시간: %" PRIu64 " μs\n", stats->min_response_time);
    }
    
    printf("최대 동시 요청 수: %" PRIu64 "\n", stats->max_concurrent_requests);
    printf("총 전송 데이터: %" PRIu64 " bytes\n", stats->total_data_sent);
    printf("총 수신 데이터: %" PRIu64 " bytes\n", stats->total_data_received);
    printf("총 오류 수: %" PRIu64 "\n", stats->total_errors);
    printf("================\n\n");
    
    pthread_mutex_unlock((pthread_mutex_t*)&stats->mutex);
}

/*
logger
*/

/* 정적 함수 선언 */
static const char* utils_get_log_level_string(log_level_t level);  // 로그 레벨 문자열 변환 함수

/**
 * @brief 로거를 초기화합니다.
 * @param filename 로그 파일 경로
 * @return 성공 시 0, 실패 시 -1
 */
int utils_init_logger(const char* filename) {
    if (!filename) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Logger", "로그 파일명이 NULL입니다");
        return -1;
    }

    // 비동기 로거 초기화
    if (log_queue_init(1000) != 0) {
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Logger", "비동기 로거 초기화 실패: %s", filename);
        return -1;
    }

    g_log_file = fopen(filename, "a");
    if (!g_log_file) {
        log_queue_cleanup();
        utils_report_error(ERROR_FILE_OPERATION_FAILED, "Logger", "로그 파일 열기 실패: %s", filename);
        return -1;
    }

    g_logger_running = true;
    
    // 로거 스레드 생성
    if (pthread_create(&g_logger_thread, NULL, logger_thread_function, NULL) != 0) {
        log_queue_cleanup();
        fclose(g_log_file);
        g_log_file = NULL;
        return -1;
    }

    return 0;
}

void utils_cleanup_logger(void) {
    if (g_logger_running) {
        // 로거 스레드 종료 신호
        log_queue_signal_shutdown();
        
        // 로거 스레드가 완전히 종료될 때까지 대기
        pthread_join(g_logger_thread, NULL);
        
        // 큐 정리
        log_queue_cleanup();
        
        // 파일 닫기
        if (g_log_file != NULL) {
            fclose(g_log_file);
            g_log_file = NULL;
        }
    }
}

/**
 * @brief 로그 메시지를 작성합니다.
 * @param level 로그 레벨
 * @param category 로그 카테고리
 * @param format 포맷 문자열
 * @param ... 가변 인자
 */
void utils_log_message(log_level_t level, const char* category, const char* format, ...) {
    if (level < current_log_level) {
        return; // 현재 설정된 로그 레벨보다 낮은 경우 무시
    }

    char message_buffer[MAX_LOG_MSG];
    va_list args;
    va_start(args, format);
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);
    va_end(args);

    message_buffer[sizeof(message_buffer) - 1] = '\0';

    char timestamp_str[64];
    time_t now = time(NULL);
    struct tm *timeinfo;
    timeinfo = localtime(&now);
    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    const char* level_str = utils_get_log_level_string(level);

    char final_message[MAX_LOG_MSG];

    // 버퍼 오버플로우를 방지하기 위해 남은 공간 계산
    int prefix_len = snprintf(NULL, 0, "[%s] [%s] ", level_str, category);
    int suffix_len = snprintf(NULL, 0, " (%s)\n", timestamp_str);
    int max_msg_len = sizeof(final_message) - prefix_len - suffix_len - 1;
    if (max_msg_len < 0) {
        max_msg_len = 0;
    }

    snprintf(final_message, sizeof(final_message), "[%s] [%s] %.*s (%s)\n",
             level_str, category, max_msg_len, message_buffer, timestamp_str);

    if (g_logger_running) {
        log_queue_push(final_message);
    } else {
        // 비동기 로거가 실행 중이 아닐 때, 표준 에러로 직접 출력
        fprintf(stderr, "%s", final_message);
    }
}

/**
 * @brief 로그 레벨을 문자열로 변환하는 내부 함수
 * @param level 로그 레벨
 * @return 로그 레벨에 해당하는 문자열
 */
static const char* utils_get_log_level_string(log_level_t level) {
    switch (level) {  // 로그 레벨에 따른 분기
        case LOG_ERROR: return "ERROR"; // common.h에 정의된 LogLevel 사용
        case LOG_WARNING: return "WARNING";  // 경고 레벨
        case LOG_INFO: return "INFO";  // 정보 레벨
        case LOG_DEBUG: return "DEBUG";  // 디버그 레벨
        default: return "UNKNOWN";  // 알 수 없는 레벨
    }
}

/**
 * @brief 타임스탬프를 문자열로 변환하는 함수
 * @param timestamp 변환할 타임스탬프
 * @return 포맷된 시간 문자열
 */
const char* utils_get_timestamp_string(time_t timestamp) {
    static char buffer[64];  // 정적 버퍼
    struct tm *timeinfo = localtime(&timestamp);  // 로컬 시간으로 변환
    if (timeinfo == NULL) {  // 시간 변환 실패 시
        return "Invalid Time";  // 유효하지 않은 시간 메시지 반환
    }
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);  // 시간 포맷
    return buffer;  // 포맷된 시간 문자열 반환
}

/*
hash_table
*/

/**
 * @brief djb2 해시 함수 - 문자열을 해시 값으로 변환
 * @param key 해시할 키 문자열
 * @param size 해시 테이블 크기
 * @return 해시 값
 */
static uint32_t utils_hash_function(const char* key, uint32_t size) {
    unsigned long hash = 5381;  // 초기 해시 값
    int c;  // 문자 변수
    while ((c = *key++)) {  // 문자열의 끝까지 반복
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % size;  // 해시 테이블 크기로 모듈로 연산
}

/**
 * @brief 해시 테이블을 생성합니다.
 * @param size 해시 테이블 크기
 * @param free_func 값 해제 함수 포인터
 * @return 생성된 hash_table_t 포인터, 실패 시 NULL
 */
hash_table_t* utils_hashtable_create(uint32_t size, void (*free_func)(void*)) {
    if (size == 0) return NULL;
    
    hash_table_t* table = malloc(sizeof(hash_table_t));
    if (!table) return NULL;
    
    table->buckets = calloc(size, sizeof(hash_node_t*));
    if (!table->buckets) {
        free(table);
        return NULL;
    }
    
    table->size = size;
    table->count = 0;
    table->free_value = free_func;
    
    // 뮤텍스 초기화
    if (pthread_mutex_init(&table->mutex, NULL) != 0) {
        free(table->buckets);
        free(table);
        return NULL;
    }
    
    return table;
}

/**
 * @brief 해시 테이블을 정리합니다.
 * @param table 정리할 hash_table_t 포인터
 */
void utils_hashtable_destroy(hash_table_t* table) {
    if (!table) return;
    
    pthread_mutex_lock(&table->mutex);
    for (uint32_t i = 0; i < table->size; i++) {
        hash_node_t* node = table->buckets[i];  // 현재 버킷의 첫 번째 노드
        while (node) {
            hash_node_t* temp = node;  // 임시 변수에 현재 노드 저장
            node = node->next;
            if (table->free_value) table->free_value(temp->value);
            free(temp->key);
            free(temp);
        }
    }
    pthread_mutex_unlock(&table->mutex);
    
    pthread_mutex_destroy(&table->mutex);
    free(table->buckets);
    free(table);
}

/**
 * @brief 해시 테이블에 키-값 쌍을 삽입합니다.
 * @param table 해시 테이블 포인터
 * @param key 삽입할 키
 * @param value 삽입할 값
 * @return 성공 시 true, 실패 시 false
 */
bool utils_hashtable_insert(hash_table_t* table, const char* key, void* value) {
    if (!table || !key) return false;
    
    pthread_mutex_lock(&table->mutex);
    
    uint32_t index = utils_hash_function(key, table->size);
    
    // 기존 키가 있는지 확인
    hash_node_t* current = table->buckets[index];
    while (current) {
        if (strcmp(current->key, key) == 0) {
            // 기존 값 해제
            if (table->free_value && current->value) {
                table->free_value(current->value);
            }
            current->value = value;
            pthread_mutex_unlock(&table->mutex);
            return true;
        }
        current = current->next;
    }
    
    // 새 노드 생성
    hash_node_t* new_node = malloc(sizeof(hash_node_t));
    if (!new_node) {
        pthread_mutex_unlock(&table->mutex);
        return false;
    }
    
    new_node->key = strdup(key);
    if (!new_node->key) {
        free(new_node);
        pthread_mutex_unlock(&table->mutex);
        return false;
    }
    
    new_node->value = value;
    new_node->next = table->buckets[index];
    table->buckets[index] = new_node;
    table->count++;
    
    pthread_mutex_unlock(&table->mutex);
    return true;
}

/**
 * @brief 해시 테이블에서 키에 해당하는 값을 조회합니다.
 * @param table 해시 테이블 포인터
 * @param key 조회할 키
 * @return 키에 해당하는 값, 없으면 NULL
 */
void* utils_hashtable_get(hash_table_t* table, const char* key) {
    if (!table || !key) return NULL;  // 유효성 검사
    
    pthread_mutex_lock(&table->mutex);
    uint32_t index = utils_hash_function(key, table->size);  // 해시 인덱스 계산
    hash_node_t* node = table->buckets[index];  // 해당 버킷의 첫 번째 노드
    while (node) {  // 노드가 존재하는 동안 반복
        if (strcmp(node->key, key) == 0) {  // 키가 일치하는 경우
            pthread_mutex_unlock(&table->mutex);
            return node->value;  // 값 반환
        }
        node = node->next;  // 다음 노드로 이동
    }
    pthread_mutex_unlock(&table->mutex);
    return NULL;  // 키를 찾지 못한 경우 NULL 반환
}

/**
 * @brief 해시 테이블에서 키-값 쌍을 삭제합니다.
 * @param table 해시 테이블 포인터
 * @param key 삭제할 키
 * @return 성공 시 true, 실패 시 false
 */
bool utils_hashtable_delete(hash_table_t* table, const char* key) {
    if (!table || !key) {
        utils_report_error(ERROR_INVALID_PARAMETER, "HashTable", "utils_hashtable_delete: 잘못된 파라미터");
        return false;  // 유효성 검사
    }

    pthread_mutex_lock(&table->mutex);
    uint32_t index = utils_hash_function(key, table->size);  // 해시 인덱스 계산
    
    hash_node_t* node = table->buckets[index];  // 해당 버킷의 첫 번째 노드
    hash_node_t* prev = NULL;  // 이전 노드 포인터

    while (node) {  // 노드가 존재하는 동안 반복
        if (strcmp(node->key, key) == 0) {  // 키가 일치하는 경우
            if (prev) {  // 이전 노드가 존재하는 경우
                prev->next = node->next;  // 이전 노드의 다음을 현재 노드의 다음으로 설정
            } else {  // 첫 번째 노드인 경우
                table->buckets[index] = node->next;  // 버킷의 첫 번째를 다음 노드로 설정
            }

            if (table->free_value) {  // 값 해제 함수가 존재하는 경우
                table->free_value(node->value);  // 값 메모리 해제
            }
            free(node->key);  // 키 메모리 해제
            free(node);  // 노드 메모리 해제
            table->count--;
            pthread_mutex_unlock(&table->mutex);
            return true;  // true 반환
        }
        prev = node;  // 이전 노드를 현재 노드로 설정
        node = node->next;  // 다음 노드로 이동
    }

    pthread_mutex_unlock(&table->mutex);
    return false;  // false 반환
}

/**
 * @brief 해시 테이블의 모든 요소에 대해 콜백 함수를 실행합니다.
 * @param table 해시 테이블 포인터
 * @param callback 실행할 콜백 함수
 * @param user_data 콜백 함수에 전달할 사용자 데이터
 */
void utils_hashtable_traverse(hash_table_t* table, void (*callback)(const char* key, void* value, void* user_data), void* user_data) {
    if (!table || !callback) {
        utils_report_error(ERROR_INVALID_PARAMETER, "HashTable", "utils_hashtable_traverse: 잘못된 파라미터");
        return;  // 유효성 검사
    }

    pthread_mutex_lock(&table->mutex);
    for (uint32_t i = 0; i < table->size; i++) {  // 모든 버킷에 대해 반복
        hash_node_t* node = table->buckets[i];  // 현재 버킷의 첫 번째 노드
        int bucket_count = 0;  // 버킷 내 노드 개수
        
        while (node) {  // 노드가 존재하는 동안 반복
            callback(node->key, node->value, user_data);  // 콜백 함수 호출
            node = node->next;  // 다음 노드로 이동
            bucket_count++;  // 노드 개수 증가
        }
        
        if (bucket_count > 0) {
            // LOG_INFO("HashTable", "버킷 %u 처리 완료: %d개 노드", i, bucket_count);
        }
    }
    pthread_mutex_unlock(&table->mutex);
}

bool utils_init_manager_base(void* manager, size_t manager_size, hash_table_t** table, uint32_t table_size, void (*free_func)(void*), pthread_mutex_t* mutex_ptr) {
    if (!manager || !table || !free_func || !mutex_ptr) {
        return false;
    }
    
    // 매니저 구조체를 0으로 초기화
    memset(manager, 0, manager_size);
    
    // 해시 테이블 생성
    *table = utils_hashtable_create(table_size, free_func);
    if (!*table) {
        return false;
    }
    
    // 뮤텍스 초기화
    if (pthread_mutex_init(mutex_ptr, NULL) != 0) {
        utils_hashtable_destroy(*table);
        *table = NULL;
        return false;
    }
    
    return true;
}

/**
 * @brief 기본 시그널 핸들러 - self-pipe를 통한 안전한 종료
 * @param signum 시그널 번호
 * @param pipe_fd self-pipe의 쓰기 파일 디스크립터
 */
void utils_default_signal_handler(int signum, int pipe_fd) {
    (void)signum;
    (void)write(pipe_fd, "s", 1);
}

void utils_cleanup_manager_base(void* manager, hash_table_t* table, pthread_mutex_t* mutex) {
    if (!manager) return;
    if (table) {
        utils_hashtable_destroy(table);
    }
    if (mutex) {
        pthread_mutex_destroy(mutex);
    }
    free(manager);
}

// [추가] 비동기 로깅 시스템 구현

/**
 * @brief 로그 큐를 초기화합니다.
 * @param capacity 큐의 최대 용량
 * @return 성공 시 0, 실패 시 -1
 */
static int log_queue_init(int capacity) {
    if (capacity <= 0) return -1;
    
    g_log_queue.entries = (log_entry_t*)malloc(capacity * sizeof(log_entry_t));
    if (!g_log_queue.entries) return -1;
    
    g_log_queue.capacity = capacity;
    g_log_queue.head = 0;
    g_log_queue.tail = 0;
    g_log_queue.size = 0;
    
    if (pthread_mutex_init(&g_log_queue.mutex, NULL) != 0) {
        free(g_log_queue.entries);
        return -1;
    }
    
    if (pthread_cond_init(&g_log_queue.not_empty, NULL) != 0) {
        pthread_mutex_destroy(&g_log_queue.mutex);
        free(g_log_queue.entries);
        return -1;
    }
    
    if (pthread_cond_init(&g_log_queue.not_full, NULL) != 0) {
        pthread_cond_destroy(&g_log_queue.not_empty);
        pthread_mutex_destroy(&g_log_queue.mutex);
        free(g_log_queue.entries);
        return -1;
    }
    
    return 0;
}

/**
 * @brief 로그 큐를 정리합니다.
 */
static void log_queue_cleanup(void) {
    pthread_mutex_lock(&g_log_queue.mutex);
    if (g_log_queue.entries) {
        free(g_log_queue.entries);
        g_log_queue.entries = NULL;
    }
    pthread_mutex_unlock(&g_log_queue.mutex);
    
    pthread_cond_destroy(&g_log_queue.not_full);
    pthread_cond_destroy(&g_log_queue.not_empty);
    pthread_mutex_destroy(&g_log_queue.mutex);
}

/**
 * @brief 로그 메시지를 큐에 추가합니다.
 * @param message 추가할 로그 메시지
 * @return 성공 시 true, 실패 시 false
 */
static bool log_queue_push(const char* message) {
    if (!message) return false;
    
    pthread_mutex_lock(&g_log_queue.mutex);
    
    // 큐가 가득 찬 경우 대기 (Back-pressure)
    while (g_log_queue.size >= g_log_queue.capacity && g_logger_running) {
        pthread_cond_wait(&g_log_queue.not_full, &g_log_queue.mutex);
    }
    
    // 로거가 종료 중이면 추가하지 않음
    if (!g_logger_running) {
        pthread_mutex_unlock(&g_log_queue.mutex);
        return false;
    }
    
    // 메시지 복사 (최대 1023자 + null 종료)
    strncpy(g_log_queue.entries[g_log_queue.tail].message, message, 1023);
    g_log_queue.entries[g_log_queue.tail].message[1023] = '\0';
    
    g_log_queue.tail = (g_log_queue.tail + 1) % g_log_queue.capacity;
    g_log_queue.size++;
    
    pthread_cond_signal(&g_log_queue.not_empty);
    pthread_mutex_unlock(&g_log_queue.mutex);
    
    return true;
}

/**
 * @brief 로그 큐에서 메시지를 꺼냅니다.
 * @param message 메시지를 저장할 버퍼
 * @return 성공 시 true, 실패 시 false
 */
static bool log_queue_pop(char* message) {
    if (!message) return false;
    
    pthread_mutex_lock(&g_log_queue.mutex);
    
    // 큐가 비어있고 로거가 실행 중이면 대기
    while (g_log_queue.size == 0 && g_logger_running) {
        pthread_cond_wait(&g_log_queue.not_empty, &g_log_queue.mutex);
    }
    
    // 큐가 비어있고 로거가 종료 중이면 false 반환
    if (g_log_queue.size == 0) {
        pthread_mutex_unlock(&g_log_queue.mutex);
        return false;
    }
    
    // 메시지 복사
    strcpy(message, g_log_queue.entries[g_log_queue.head].message);
    
    g_log_queue.head = (g_log_queue.head + 1) % g_log_queue.capacity;
    g_log_queue.size--;
    
    pthread_cond_signal(&g_log_queue.not_full);
    pthread_mutex_unlock(&g_log_queue.mutex);
    
    return true;
}

/**
 * @brief 로거 스레드 종료를 위한 신호를 보냅니다.
 */
static void log_queue_signal_shutdown(void) {
    pthread_mutex_lock(&g_log_queue.mutex);
    g_logger_running = false;
    pthread_cond_signal(&g_log_queue.not_empty);
    pthread_mutex_unlock(&g_log_queue.mutex);
}

/**
 * @brief 로거 스레드 함수 - 큐에서 메시지를 꺼내 파일에 쓰는 역할
 * @param arg 스레드 인자 (사용하지 않음)
 * @return NULL
 */
static void* logger_thread_function(void* arg) {
    (void)arg;
    char message[1024];
    
    while (g_logger_running || g_log_queue.size > 0) {
        if (log_queue_pop(message)) {
            // 파일에 직접 쓰기
            if (g_log_file) {
                fputs(message, g_log_file);
                fflush(g_log_file);  // 즉시 디스크에 쓰기
            }
        }
    }
    
    return NULL;
}