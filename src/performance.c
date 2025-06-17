#include "../include/performance.h"
#include "../include/logger.h"


/* 현재 시간을 마이크로초 단위로 반환 */
uint64_t get_current_time(void) {
    struct timeval tv;
    if (gettimeofday(&tv, NULL) != 0) {
        LOG_ERROR("Performance", "gettimeofday 실패");
        return 0;
    }
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

/* 경과 시간을 마이크로초 단위로 계산 */
uint64_t get_elapsed_time(uint64_t start_time) {
    uint64_t current_time = get_current_time();
    if (current_time < start_time) {
        LOG_ERROR("Performance", "시간 계산 오류: 현재 시간이 시작 시간보다 이전");
        return 0;
    }
    return current_time - start_time;
}

/* 성능 통계 초기화 */
PerformanceStats* init_performance_stats(void) {
    PerformanceStats* stats = (PerformanceStats*)malloc(sizeof(PerformanceStats));
    if (!stats) {
        LOG_ERROR("Performance", "성능 통계 메모리 할당 실패");
        return NULL;
    }

    // 뮤텍스 초기화
    if (pthread_mutex_init(&stats->mutex, NULL) != 0) {
        LOG_ERROR("Performance", "뮤텍스 초기화 실패");
        free(stats);
        return NULL;
    }

    // 통계 초기화
    stats->start_time = get_current_time();
    stats->total_requests = 0;
    stats->successful_requests = 0;
    stats->failed_requests = 0;
    stats->total_response_time = 0;
    stats->min_response_time = UINT64_MAX;
    stats->max_response_time = 0;
    stats->total_data_sent = 0;
    stats->total_data_received = 0;
    stats->active_connections = 0;
    stats->peak_connections = 0;
    stats->total_errors = 0;
    stats->total_warnings = 0;

    LOG_INFO("Performance", "성능 통계 초기화 완료");
    return stats;
}

/* 성능 통계 정리 */
void cleanup_performance_stats(PerformanceStats* stats) {
    if (!stats) return;

    pthread_mutex_destroy(&stats->mutex);
    free(stats);
}

/* 요청 처리 시작 */
void start_request(PerformanceStats* stats) {
    if (!stats) {
        LOG_ERROR("Performance", "잘못된 성능 통계 포인터");
        return;
    }

    LOG_INFO("Performance", "요청 시작");
    stats->current_request_start = get_current_time();
    stats->active_requests++;
    if (stats->active_requests > stats->max_concurrent_requests) {
        stats->max_concurrent_requests = stats->active_requests;
    }
}

/* 요청 처리 완료 */
void end_request(PerformanceStats* stats, bool success, uint64_t response_time) {
    if (!stats) {
        LOG_ERROR("Performance", "잘못된 성능 통계 포인터");
        return;
    }

    LOG_INFO("Performance", "요청 종료: 성공=%d, 응답시간=%llu", success, response_time);
    stats->total_requests++;
    if (success) {
        stats->successful_requests++;
    } else {
        stats->failed_requests++;
    }

    stats->total_response_time += response_time;
    if (response_time > stats->max_response_time) {
        stats->max_response_time = response_time;
    }
    if (response_time < stats->min_response_time || stats->min_response_time == 0) {
        stats->min_response_time = response_time;
    }

    stats->active_requests--;
}

/* 데이터 전송 통계 업데이트 */
void update_data_stats(PerformanceStats* stats, size_t sent, size_t received) {
    if (!stats) {
        LOG_ERROR("Performance", "잘못된 성능 통계 포인터");
        return;
    }

    LOG_INFO("Performance", "데이터 통계 업데이트: 송신=%zu, 수신=%zu", sent, received);
    
    stats->total_data_sent += sent;
    stats->total_data_received += received;
}

/* 에러 통계 업데이트 */
void update_error_stats(PerformanceStats* stats, bool is_error) {
    if (!stats) {
        LOG_ERROR("Performance", "잘못된 성능 통계 포인터");
        return;
    }

    LOG_INFO("Performance", "에러 통계 업데이트: 에러=%d", is_error);
    if (is_error) {
        stats->total_errors++;
    }
}

/* 성능 통계 가져오기 */
void get_performance_stats(PerformanceStats* stats, PerformanceStats* output) {
    if (!stats || !output) return;

    pthread_mutex_lock(&stats->mutex);
    memcpy(output, stats, sizeof(PerformanceStats));
    pthread_mutex_unlock(&stats->mutex);
}

/* 성능 통계 출력 */
void print_performance_stats(PerformanceStats* stats) {
    if (!stats) {
        LOG_ERROR("Performance", "잘못된 성능 통계 포인터");
        return;
    }

    LOG_INFO("Performance", "성능 통계 출력");
    printf("=== 성능 통계 ===\n");
    printf("총 요청 수: %llu\n", stats->total_requests);
    printf("성공한 요청 수: %llu\n", stats->successful_requests);
    printf("실패한 요청 수: %llu\n", stats->failed_requests);
    printf("최대 동시 요청 수: %llu\n", stats->max_concurrent_requests);
    printf("평균 응답 시간: %llu ms\n", 
           stats->total_requests > 0 ? stats->total_response_time / stats->total_requests : 0);
    printf("최소 응답 시간: %llu ms\n", stats->min_response_time);
    printf("최대 응답 시간: %llu ms\n", stats->max_response_time);
    printf("총 송신 바이트: %llu\n", stats->total_data_sent);
    printf("총 수신 바이트: %llu\n", stats->total_data_received);
  
    printf("총 에러 수: %llu\n", stats->total_errors);
    printf("================\n");
}

/* 성능 통계 초기화 */
void reset_performance_stats(PerformanceStats* stats) {
    if (!stats) return;

    pthread_mutex_lock(&stats->mutex);
    stats->start_time = get_current_time();
    stats->total_requests = 0;
    stats->successful_requests = 0;
    stats->failed_requests = 0;
    stats->total_response_time = 0;
    stats->min_response_time = UINT64_MAX;
    stats->max_response_time = 0;
    stats->total_data_sent = 0;
    stats->total_data_received = 0;
    stats->active_connections = 0;
    stats->peak_connections = 0;
    stats->total_errors = 0;
    stats->total_warnings = 0;
    pthread_mutex_unlock(&stats->mutex);

    LOG_INFO("Performance", "성능 통계 초기화됨");
}

/* 버퍼 풀 초기화 */
int init_buffer_pool(BufferPool* pool, int max_buffers, size_t buffer_size) {
    if (!pool || max_buffers <= 0 || buffer_size <= 0) {
        LOG_ERROR("Performance", "잘못된 버퍼 풀 파라미터");
        return -1;
    }

    pool->buffers = (Buffer*)malloc(max_buffers * sizeof(Buffer));
    if (!pool->buffers) {
        LOG_ERROR("Performance", "버퍼 풀 메모리 할당 실패");
        return -1;
    }

    pool->max_buffers = max_buffers;
    pool->buffer_size = buffer_size;
    pool->used_buffers = 0;

    // 각 버퍼 초기화
    for (int i = 0; i < max_buffers; i++) {
        pool->buffers[i].data = (char*)malloc(buffer_size);
        if (!pool->buffers[i].data) {
            LOG_ERROR("Performance", "버퍼 메모리 할당 실패");
            // 이미 할당된 버퍼들 정리
            for (int j = 0; j < i; j++) {
                free(pool->buffers[j].data);
            }
            free(pool->buffers);
            return -1;
        }
        pool->buffers[i].size = 0;
        pool->buffers[i].in_use = false;
    }

    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        LOG_ERROR("Performance", "버퍼 풀 뮤텍스 초기화 실패");
        for (int i = 0; i < max_buffers; i++) {
            free(pool->buffers[i].data);
        }
        free(pool->buffers);
        return -1;
    }

    return 0;
}

