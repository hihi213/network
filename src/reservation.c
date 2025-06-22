/**
 * @file reservation.c
 * @brief 예약 관리 시스템 - 타임휠 기반 만료 처리 및 해시 테이블 기반 고속 조회
 * 
 * @details
 * 이 모듈은 장비 예약 시스템의 핵심 로직을 구현합니다:
 * 
 * 1. **예약 생명주기 관리**: 생성 → 승인 → 완료/취소 → 정리
 * 2. **타임휠(Time Wheel) 알고리즘**: O(1) 시간 복잡도로 예약 만료 처리
 * 3. **해시 테이블 기반 조회**: O(1) 평균 시간 복잡도로 예약 검색
 * 4. **동시성 제어**: 뮤텍스 기반 스레드 안전성 보장
 * 
 * @note 타임휠 알고리즘은 대용량 예약 시스템에서 효율적인 만료 처리를 위해 선택되었습니다.
 *       전통적인 방식(모든 예약을 순회하며 만료 확인)의 O(n) 복잡도를 O(1)로 개선합니다.
 */

#include "../include/reservation.h"
#include "../include/resource.h"
#include "../include/message.h"

// 백그라운드 정리를 위한 전역 변수
static pthread_t cleanup_thread;
static bool cleanup_thread_running = false;
static reservation_manager_t* global_manager = NULL;
static resource_manager_t* global_resource_manager = NULL;
static void* reservation_cleanup_thread_function(void* arg);

/**
 * @brief 예약 충돌 검사를 위한 컨텍스트 데이터
 * @details 해시 테이블 순회 시 각 예약과의 시간 겹침을 확인하기 위한 임시 데이터 구조
 */
typedef struct {
    const char* device_id;    ///< 검사 대상 장비 ID
    time_t start_time;        ///< 예약 시작 시간
    time_t end_time;          ///< 예약 종료 시간
    bool has_conflict;        ///< 충돌 발견 여부
} conflict_check_data_t;

/**
 * @brief 예약 충돌 검사 콜백 함수
 * @details utils_hashtable_traverse에 의해 각 예약마다 호출되어 시간 겹침을 검사합니다.
 * 
 * 시간 겹침 판정 공식: !(A.end <= B.start || A.start >= B.end)
 * 이는 두 시간 구간이 겹치지 않는 조건의 논리적 부정입니다.
 * 
 * @param key 해시 테이블 키 (사용되지 않음)
 * @param value 예약 객체 포인터
 * @param user_data conflict_check_data_t 구조체 포인터
 */
static void reservation_conflict_check_callback(const char* key, void* value, void* user_data) {
    (void)key; // key는 사용되지 않음
    reservation_t* reservation = (reservation_t*)value;
    conflict_check_data_t* data = (conflict_check_data_t*)user_data;
    
    // 동일 장비의 승인된 예약만 검사 (취소/완료된 예약은 제외)
    if (strcmp(reservation->device_id, data->device_id) == 0 &&
        reservation->status == RESERVATION_APPROVED) {
        
        // 시간 겹침 확인: 두 구간이 겹치지 않는 조건의 부정
        if (!(data->end_time <= reservation->start_time ||
              data->start_time >= reservation->end_time)) {
            data->has_conflict = true;
        }
    }
}

/**
 * @brief 타임휠 자료구조 초기화
 * @details 타임휠은 원형 배열 형태로 구현되며, 각 버킷은 만료 시간이 같은 예약들의 연결 리스트를 가집니다.
 * 
 * 타임휠의 장점:
 * - O(1) 시간 복잡도로 만료 처리
 * - 메모리 사용량이 예약 수에 비례하지 않음
 * - 스케줄링 오버헤드 최소화
 * 
 * @return 초기화된 타임휠 포인터, 실패 시 NULL
 */
static time_wheel_t* time_wheel_init(void) {
    time_wheel_t* wheel = malloc(sizeof(time_wheel_t));
    if (!wheel) return NULL;

    wheel->size = TIME_WHEEL_SIZE;
    wheel->current_index = 0;
    wheel->base_time = time(NULL);
    
    // 모든 버킷을 NULL로 초기화
    for (int i = 0; i < wheel->size; i++) {
        wheel->buckets[i] = NULL;
    }
    
    pthread_mutex_init(&wheel->mutex, NULL);
    return wheel;
}

