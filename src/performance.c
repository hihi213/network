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