/* 버퍼 풀 정리 */
void cleanup_buffer_pool(BufferPool* pool) {
    if (!pool) return;

    if (pool->buffers) {
        for (int i = 0; i < pool->max_buffers; i++) {
            if (pool->buffers[i].data) {
                free(pool->buffers[i].data);
            }
        }
        free(pool->buffers);
    }

    pthread_mutex_destroy(&pool->mutex);
}

/* 배치 버퍼 초기화 */
BatchBuffer* init_batch_buffer(size_t max_messages) {
    BatchBuffer* buffer = (BatchBuffer*)malloc(sizeof(BatchBuffer));
    if (!buffer) {
        LOG_ERROR("Performance", "배치 버퍼 메모리 할당 실패");
        return NULL;
    }

    buffer->messages = (Message**)malloc(max_messages * sizeof(Message*));
    if (!buffer->messages) {
        LOG_ERROR("Performance", "메시지 배열 메모리 할당 실패");
        free(buffer);
        return NULL;
    }

    buffer->max_messages = max_messages;
    buffer->message_count = 0;
    buffer->last_flush = time(NULL);

    return buffer;
}

/* 배치 버퍼 정리 */
void cleanup_batch_buffer(BatchBuffer* buffer) {
    if (!buffer) return;

    if (buffer->messages) {
        free(buffer->messages);
    }

    pthread_mutex_destroy(&buffer->mutex);
}

/* 성능 모니터링 스레드 함수 */
void* performance_monitor_thread(void* arg) {
    PerformanceMonitor* monitor = (PerformanceMonitor*)arg;
    if (!monitor) return NULL;

    while (monitor->running) {
        // 성능 통계 가져오기
        PerformanceStats stats;
        get_performance_stats(monitor->stats, &stats);

        // 성능 통계 출력
        print_performance_stats(&stats);

        // 대기
        sleep(monitor->interval);
    }

    return NULL;
}

/* 성능 모니터링 스레드 생성 */
int create_performance_monitor(PerformanceMonitor* monitor, PerformanceStats* stats, int interval) {
    if (!monitor || !stats || interval <= 0) {
        LOG_ERROR("Performance", "잘못된 성능 모니터링 파라미터");
        return -1;
    }

    monitor->stats = stats;
    monitor->interval = interval;
    monitor->running = true;

    // 성능 모니터링 스레드 생성
    int ret = pthread_create(&monitor->thread, NULL, performance_monitor_thread, monitor);
    if (ret != 0) {
        LOG_ERROR("Performance", "성능 모니터링 스레드 생성 실패: %s", strerror(ret));
        return -1;
    }

    return 0;
}

/* 성능 모니터링 스레드 정리 */
void cleanup_performance_monitor(PerformanceMonitor* monitor) {
    if (!monitor) return;

    monitor->running = false;
    pthread_join(monitor->thread, NULL);
} 