/**
 * @brief 예약을 타임휠에 추가
 * @details 예약의 만료 시간을 기준으로 타임휠의 적절한 버킷에 삽입합니다.
 * 
 * 타임휠 삽입 알고리즘:
 * 1. 현재 시간과 만료 시간의 차이를 계산
 * 2. 차이를 타임휠 크기로 나누어 cycle 수 결정
 * 3. 나머지를 이용해 버킷 인덱스 계산
 * 4. 해당 버킷의 연결 리스트에 노드 추가
 * 
 * @param wheel 타임휠 포인터
 * @param res 추가할 예약 객체
 */
static void time_wheel_add(time_wheel_t* wheel, reservation_t* res) {
    if (!wheel || !res) {
        LOG_WARNING("TimeWheel", "타임휠 추가 실패: 유효하지 않은 인자. 예약ID=%u", 
                   res ? res->id : 0);
        return;
    }

    time_t now = time(NULL);
    if (res->end_time <= now) {
        LOG_WARNING("TimeWheel", "타임휠 추가 실패: 이미 만료된 예약. 예약ID=%u", res->id);
        return;
    }
    
    long remaining_seconds = res->end_time - now;

    time_wheel_node_t* node = malloc(sizeof(time_wheel_node_t));
    if (!node) {
        LOG_ERROR("TimeWheel", "타임휠 노드 메모리 할당 실패: 예약ID=%u", res->id);
        return;
    }

    node->reservation = res;
    res->time_wheel_node = node; // 예약 객체에서 타임휠 노드를 가리키는 역방향 포인터

    // 타임휠 삽입 위치 계산
    // cycle: 몇 바퀴 돌아야 하는지 (remaining_seconds / wheel->size)
    // bucket_index: 현재 바퀴에서 어느 버킷에 위치할지
    node->cycle = remaining_seconds / wheel->size;
    int bucket_index = (wheel->current_index + (remaining_seconds % wheel->size)) % wheel->size;
    
    LOG_INFO("TimeWheel", "타임휠 추가: 예약ID=%u, 장비=%s, 남은시간=%ld초, cycle=%d, 버킷=%d", 
             res->id, res->device_id, remaining_seconds, node->cycle, bucket_index);

    // 스레드 안전성을 위해 뮤텍스로 보호
    pthread_mutex_lock(&wheel->mutex);
    node->next = wheel->buckets[bucket_index];
    wheel->buckets[bucket_index] = node;
    pthread_mutex_unlock(&wheel->mutex);
}

/**
 * @brief 타임휠 시간 진행 및 만료 처리
 * @details 타임휠의 핵심 알고리즘으로, 매 호출마다 1초씩 시간을 진행시키며 만료된 예약을 처리합니다.
 * 
 * 처리 로직:
 * 1. 현재 인덱스의 버킷에서 모든 노드를 꺼냄
 * 2. 각 노드에 대해:
 *    - cycle > 0: cycle 감소 후 현재 버킷에 재삽입
 *    - cycle = 0: 만료 시간 확인 후 처리
 *      * 만료됨: 상태를 COMPLETED로 변경, 해시 테이블에서 제거
 *      * 아직 유효: 다음 버킷에 재삽입
 *    - 취소됨: 해시 테이블에서 제거
 * 
 * @note 이 함수는 백그라운드 스레드에서 주기적으로 호출됩니다.
 * @param manager 예약 관리자 포인터
 */
