#include "../include/utils.h"

/*
performance
 */
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

/*
logger
*/


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

/*
hash_table
*/


// djb2 해시 함수
static uint32_t hash_function(const char* key, uint32_t size) {
    unsigned long hash = 5381;
    int c;
    while ((c = *key++)) {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % size;
}

HashTable* ht_create(uint32_t size, void (*free_value_func)(void*)) {
    HashTable* table = (HashTable*)malloc(sizeof(HashTable));
    if (!table) {
        LOG_ERROR("HashTable", "해시 테이블 메모리 할당 실패");
        return NULL;
    }
    table->size = size;
    table->count = 0;
    table->free_value = free_value_func;
    table->buckets = (HashNode**)calloc(table->size, sizeof(HashNode*));
    if (!table->buckets) {
        LOG_ERROR("HashTable", "버킷 메모리 할당 실패");
        free(table);
        return NULL;
    }
    return table;
}

void ht_destroy(HashTable* table) {
    if (!table) return;
    for (uint32_t i = 0; i < table->size; i++) {
        HashNode* node = table->buckets[i];
        while (node) {
            HashNode* temp = node;
            node = node->next;
            free(temp->key);
            if (table->free_value) {
                table->free_value(temp->value);
            }
            free(temp);
        }
    }
    free(table->buckets);
    free(table);
}

bool ht_insert(HashTable* table, const char* key, void* value) {
    if (!table || !key || !value) return false;

    uint32_t index = hash_function(key, table->size);
    HashNode* node = table->buckets[index];

    // 키가 이미 존재하는지 확인 (덮어쓰기)
    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (table->free_value) {
                table->free_value(node->value);
            }
            node->value = value;
            return true;
        }
        node = node->next;
    }

    // 새 노드 생성
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));
    if (!new_node) return false;

    new_node->key = strdup(key);
    if (!new_node->key) {
        free(new_node);
        return false;
    }
    new_node->value = value;
    new_node->next = table->buckets[index];

    table->buckets[index] = new_node;
    table->count++;

    return true;
}

void* ht_get(HashTable* table, const char* key) {
    if (!table || !key) return NULL;
    uint32_t index = hash_function(key, table->size);
    HashNode* node = table->buckets[index];
    while (node) {
        if (strcmp(node->key, key) == 0) {
            return node->value;
        }
        node = node->next;
    }
    return NULL;
}

bool ht_delete(HashTable* table, const char* key) {
    if (!table || !key) return false;
    uint32_t index = hash_function(key, table->size);
    HashNode* node = table->buckets[index];
    HashNode* prev = NULL;

    while (node) {
        if (strcmp(node->key, key) == 0) {
            if (prev) {
                prev->next = node->next;
            } else {
                table->buckets[index] = node->next;
            }
            free(node->key);
            if (table->free_value) {
                table->free_value(node->value);
            }
            free(node);
            table->count--;
            return true;
        }
        prev = node;
        node = node->next;
    }
    return false;
}

void ht_traverse(HashTable* table, void (*callback)(const char* key, void* value, void* user_data), void* user_data) {
    if (!table || !callback) return;

    for (uint32_t i = 0; i < table->size; i++) {
        HashNode* node = table->buckets[i];
        while (node) {
            callback(node->key, node->value, user_data);
            node = node->next;
        }
    }
}