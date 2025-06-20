/**
 * @file utils.c
 * @brief 유틸리티 모듈 - 성능 측정, 로깅, 해시 테이블 기능
 * @details 시스템 전반에서 사용되는 공통 기능들을 제공합니다.
 */

#include "../include/utils.h"  // 유틸리티 관련 헤더 파일 포함
#include "../include/ui.h"    // UIManager, show_error_message 사용을 위해 추가

/*
error handling
*/

/**
 * @brief 에러 코드에 해당하는 메시지를 반환합니다.
 * @param error_code 에러 코드
 * @return 에러 메시지 문자열
 */
static const char* get_error_message(ErrorCode error_code) {
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
void error_report(ErrorCode error_code, const char* module, const char* additional_info, ...) {
    if (error_code == ERROR_NONE) return; // 성공인 경우 아무것도 하지 않음

    const char* error_msg = get_error_message(error_code);
    char formatted_message[512];
    
    if (additional_info && strlen(additional_info) > 0) {
        va_list args;
        va_start(args, additional_info);
        vsnprintf(formatted_message, sizeof(formatted_message), additional_info, args);
        va_end(args);
        
        extern UIManager* global_ui_manager;
        if (global_ui_manager) {
            char ui_msg[600];
            snprintf(ui_msg, sizeof(ui_msg), "[%s] %s: %s", module, error_msg, formatted_message);
            show_error_message(ui_msg);
        } else {
            fprintf(stderr, "[%s] %s: %s\n", module, error_msg, formatted_message);
        }
    } else {
        extern UIManager* global_ui_manager;
        if (global_ui_manager) {
            char ui_msg[600];
            snprintf(ui_msg, sizeof(ui_msg), "[%s] %s", module, error_msg);
            show_error_message(ui_msg);
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
uint64_t get_current_time(void) {
    struct timeval tv;  // 시간 구조체 선언
    if (gettimeofday(&tv, NULL) != 0) {  // 현재 시간 가져오기 실패 시
        error_report(ERROR_PERFORMANCE_TIME_FAILED, "Performance", "gettimeofday 실패");  // 에러 로그 출력
        return 0;  // 0 반환
    }
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;  // 마이크로초 단위로 변환하여 반환
}

/**
 * @brief 성능 통계를 안전하게 복사합니다.
 * @param stats 원본 성능 통계 포인터
 * @param output 복사할 대상 포인터
 */
void get_performance_stats(PerformanceStats* stats, PerformanceStats* output) {
    if (!stats || !output) return;  // 유효성 검사

    pthread_mutex_lock(&stats->mutex);  // 뮤텍스 잠금
    memcpy(output, stats, sizeof(PerformanceStats));  // 성능 통계 복사
    pthread_mutex_unlock(&stats->mutex);  // 뮤텍스 해제
}

/**
 * @brief 성능 통계를 콘솔에 출력합니다.
 * @param stats 출력할 성능 통계 포인터
 */
void print_performance_stats(PerformanceStats* stats) {
    if (!stats) {  // 통계 포인터가 NULL인 경우
        error_report(ERROR_PERFORMANCE_STATS_INVALID, "Performance", "잘못된 성능 통계 포인터");  // 에러 로그 출력
        return;  // 함수 종료
    }

    // LOG_INFO("Performance", "성능 통계 출력");  // 정보 로그 출력
    printf("=== 성능 통계 ===\n");  // 제목 출력
    printf("총 요청 수: %llu\n", stats->total_requests);  // 총 요청 수 출력
    printf("성공한 요청 수: %llu\n", stats->successful_requests);  // 성공한 요청 수 출력
    printf("실패한 요청 수: %llu\n", stats->failed_requests);  // 실패한 요청 수 출력
    printf("최대 동시 요청 수: %llu\n", stats->max_concurrent_requests);  // 최대 동시 요청 수 출력
    printf("평균 응답 시간: %llu ms\n",   // 평균 응답 시간 출력
           stats->total_requests > 0 ? stats->total_response_time / stats->total_requests : 0);
    printf("최소 응답 시간: %llu ms\n", stats->min_response_time);  // 최소 응답 시간 출력
    printf("최대 응답 시간: %llu ms\n", stats->max_response_time);  // 최대 응답 시간 출력
    printf("총 송신 바이트: %llu\n", stats->total_data_sent);  // 총 송신 바이트 출력
    printf("총 수신 바이트: %llu\n", stats->total_data_received);  // 총 수신 바이트 출력
  
    printf("총 에러 수: %llu\n", stats->total_errors);  // 총 에러 수 출력
    printf("================\n");  // 구분선 출력
}

/*
logger
*/

/* 전역 변수 */
static FILE* log_file = NULL;  // 로그 파일 포인터
static LogLevel current_log_level = LOG_INFO; // 기본 로그 레벨은 INFO
static pthread_mutex_t log_mutex;  // 로그 뮤텍스

/* 정적 함수 선언 */
static void write_to_log_file(LogLevel level, const char* category, const char* message);  // 로그 파일에 쓰기 함수
static const char* get_log_level_string_internal(LogLevel level);  // 로그 레벨 문자열 변환 함수

/**
 * @brief 로거를 초기화합니다.
 * @param filename 로그 파일 경로
 * @return 성공 시 0, 실패 시 -1
 */
int init_logger(const char* filename) {
    if (log_file != NULL) {  // 이미 초기화된 경우
        return -1; // 이미 초기화됨
    }

    log_file = fopen(filename, "a"); // 추가 모드로 열기
    if (log_file == NULL) {  // 파일 열기 실패 시
        return -1;  // 에러 코드 반환
    }

    pthread_mutex_init(&log_mutex, NULL);  // 로그 뮤텍스 초기화
    // LOG_INFO("System", "로거 초기화 완료. 로그 파일: %s", filename);  // 초기화 완료 로그
    return 0;  // 성공 코드 반환
}

/**
 * @brief 로거를 정리합니다.
 */
void cleanup_logger(void) {
    if (log_file != NULL) {  // 로그 파일이 열려있는 경우
        // LOG_INFO("System", "로거 정리 중...");  // 정리 시작 로그
        fclose(log_file);  // 로그 파일 닫기
        log_file = NULL;  // 포인터를 NULL로 설정
    }
    pthread_mutex_destroy(&log_mutex);  // 로그 뮤텍스 정리
}

/**
 * @brief 로그 메시지를 작성합니다.
 * @param level 로그 레벨
 * @param category 로그 카테고리
 * @param format 포맷 문자열
 * @param ... 가변 인자
 */
void log_message(LogLevel level, const char* category, const char* format, ...) {
    if (level > current_log_level) {  // 로그 레벨이 낮으면 출력하지 않음
        return; // 로그 레벨이 낮으면 출력하지 않음
    }

    char message_buffer[MAX_LOG_MSG];  // 메시지 버퍼
    va_list args;  // 가변 인자 리스트
    va_start(args, format);  // 가변 인자 시작
    vsnprintf(message_buffer, sizeof(message_buffer), format, args);  // 포맷된 메시지 생성
    va_end(args);  // 가변 인자 종료

    write_to_log_file(level, category, message_buffer);  // 로그 파일에 메시지 쓰기
}

/**
 * @brief 실제 로그 파일에 메시지를 쓰는 함수
 * @param level 로그 레벨
 * @param category 로그 카테고리
 * @param message 로그 메시지
 */
static void write_to_log_file(LogLevel level, const char* category, const char* message) {
    pthread_mutex_lock(&log_mutex);  // 로그 뮤텍스 잠금

    if (log_file != NULL) {  // 로그 파일이 열려있는 경우
        time_t rawtime;  // 원시 시간 변수
        struct tm *timeinfo;  // 시간 정보 구조체 포인터
        char timestamp_str[64];  // 타임스탬프 문자열 버퍼

        time(&rawtime);  // 현재 시간 가져오기
        timeinfo = localtime(&rawtime);  // 로컬 시간으로 변환
        strftime(timestamp_str, sizeof(timestamp_str), "%m월 %d일 %H:%M", timeinfo);  // 타임스탬프 포맷

        fprintf(log_file, "[%s] [%s] %s [%s]\n",  // 로그 메시지 출력
                get_log_level_string_internal(level),
                category,
                message,
                timestamp_str);
        fflush(log_file); // 즉시 디스크에 쓰기
    } else {  // 로그 파일이 열려있지 않은 경우
        error_report(ERROR_LOGGER_FILE_NOT_OPEN, "Logger", "로그 파일이 열려 있지 않습니다. 메시지: [%s] [%s] %s", 
                    get_log_level_string_internal(level), category, message);  // 경고 메시지 출력
    }

    pthread_mutex_unlock(&log_mutex);  // 로그 뮤텍스 해제
}

/**
 * @brief 로그 레벨을 문자열로 변환하는 내부 함수
 * @param level 로그 레벨
 * @return 로그 레벨에 해당하는 문자열
 */
static const char* get_log_level_string_internal(LogLevel level) {
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
const char* get_timestamp_string(time_t timestamp) {
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
static uint32_t hash_function(const char* key, uint32_t size) {
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
 * @param free_value_func 값 해제 함수 포인터
 * @return 생성된 HashTable 포인터, 실패 시 NULL
 */
HashTable* ht_create(uint32_t size, void (*free_value_func)(void*)) {
    HashTable* table = (HashTable*)malloc(sizeof(HashTable));  // 해시 테이블 메모리 할당
    if (!table) {  // 메모리 할당 실패 시
        error_report(ERROR_HASHTABLE_CREATION_FAILED, "HashTable", "해시 테이블 메모리 할당 실패");  // 에러 로그 출력
        return NULL;  // NULL 반환
    }
    table->size = size;  // 해시 테이블 크기 설정
    table->count = 0;  // 요소 개수를 0으로 초기화
    table->free_value = free_value_func;  // 값 해제 함수 설정
    table->buckets = (HashNode**)calloc(table->size, sizeof(HashNode*));  // 버킷 배열 메모리 할당
    if (!table->buckets) {  // 버킷 메모리 할당 실패 시
        error_report(ERROR_HASHTABLE_CREATION_FAILED, "HashTable", "버킷 메모리 할당 실패");  // 에러 로그 출력
        free(table);  // 해시 테이블 메모리 해제
        return NULL;  // NULL 반환
    }
    return table;  // 생성된 해시 테이블 반환
}

/**
 * @brief 해시 테이블을 정리합니다.
 * @param table 정리할 HashTable 포인터
 */
void ht_destroy(HashTable* table) {
    if (!table) return;  // 테이블이 NULL이면 함수 종료
    for (uint32_t i = 0; i < table->size; i++) {  // 모든 버킷에 대해 반복
        HashNode* node = table->buckets[i];  // 현재 버킷의 첫 번째 노드
        while (node) {  // 노드가 존재하는 동안 반복
            HashNode* temp = node;  // 임시 변수에 현재 노드 저장
            node = node->next;  // 다음 노드로 이동
            free(temp->key);  // 키 메모리 해제
            if (table->free_value) {  // 값 해제 함수가 존재하는 경우
                table->free_value(temp->value);  // 값 메모리 해제
            }
            free(temp);  // 노드 메모리 해제
        }
    }
    free(table->buckets);  // 버킷 배열 메모리 해제
    free(table);  // 해시 테이블 메모리 해제
}

/**
 * @brief 해시 테이블에 키-값 쌍을 삽입합니다.
 * @param table 해시 테이블 포인터
 * @param key 삽입할 키
 * @param value 삽입할 값
 * @return 성공 시 true, 실패 시 false
 */
bool ht_insert(HashTable* table, const char* key, void* value) {
    if (!table || !key || !value) {
        error_report(ERROR_INVALID_PARAMETER, "HashTable", "ht_insert: 잘못된 파라미터");
        return false;  // 유효성 검사
    }

    // LOG_INFO("HashTable", "해시 테이블 삽입 시작: 키=%s", key);

    uint32_t index = hash_function(key, table->size);  // 해시 인덱스 계산
    // LOG_INFO("HashTable", "해시 인덱스 계산: 키=%s, 인덱스=%u", key, index);
    
    HashNode* node = table->buckets[index];  // 해당 버킷의 첫 번째 노드
    
    // 기존 키가 있는지 확인
    while (node) {  // 노드가 존재하는 동안 반복
        if (strcmp(node->key, key) == 0) {  // 키가 일치하는 경우
            // LOG_INFO("HashTable", "기존 키 발견, 값 교체: 키=%s", key);
            if (table->free_value) {  // 값 해제 함수가 존재하는 경우
                table->free_value(node->value);  // 기존 값 메모리 해제
            }
            node->value = value;  // 새 값으로 교체
            // LOG_INFO("HashTable", "새 노드 삽입 성공: 키=%s, 인덱스=%u", key, index);
            return true;  // true 반환
        }
        node = node->next;  // 다음 노드로 이동
    }

    // 새 노드 생성
    HashNode* new_node = (HashNode*)malloc(sizeof(HashNode));  // 새 노드 메모리 할당
    if (!new_node) {  // 메모리 할당 실패 시
        error_report(ERROR_HASHTABLE_INSERT_FAILED, "HashTable", "새 노드 메모리 할당 실패: 키=%s", key);
        return false;  // false 반환
    }

    // 노드 초기화
    new_node->key = strdup(key);  // 키 복사
    if (!new_node->key) {  // 키 복사 실패 시
        error_report(ERROR_HASHTABLE_INSERT_FAILED, "HashTable", "키 복사 실패: 키=%s", key);
        free(new_node);  // 노드 메모리 해제
        return false;  // false 반환
    }
    new_node->value = value;  // 값 설정
    new_node->next = table->buckets[index];  // 기존 첫 번째 노드를 다음으로 설정
    table->buckets[index] = new_node;  // 새 노드를 첫 번째로 설정

    // LOG_INFO("HashTable", "새 노드 삽입 성공: 키=%s, 인덱스=%u", key, index);
    return true;  // true 반환
}

/**
 * @brief 해시 테이블에서 키에 해당하는 값을 조회합니다.
 * @param table 해시 테이블 포인터
 * @param key 조회할 키
 * @return 키에 해당하는 값, 없으면 NULL
 */
void* ht_get(HashTable* table, const char* key) {
    if (!table || !key) return NULL;  // 유효성 검사
    uint32_t index = hash_function(key, table->size);  // 해시 인덱스 계산
    HashNode* node = table->buckets[index];  // 해당 버킷의 첫 번째 노드
    while (node) {  // 노드가 존재하는 동안 반복
        if (strcmp(node->key, key) == 0) {  // 키가 일치하는 경우
            return node->value;  // 값 반환
        }
        node = node->next;  // 다음 노드로 이동
    }
    return NULL;  // 키를 찾지 못한 경우 NULL 반환
}

/**
 * @brief 해시 테이블에서 키-값 쌍을 삭제합니다.
 * @param table 해시 테이블 포인터
 * @param key 삭제할 키
 * @return 성공 시 true, 실패 시 false
 */
bool ht_delete(HashTable* table, const char* key) {
    if (!table || !key) {
        error_report(ERROR_INVALID_PARAMETER, "HashTable", "ht_delete: 잘못된 파라미터");
        return false;  // 유효성 검사
    }

    // LOG_INFO("HashTable", "해시 테이블 삭제 시작: 키=%s", key);

    uint32_t index = hash_function(key, table->size);  // 해시 인덱스 계산
    // LOG_INFO("HashTable", "해시 인덱스 계산: 키=%s, 인덱스=%u", key, index);
    
    HashNode* node = table->buckets[index];  // 해당 버킷의 첫 번째 노드
    HashNode* prev = NULL;  // 이전 노드 포인터

    while (node) {  // 노드가 존재하는 동안 반복
        if (strcmp(node->key, key) == 0) {  // 키가 일치하는 경우
            // LOG_INFO("HashTable", "삭제할 노드 발견: 키=%s", key);
            
            if (prev) {  // 이전 노드가 존재하는 경우
                prev->next = node->next;  // 이전 노드의 다음을 현재 노드의 다음으로 설정
            } else {  // 첫 번째 노드인 경우
                table->buckets[index] = node->next;  // 버킷의 첫 번째를 다음 노드로 설정
            }

            free(node->key);  // 키 메모리 해제
            if (table->free_value) {  // 값 해제 함수가 존재하는 경우
                table->free_value(node->value);  // 값 메모리 해제
            }
            free(node);  // 노드 메모리 해제

            // LOG_INFO("HashTable", "노드 삭제 성공: 키=%s", key);
            return true;  // true 반환
        }
        prev = node;  // 이전 노드를 현재 노드로 설정
        node = node->next;  // 다음 노드로 이동
    }

    // LOG_WARNING("HashTable", "삭제할 키를 찾을 수 없음: 키=%s", key);
    return false;  // false 반환
}

/**
 * @brief 해시 테이블의 모든 요소에 대해 콜백 함수를 실행합니다.
 * @param table 해시 테이블 포인터
 * @param callback 실행할 콜백 함수
 * @param user_data 콜백 함수에 전달할 사용자 데이터
 */
void ht_traverse(HashTable* table, void (*callback)(const char* key, void* value, void* user_data), void* user_data) {
    if (!table || !callback) {
        error_report(ERROR_INVALID_PARAMETER, "HashTable", "ht_traverse: 잘못된 파라미터");
        return;  // 유효성 검사
    }

    // LOG_INFO("HashTable", "해시 테이블 순회 시작: 크기=%u", table->size);

    for (uint32_t i = 0; i < table->size; i++) {  // 모든 버킷에 대해 반복
        HashNode* node = table->buckets[i];  // 현재 버킷의 첫 번째 노드
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

    // LOG_INFO("HashTable", "해시 테이블 순회 완료");
}