static void time_wheel_tick(reservation_manager_t* manager) {
    time_wheel_t* wheel = manager->time_wheel;
    
    // 락 경합을 최소화하기 위해 처리할 노드들을 락 외부로 분리
    pthread_mutex_lock(&wheel->mutex);
    wheel->current_index = (wheel->current_index + 1) % wheel->size;
    time_wheel_node_t* node = wheel->buckets[wheel->current_index];
    wheel->buckets[wheel->current_index] = NULL;
    pthread_mutex_unlock(&wheel->mutex);

    // 처리할 노드 수 로깅
    int node_count = 0;
    time_wheel_node_t* temp = node;
    while (temp) {
        node_count++;
        temp = temp->next;
    }
    
    if (node_count > 0) {
        LOG_INFO("TimeWheel", "버킷[%d] 처리 시작: %d개 노드, 현재시간: %ld", 
                 wheel->current_index, node_count, time(NULL));
    }

    bool updated = false;
    int processed_count = 0;
    int expired_count = 0;
    int recycled_count = 0;
    
    while (node) {
        time_wheel_node_t* next = node->next;
        processed_count++;

        reservation_t* res_snapshot = node->reservation;
        bool should_free_node = false;

        pthread_mutex_lock(&manager->mutex);

        char id_str[16];
        snprintf(id_str, sizeof(id_str), "%u", res_snapshot->id);
        reservation_t* live_res = (reservation_t*)utils_hashtable_get(manager->reservation_map, id_str);

        if (!live_res) { 
            // 이미 다른 곳에서 정리된 경우 (동시성 제어)
            should_free_node = true;
        } else if (live_res->status == RESERVATION_CANCELLED) {
            // 소프트 삭제된 취소 예약을 최종 정리
            LOG_INFO("TimeWheel", "취소된 예약 정리: 예약ID=%u", live_res->id);
            utils_hashtable_delete(manager->reservation_map, id_str);
            should_free_node = true;
            expired_count++;
        } else if (live_res->status == RESERVATION_APPROVED) {
            if (node->cycle == 0) {
                time_t current_time = time(NULL);
                if (live_res->end_time <= current_time) {
                    // 예약 시간이 만료된 경우
                    LOG_INFO("TimeWheel", "예약 만료 처리: 예약ID=%u", live_res->id);
                    live_res->status = RESERVATION_COMPLETED;
                    resource_update_device_status(global_resource_manager, live_res->device_id, DEVICE_AVAILABLE, 0);
                    updated = true;
                    utils_hashtable_delete(manager->reservation_map, id_str);
                    should_free_node = true;
                    expired_count++;
                }
            }
        }
        
        pthread_mutex_unlock(&manager->mutex);
        
        if (should_free_node) {
            free(node);
        } else {
            recycled_count++;
            if (node->cycle > 0) {
                // cycle이 남은 경우, cycle을 감소시키고 현재 버킷에 다시 삽입
                node->cycle--;
                pthread_mutex_lock(&wheel->mutex);
                node->next = wheel->buckets[wheel->current_index];
                wheel->buckets[wheel->current_index] = node;
                pthread_mutex_unlock(&wheel->mutex);
            } else {
                // cycle이 0이지만 아직 만료되지 않은 경우, 다음 틱에 확인하기 위해 다음 버킷으로 이동
                int next_bucket_index = (wheel->current_index + 1) % wheel->size;
                pthread_mutex_lock(&wheel->mutex);
                node->next = wheel->buckets[next_bucket_index];
                wheel->buckets[next_bucket_index] = node;
                pthread_mutex_unlock(&wheel->mutex);
            }
        }
        node = next;
    }
    
    if (node_count > 0) {
        LOG_INFO("TimeWheel", "버킷[%d] 처리 완료: 총 %d, 만료 %d, 재사용 %d", 
                 wheel->current_index, processed_count, expired_count, recycled_count);
    }
    
    // 장비 상태에 변경이 있었으면 클라이언트들에게 브로드캐스트
    if (updated && manager->broadcast_callback) {
        LOG_INFO("TimeWheel", "상태 변경 감지, 브로드캐스트 콜백 호출");
        manager->broadcast_callback();
    }
}

/**
 * @brief 타임휠 관련 모든 리소스를 정리합니다.
 * @param wheel 정리할 타임휠 포인터
 */
static void time_wheel_cleanup(time_wheel_t* wheel) {
    if (!wheel) return;
    for (int i = 0; i < wheel->size; i++) {
        time_wheel_node_t* node = wheel->buckets[i];
        while (node) {
            time_wheel_node_t* temp = node;
            node = node->next;
            free(temp);
        }
    }
    pthread_mutex_destroy(&wheel->mutex);
    free(wheel);
}

/**
 * @brief 1초마다 타임휠의 tick 함수를 호출하는 백그라운드 스레드 함수
 */
static void* reservation_cleanup_thread_function(void* arg) {
    reservation_manager_t* manager = (reservation_manager_t*)arg;
    while (cleanup_thread_running) {
        sleep(1);
        if (manager) {
            time_wheel_tick(manager);
        }
    }
    return NULL;
}

/**
 * @brief 예약 관리자를 초기화합니다.
 * @details 해시 테이블과 타임휠을 초기화하고, 정리 스레드를 생성합니다.
 * @param res_manager 리소스 관리자 포인터
 * @param callback 상태 변경 시 호출될 브로드캐스트 콜백 함수
 * @return 성공 시 초기화된 예약 관리자 포인터, 실패 시 NULL
 */
