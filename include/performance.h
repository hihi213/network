#ifndef PERFORMANCE_H
#define PERFORMANCE_H

#include "common.h"
#include "message.h"

/* 버퍼 관련 상수 */
#define BUFFER_POOL_SIZE 100
#define MAX_BATCH_SIZE 50

/* 성능 통계 구조체 */
typedef struct {
    uint64_t start_time;
    uint64_t total_requests;
    uint64_t successful_requests;
    uint64_t failed_requests;
    uint64_t total_response_time;
    uint64_t min_response_time;
    uint64_t max_response_time;
    uint64_t total_data_sent;
    uint64_t total_data_received;
    uint64_t active_connections;
    uint64_t peak_connections;
    uint64_t total_errors;
    uint64_t total_warnings;

    uint64_t current_request_start;
    uint64_t active_requests;
    uint64_t max_concurrent_requests;

    pthread_mutex_t mutex;
} PerformanceStats;

typedef struct {
    char* data;
    size_t size;
    bool in_use;
} Buffer;

typedef struct {
    Buffer* buffers;
    int max_buffers;
    size_t buffer_size;
    int used_buffers;
    pthread_mutex_t mutex;
} BufferPool;


/* 쓰기 버퍼 구조체 */
typedef struct {
    char* buffer;
    size_t size;
    size_t position;
    pthread_mutex_t mutex;
} WriteBuffer;

/* 배치 버퍼 구조체 */
typedef struct {
    Message** messages;
    int message_count;
    int max_messages;
    time_t last_flush;
    pthread_mutex_t mutex;
} BatchBuffer;

/* 메모리 관리 함수 */
void* safe_malloc(size_t size);
void* safe_calloc(size_t nmemb, size_t size);
void safe_free(void* ptr);

/* 버퍼 풀 함수 */
int init_buffer_pool(BufferPool* pool, int max_buffers, size_t buffer_size);
void cleanup_buffer_pool(BufferPool* pool);
char* get_buffer(BufferPool* pool);
void release_buffer(BufferPool* pool, char* buffer);

/* 배치 처리 함수 */
BatchBuffer* init_batch_buffer(size_t max_messages);
void cleanup_batch_buffer(BatchBuffer* buffer);
int batch_write(BatchBuffer* buffer, const char* message);
int flush_batch(BatchBuffer* buffer);

/* 성능 측정 함수 */
PerformanceStats* init_performance_stats(void);
void cleanup_performance_stats(PerformanceStats* stats);
void start_request(PerformanceStats* stats);
void end_request(PerformanceStats* stats, bool success, uint64_t response_time);
void update_data_stats(PerformanceStats* stats, size_t sent, size_t received);
void update_error_stats(PerformanceStats* stats, bool is_error);
void get_performance_stats(PerformanceStats* stats, PerformanceStats* output);
void print_performance_stats(PerformanceStats* stats);
void reset_performance_stats(PerformanceStats* stats);

/* 시간 측정 함수 */
uint64_t get_current_time(void);
uint64_t get_elapsed_time(uint64_t start_time);

/* 시스템 자원 측정 함수 */
double get_cpu_usage(void);
uint64_t get_memory_usage(void);

/* 성능 통계 출력 함수 */
int save_performance_stats(const PerformanceStats* stats, const char* filename);

/* 성능 모니터링 구조체 */
typedef struct {
    PerformanceStats* stats;
    int interval;
    pthread_t thread;
    bool running;
} PerformanceMonitor;

/* 성능 모니터링 함수 */
void* performance_monitor_thread(void* arg);
int create_performance_monitor(PerformanceMonitor* monitor, PerformanceStats* stats, int interval);
void cleanup_performance_monitor(PerformanceMonitor* monitor);

#endif /* PERFORMANCE_H */ 