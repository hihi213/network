#ifndef UTILS_H
#define UTILS_H

#include "common.h"

/*
 * =====================================================================================
 * Error Handling
 * =====================================================================================
 */

// 에러 코드 열거형 - 모듈별/상황별 에러 코드 정의
typedef enum {
    // 시스템 공통 에러 (0-99)
    ERROR_NONE = 0,
    ERROR_INVALID_PARAMETER,
    ERROR_MEMORY_ALLOCATION_FAILED,
    ERROR_FILE_OPERATION_FAILED,
    ERROR_PERMISSION_DENIED,
    ERROR_TIMEOUT,
    ERROR_NOT_FOUND,
    ERROR_ALREADY_EXISTS,
    ERROR_INVALID_STATE,
    ERROR_UNKNOWN,

    // 네트워크 관련 에러 (100-199)
    ERROR_NETWORK_SOCKET_CREATION_FAILED = 100,
    ERROR_NETWORK_BIND_FAILED,
    ERROR_NETWORK_LISTEN_FAILED,
    ERROR_NETWORK_CONNECT_FAILED,
    ERROR_NETWORK_ACCEPT_FAILED,
    ERROR_NETWORK_SEND_FAILED,
    ERROR_NETWORK_RECEIVE_FAILED,
    ERROR_NETWORK_SSL_INIT_FAILED,
    ERROR_NETWORK_SSL_HANDSHAKE_FAILED,
    ERROR_NETWORK_SSL_CERTIFICATE_FAILED,
    ERROR_NETWORK_SSL_CONTEXT_FAILED,
    ERROR_NETWORK_IP_CONVERSION_FAILED,
    ERROR_NETWORK_SOCKET_OPTION_FAILED,

    // 메시지 관련 에러 (200-299)
    ERROR_MESSAGE_CREATION_FAILED = 200,
    ERROR_MESSAGE_SERIALIZATION_FAILED,
    ERROR_MESSAGE_DESERIALIZATION_FAILED,
    ERROR_MESSAGE_INVALID_TYPE,
    ERROR_MESSAGE_INVALID_FORMAT,

    // 세션 관련 에러 (300-399)
    ERROR_SESSION_CREATION_FAILED = 300,
    ERROR_SESSION_NOT_FOUND,
    ERROR_SESSION_ALREADY_EXISTS,
    ERROR_SESSION_INVALID_STATE,
    ERROR_SESSION_AUTHENTICATION_FAILED,
    ERROR_SESSION_AUTHORIZATION_FAILED,

    // 리소스 관련 에러 (400-499)
    ERROR_RESOURCE_INIT_FAILED = 400,
    ERROR_RESOURCE_NOT_FOUND,
    ERROR_RESOURCE_ALREADY_EXISTS,
    ERROR_RESOURCE_IN_USE,
    ERROR_RESOURCE_INVALID_STATUS,
    ERROR_RESOURCE_MAINTENANCE_MODE,

    // 예약 관련 에러 (500-599)
    ERROR_RESERVATION_CREATION_FAILED = 500,
    ERROR_RESERVATION_NOT_FOUND,
    ERROR_RESERVATION_ALREADY_EXISTS,
    ERROR_RESERVATION_INVALID_TIME,
    ERROR_RESERVATION_CONFLICT,
    ERROR_RESERVATION_CANCELLATION_FAILED,
    ERROR_RESERVATION_PERMISSION_DENIED,
    ERROR_RESERVATION_MAX_LIMIT_REACHED,

    // UI 관련 에러 (600-699)
    ERROR_UI_INIT_FAILED = 600,
    ERROR_UI_TERMINAL_TOO_SMALL,
    ERROR_UI_DRAW_FAILED,
    ERROR_UI_INPUT_FAILED,

    // 로깅 관련 에러 (700-799)
    ERROR_LOGGER_INIT_FAILED = 700,
    ERROR_LOGGER_WRITE_FAILED,
    ERROR_LOGGER_FILE_NOT_OPEN,

    // 해시 테이블 관련 에러 (800-899)
    ERROR_HASHTABLE_CREATION_FAILED = 800,
    ERROR_HASHTABLE_INSERT_FAILED,
    ERROR_HASHTABLE_DELETE_FAILED,
    ERROR_HASHTABLE_KEY_NOT_FOUND,

    // 성능 측정 관련 에러 (900-999)
    ERROR_PERFORMANCE_TIME_FAILED = 900,
    ERROR_PERFORMANCE_STATS_INVALID
} ErrorCode;