reservation_manager_t* reservation_init_manager(resource_manager_t* res_manager, void (*callback)(void)) {
    reservation_manager_t* manager = (reservation_manager_t*)malloc(sizeof(reservation_manager_t));
    if (!manager) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Reservation", "예약 관리자 메모리 할당 실패");
        return NULL;
    }

    if (!utils_init_manager_base(manager, sizeof(reservation_manager_t), &manager->reservation_map, MAX_RESERVATIONS, free, &manager->mutex)) {
        utils_report_error(ERROR_HASHTABLE_CREATION_FAILED, "Reservation", "매니저 공통 초기화 실패");
        free(manager);
        return NULL;
    }

    // 예약 관리자 특화 초기화
    manager->reservation_count = 0;
    manager->next_reservation_id = 1;
    manager->broadcast_callback = callback;

    // 타임휠 초기화
    manager->time_wheel = time_wheel_init();
    if (!manager->time_wheel) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Reservation", "타임휠 초기화 실패");
        utils_hashtable_destroy(manager->reservation_map);
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    global_manager = manager;
    global_resource_manager = res_manager;
    cleanup_thread_running = true;

    // 예약 정리 스레드 생성
    if (pthread_create(&cleanup_thread, NULL, reservation_cleanup_thread_function, manager) != 0) {
        utils_report_error(ERROR_INVALID_STATE, "Reservation", "만료 예약 정리 스레드 생성 실패");
        cleanup_thread_running = false;
        global_manager = NULL;
        time_wheel_cleanup(manager->time_wheel);
        utils_hashtable_destroy(manager->reservation_map);
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    return manager;
}

/**
 * @brief 특정 장비에 현재 활성화된 예약을 조회합니다.
 * @param resv_manager 예약 관리자 포인터
 * @param rsrc_manager 리소스 관리자 포인터
 * @param device_id 장비 ID
 * @return 성공 시 활성 예약 객체 포인터, 실패 또는 예약 없음 시 NULL
 */
reservation_t* reservation_get_active_for_device(reservation_manager_t* resv_manager, resource_manager_t* rsrc_manager, const char* device_id) {
    if (!resv_manager || !rsrc_manager || !device_id) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Reservation", "reservation_get_active_for_device: 잘못된 파라미터");
        return NULL;
    }

    device_t* device = (device_t*)utils_hashtable_get(rsrc_manager->devices, device_id);
    if (!device || device->active_reservation_id == 0) {
        return NULL;
    }

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", device->active_reservation_id);
    
    pthread_mutex_lock(&resv_manager->mutex);
    reservation_t* reservation = (reservation_t*)utils_hashtable_get(resv_manager->reservation_map, id_str);
    pthread_mutex_unlock(&resv_manager->mutex);

    if (reservation && reservation->status == RESERVATION_APPROVED) {
        return reservation;
    }

    return NULL;
}

/**
 * @brief 예약 관리자의 모든 리소스를 정리합니다.
 * @details 정리 스레드를 안전하게 종료하고, 타임휠과 해시 테이블 메모리를 해제합니다.
 * @param manager 정리할 예약 관리자 포인터
 */
