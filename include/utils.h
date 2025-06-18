#ifndef UTILS_H
#define UTILS_H

#include "common.h"

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