// 에러 보고 함수 프로토타입
void error_report(ErrorCode error_code, const char* module, const char* additional_info, ...);

/*
 * =====================================================================================
 * Logger
 * =====================================================================================
 */

// 로그 레벨 정의
typedef enum {
    LOG_ERROR,
    LOG_WARNING,
    LOG_INFO,
    LOG_DEBUG
} LogLevel;

// Logger 함수 프로토타입
int init_logger(const char* filename);
void cleanup_logger(void);
void log_message(LogLevel level, const char* category, const char* format, ...);
const char* get_timestamp_string(time_t timestamp);

// 로그 매크로
#define LOG_ERROR(category, format, ...) log_message(LOG_ERROR, category, format, ##__VA_ARGS__)
#define LOG_WARNING(category, format, ...) log_message(LOG_WARNING, category, format, ##__VA_ARGS__)
#define LOG_INFO(category, format, ...) log_message(LOG_INFO, category, format, ##__VA_ARGS__)
#define LOG_DEBUG(category, format, ...) log_message(LOG_DEBUG, category, format, ##__VA_ARGS__)

/*
 * =====================================================================================
 * Performance
 * =====================================================================================
 */

// 성능 통계 구조체
typedef struct {
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    uint64_t max_concurrent_requests;
    uint64_t total_response_time; // ms
    uint64_t min_response_time;   // ms
    uint64_t max_response_time;   // ms
    uint64_t total_data_sent;
    uint64_t total_data_received;
    uint64_t total_errors;
    pthread_mutex_t mutex;
} PerformanceStats;

// Performance 함수 프로토타입
uint64_t get_current_time(void);
void get_performance_stats(PerformanceStats* stats, PerformanceStats* output);
void print_performance_stats(PerformanceStats* stats);


/*
 * =====================================================================================
 * Hash Table
 * =====================================================================================
 */

// 해시 테이블의 각 항목(노드)
typedef struct HashNode {
    char* key;
    void* value;
    struct HashNode* next; // 충돌 발생 시 체이닝을 위한 포인터
} HashNode;

// 해시 테이블 구조체
typedef struct HashTable {
    uint32_t size;
    uint32_t count;
    HashNode** buckets;
    void (*free_value)(void*); // 값을 해제하는 함수 포인터
} HashTable;

/**
 * @brief 새로운 해시 테이블을 생성합니다.
 * @param size 해시 테이블의 버킷 크기.
 * @param free_value_func 값을 메모리에서 해제할 때 사용할 함수 포인터 (e.g., free).
 * @return 생성된 HashTable 객체 포인터.
 */
HashTable* ht_create(uint32_t size, void (*free_value_func)(void*));

/**
 * @brief 해시 테이블 및 모든 노드의 메모리를 해제합니다.
 * @param table 해제할 해시 테이블.
 */
void ht_destroy(HashTable* table);

/**
 * @brief 해시 테이블에 키-값 쌍을 삽입합니다. 키가 이미 존재하면 값을 덮어씁니다.
 * @param table 해시 테이블.
 * @param key 키 문자열.
 * @param value 저장할 값에 대한 포인터.
 * @return 성공 시 true, 실패 시 false.
 */
bool ht_insert(HashTable* table, const char* key, void* value);

/**
 * @brief 키를 사용하여 해시 테이블에서 값을 검색합니다.
 * @param table 해시 테이블.
 * @param key 검색할 키.
 * @return 값을 찾으면 값에 대한 포인터, 없으면 NULL.
 */
void* ht_get(HashTable* table, const char* key);

/**
 * @brief 키를 사용하여 해시 테이블에서 항목을 삭제합니다.
 * @param table 해시 테이블.
 * @param key 삭제할 키.
 * @return 성공 시 true, 키가 없을 경우 false.
 */
bool ht_delete(HashTable* table, const char* key);

/**
 * @brief 해시 테이블을 순회하며 각 항목에 대해 콜백 함수를 실행합니다.
 * @param table 해시 테이블.
 * @param callback 각 키-값 쌍에 대해 실행할 함수.
 * @param user_data 콜백 함수에 전달할 사용자 데이터.
 */
void ht_traverse(HashTable* table, void (*callback)(const char* key, void* value, void* user_data), void* user_data);



#endif 