void reservation_cleanup_manager(reservation_manager_t* manager) {
    if (!manager) return;

    if (cleanup_thread_running) {
        cleanup_thread_running = false;
        pthread_join(cleanup_thread, NULL);
        global_manager = NULL;
    }

    if (manager->time_wheel) {
        time_wheel_cleanup(manager->time_wheel);
    }
    
    if (manager->reservation_map) {
        utils_hashtable_destroy(manager->reservation_map);
    }
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * @brief 새로운 예약을 생성합니다.
 * @details 예약 시간 충돌을 검사한 후, 예약 객체를 생성하여 해시 테이블과 타임휠에 추가합니다.
 *          성공 시 해당 장비의 상태도 '예약됨'으로 변경합니다.
 * @param manager 예약 관리자 포인터
 * @param device_id 예약할 장비 ID
 * @param username 예약자 이름
 * @param start_time 예약 시작 시간
 * @param end_time 예약 종료 시간
 * @param reason 예약 사유
 * @return 성공 시 생성된 예약 ID, 실패 시 0
 */
uint32_t reservation_create(reservation_manager_t* manager, const char* device_id,
                            const char* username, time_t start_time,
                            time_t end_time, const char* reason) {
    if (!manager || !device_id || !username || !reason) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Reservation", "잘못된 파라미터");
        return 0;
    }

    pthread_mutex_lock(&manager->mutex);

    if (manager->reservation_count >= MAX_RESERVATIONS) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_RESERVATION_MAX_LIMIT_REACHED, "Reservation", "예약 최대 개수 초과");
        return 0;
    }
    
    if (start_time >= end_time || start_time < time(NULL)) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_RESERVATION_INVALID_TIME, "Reservation", "잘못된 예약 시간");
        return 0;
    }

    // 해시 테이블 전체를 순회하여 시간 충돌 검사
    conflict_check_data_t conflict_data = {
        .device_id = device_id,
        .start_time = start_time,
        .end_time = end_time,
        .has_conflict = false
    };
    utils_hashtable_traverse(manager->reservation_map, reservation_conflict_check_callback, &conflict_data);
    
    if (conflict_data.has_conflict) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_RESERVATION_CONFLICT, "Reservation", "해당 장비는 요청된 시간에 이미 예약이 존재합니다.");
        return 0;
    }

    uint32_t reservation_id = manager->next_reservation_id++;
    
    reservation_t* new_reservation = (reservation_t*)malloc(sizeof(reservation_t));
    if (!new_reservation) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Reservation", "예약 객체 메모리 할당 실패");
        return 0;
    }
    
    // 새 예약 정보 채우기
    new_reservation->id = reservation_id;
    strncpy(new_reservation->device_id, device_id, MAX_DEVICE_ID_LEN - 1);
    new_reservation->device_id[MAX_DEVICE_ID_LEN - 1] = '\0';
    strncpy(new_reservation->username, username, MAX_USERNAME_LENGTH - 1);
    new_reservation->username[MAX_USERNAME_LENGTH - 1] = '\0';
    strncpy(new_reservation->reason, reason, MAX_REASON_LEN - 1);
    new_reservation->reason[MAX_REASON_LEN - 1] = '\0';
    new_reservation->start_time = start_time;
    new_reservation->end_time = end_time;
    new_reservation->status = RESERVATION_APPROVED;
    new_reservation->created_at = time(NULL);
    new_reservation->time_wheel_node = NULL;

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", new_reservation->id);

    if (utils_hashtable_insert(manager->reservation_map, id_str, new_reservation)) {
        LOG_INFO("Reservation", "예약 생성 성공: ID=%u, 장비=%s, 사용자=%s, 종료시간=%ld", 
                 reservation_id, device_id, username, end_time);
        
        // 만료 처리를 위해 타임휠에 추가
        time_wheel_add(manager->time_wheel, new_reservation);
        
        // 해당 장비의 상태를 '예약됨'으로 변경
        if (global_resource_manager) {
            resource_update_device_status(global_resource_manager, device_id, DEVICE_RESERVED, reservation_id);
        }
        
        manager->reservation_count++;
        pthread_mutex_unlock(&manager->mutex);
        return reservation_id; 
    } else {
        free(new_reservation);
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_HASHTABLE_INSERT_FAILED, "Reservation", "예약 해시 테이블 추가 실패");
        return 0;
    }
}

/**
 * @brief 예약을 취소합니다. (소프트 삭제)
 * @details
 *  - 실제 메모리 해제나 해시 테이블/타임휠에서 즉시 제거하지 않습니다.
 *  - 예약의 상태를 `RESERVATION_CANCELLED`로 변경하기만 합니다. (소프트 삭제)
 *  - 실제 정리는 `time_wheel_tick`에서 일괄적으로 처리됩니다.
 *  - 해당 장비의 상태는 즉시 '사용 가능'으로 변경됩니다.
 * @param manager 예약 관리자 포인터
 * @param reservation_id 취소할 예약 ID
 * @param username 예약을 시도하는 사용자 이름 (권한 확인용)
 * @return 성공 시 true, 실패 시 false
 */
bool reservation_cancel(reservation_manager_t* manager, uint32_t reservation_id,
                       const char* username) {
    if (!manager || !username) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Reservation", "잘못된 파라미터");
        return false;
    }

    pthread_mutex_lock(&manager->mutex);

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", reservation_id);
    reservation_t* reservation = (reservation_t*)utils_hashtable_get(manager->reservation_map, id_str);

    // 예약이 없거나, 사용자 이름이 다르거나, 이미 처리된 예약인 경우
    if (!reservation || strcmp(reservation->username, username) != 0 || reservation->status != RESERVATION_APPROVED) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 1. 상태를 '취소됨'으로 변경 (소프트 삭제)
    LOG_INFO("Reservation", "예약 취소 처리(소프트 삭제): ID=%u, 사용자=%s, 장비=%s",
             reservation_id, username, reservation->device_id);
    reservation->status = RESERVATION_CANCELLED;

    // 2. 장비 상태를 즉시 '사용 가능'으로 업데이트
    if (global_resource_manager) {
        resource_update_device_status(global_resource_manager, reservation->device_id, DEVICE_AVAILABLE, 0);
    }
    
    // 3. 실제 메모리 정리는 백그라운드 스레드의 time_wheel_tick에서 처리

    pthread_mutex_unlock(&manager->mutex);
    return true;
}