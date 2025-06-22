/**
 * @file reservation.c
 * @brief 예약 관리 모듈 - 장비 예약 시스템의 핵심 기능
 * @details 예약 생성, 취소, 만료 처리 및 백그라운드 정리 스레드를 담당합니다.
 */

#include "../include/reservation.h"
#include "../include/resource.h"
#include "../include/message.h"

static pthread_t cleanup_thread;
static bool cleanup_thread_running = false;
static reservation_manager_t* global_manager = NULL;
static resource_manager_t* global_resource_manager = NULL;
static void* reservation_cleanup_thread_function(void* arg);

// [추가] 해시 테이블 순회를 위한 콜백 데이터 구조
typedef struct {
    const char* device_id;
    time_t start_time;
    time_t end_time;
    bool has_conflict;
} conflict_check_data_t;

// [추가] 예약 충돌 검사를 위한 콜백 함수
static void reservation_conflict_check_callback(const char* key, void* value, void* user_data) {
    (void)key; // 미사용 매개변수
    reservation_t* reservation = (reservation_t*)value;
    conflict_check_data_t* data = (conflict_check_data_t*)user_data;
    
    if (strcmp(reservation->device_id, data->device_id) == 0 &&
        reservation->status == RESERVATION_APPROVED) {
        if (!(data->end_time <= reservation->start_time ||
              data->start_time >= reservation->end_time)) {
            data->has_conflict = true;
        }
    }
}

// [추가] 타임휠 관련 함수들
static time_wheel_t* time_wheel_init(void) {
    time_wheel_t* wheel = malloc(sizeof(time_wheel_t));
    if (!wheel) return NULL;

    wheel->size = TIME_WHEEL_SIZE;
    wheel->current_index = 0;
    wheel->base_time = time(NULL);
    for (int i = 0; i < wheel->size; i++) {
        wheel->buckets[i] = NULL;
    }
    pthread_mutex_init(&wheel->mutex, NULL);
    return wheel;
}

static void time_wheel_add(time_wheel_t* wheel, reservation_t* res) {
    if (!wheel || !res) {
        LOG_WARNING("TimeWheel", "타임휠 추가 실패: 예약ID=%u, 노드포인터=%p", 
                   res ? res->id : 0, res ? res->time_wheel_node : NULL);
        return;
    }

    // [수정] 현재 시간을 기준으로 '앞으로 남은 시간'을 계산합니다.
    time_t now = time(NULL);
    if (res->end_time <= now) {
        LOG_WARNING("TimeWheel", "타임휠 추가 실패: 예약 종료 시간이 이미 지났습니다. 예약ID=%u, 종료시간=%ld, 현재시간=%ld", 
                   res->id, res->end_time, now);
        return; // 이미 만료된 예약은 추가하지 않음
    }
    long remaining_seconds = res->end_time - now;

    time_wheel_node_t* node = malloc(sizeof(time_wheel_node_t));
    if (!node) {
        LOG_ERROR("TimeWheel", "타임휠 노드 메모리 할당 실패: 예약ID=%u", res->id);
        return;
    }

    node->reservation = res;
    res->time_wheel_node = node; // 역방향 포인터 설정

    // [수정] 남은 시간을 기준으로 cycle과 bucket_index를 계산합니다.
    node->cycle = remaining_seconds / wheel->size;
    int bucket_index = (wheel->current_index + (remaining_seconds % wheel->size)) % wheel->size;
    
    LOG_INFO("TimeWheel", "타임휠 추가: 예약ID=%u, 장비=%s, 남은시간=%ld초, cycle=%d, 버킷=%d", 
             res->id, res->device_id, remaining_seconds, node->cycle, bucket_index);

    pthread_mutex_lock(&wheel->mutex);
    node->next = wheel->buckets[bucket_index];
    wheel->buckets[bucket_index] = node;
    pthread_mutex_unlock(&wheel->mutex);
}

static void time_wheel_tick(reservation_manager_t* manager) {
    time_wheel_t* wheel = manager->time_wheel;
    
    // 락 외부에서 처리할 노드 리스트 분리
    pthread_mutex_lock(&wheel->mutex);
    wheel->current_index = (wheel->current_index + 1) % wheel->size;
    time_wheel_node_t* node = wheel->buckets[wheel->current_index];
    wheel->buckets[wheel->current_index] = NULL;
    pthread_mutex_unlock(&wheel->mutex);

    int node_count = 0;
    time_wheel_node_t* temp = node;
    while (temp) {
        node_count++;
        temp = temp->next;
    }
    
    if (node_count > 0) {
        LOG_INFO("TimeWheel", "현재 버킷[%d]에서 %d개 노드 처리 시작 (현재시간: %ld)", 
                 wheel->current_index, node_count, time(NULL));
    }

    // 만료 처리 (이제 락 외부에서 수행)
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
            should_free_node = true;
        } else if (live_res->status == RESERVATION_CANCELLED) {
            LOG_INFO("TimeWheel", "취소된 예약 정리: 예약ID=%u", live_res->id);
            utils_hashtable_delete(manager->reservation_map, id_str);
            should_free_node = true;
            expired_count++;
        } else if (live_res->status == RESERVATION_APPROVED) {
            if (node->cycle == 0) {
                time_t current_time = time(NULL);
                if (live_res->end_time <= current_time) {
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
                node->cycle--;
                pthread_mutex_lock(&wheel->mutex);
                node->next = wheel->buckets[wheel->current_index];
                wheel->buckets[wheel->current_index] = node;
                pthread_mutex_unlock(&wheel->mutex);
            } else {
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
        LOG_INFO("TimeWheel", "버킷[%d] 처리 완료: 총%d개, 만료%d개, 재사용%d개", 
                 wheel->current_index, processed_count, expired_count, recycled_count);
    }
    
    if (updated && manager->broadcast_callback) {
        LOG_INFO("TimeWheel", "상태 변경 감지, 브로드캐스트 콜백 호출");
        manager->broadcast_callback();
    }
}

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

// [수정] 기존 cleanup 스레드 함수를 타임휠 방식으로 변경
static void* reservation_cleanup_thread_function(void* arg) {
    reservation_manager_t* manager = (reservation_manager_t*)arg;
    while (cleanup_thread_running) {
        sleep(1);
        if (manager) {
            time_wheel_tick(manager); // 1초마다 tick 함수 호출
        }
    }
    return NULL;
}

// [삭제] 기존 순회 방식 관련 구조체와 함수들
// struct cleanup_ctx와 collect_expired_callback 함수는 더 이상 필요하지 않음

reservation_manager_t* reservation_init_manager(resource_manager_t* res_manager, void (*callback)(void)) {
    reservation_manager_t* manager = (reservation_manager_t*)malloc(sizeof(reservation_manager_t));
    if (!manager) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Reservation", "예약 관리자 메모리 할당 실패");
        return NULL;
    }

    // [개선] 공통 초기화 헬퍼 함수 사용
    if (!utils_init_manager_base(manager, sizeof(reservation_manager_t), &manager->reservation_map, MAX_RESERVATIONS, free, &manager->mutex)) {
        utils_report_error(ERROR_HASHTABLE_CREATION_FAILED, "Reservation", "매니저 공통 초기화 실패");
        free(manager);
        return NULL;
    }

    // 예약 매니저 특화 초기화
    manager->reservation_count = 0;
    manager->next_reservation_id = 1;
    manager->broadcast_callback = callback;

    // [추가] 타임휠 초기화
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

    // LOG_INFO("Reservation", "예약 관리자 및 타임휠 초기화 성공");
    return manager;
}

reservation_t* reservation_get_active_for_device(reservation_manager_t* resv_manager, resource_manager_t* rsrc_manager, const char* device_id) {
    if (!resv_manager || !rsrc_manager || !device_id) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Reservation", "reservation_get_active_for_device: 잘못된 파라미터");
        return NULL;
    }

    // LOG_INFO("Reservation", "장비 활성 예약 조회 시작: 장비ID=%s", device_id);

    device_t* device = (device_t*)utils_hashtable_get(rsrc_manager->devices, device_id);
    if (!device) {
        // LOG_WARNING("Reservation", "장비를 찾을 수 없음: ID=%s", device_id);
        return NULL;
    }

    // LOG_INFO("Reservation", "장비 정보 조회: ID=%s, 상태=%s, 활성예약ID=%u", 
    //          device_id, message_get_device_status_string(device->status), device->active_reservation_id);

    if (device->active_reservation_id == 0) {
        // LOG_INFO("Reservation", "장비에 활성 예약이 없음: ID=%s", device_id);
        return NULL;
    }

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", device->active_reservation_id);
    
    pthread_mutex_lock(&resv_manager->mutex);
    reservation_t* reservation = (reservation_t*)utils_hashtable_get(resv_manager->reservation_map, id_str);
    pthread_mutex_unlock(&resv_manager->mutex);

    if (reservation && reservation->status == RESERVATION_APPROVED) {
        // LOG_INFO("Reservation", "활성 예약 발견: 장비ID=%s, 예약ID=%u, 사용자=%s, 종료시간=%ld", 
        //          device_id, reservation->id, reservation->username, reservation->end_time);
        return reservation;
    } else if (reservation) {
        // LOG_WARNING("Reservation", "예약은 존재하지만 승인되지 않음: 장비ID=%s, 예약ID=%u, 상태=%d", 
        //             device_id, reservation->id, reservation->status);
    } else {
        // LOG_WARNING("Reservation", "예약을 찾을 수 없음: 장비ID=%s, 예약ID=%u", device_id, device->active_reservation_id);
    }

    return NULL;
}

void reservation_cleanup_manager(reservation_manager_t* manager) {
    if (!manager) return;

    if (cleanup_thread_running) {
        cleanup_thread_running = false;
        pthread_join(cleanup_thread, NULL);
        global_manager = NULL;
    }

    // [추가] 타임휠 정리
    if (manager->time_wheel) {
        time_wheel_cleanup(manager->time_wheel);
    }
    
    if (manager->reservation_map) {
        utils_hashtable_destroy(manager->reservation_map);
    }
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
    // LOG_INFO("Reservation", "예약 관리자 및 타임휠 정리 완료");
}

uint32_t reservation_create(reservation_manager_t* manager, const char* device_id,
                            const char* username, time_t start_time,
                            time_t end_time, const char* reason) {
    if (!manager || !device_id || !username || !reason) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Reservation", "잘못된 파라미터");
        return 0;
    }

    // LOG_INFO("Reservation", "예약 생성 시작: 장치=%s, 사용자=%s", device_id, username);
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

    // [개선] 해시 테이블 순회를 통한 충돌 검사
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
    
    // [개선] 동적 메모리 할당으로 예약 객체 생성
    reservation_t* new_reservation = (reservation_t*)malloc(sizeof(reservation_t));
    if (!new_reservation) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Reservation", "예약 객체 메모리 할당 실패");
        return 0;
    }
    
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
    new_reservation->time_wheel_node = NULL; // [추가] 타임휠 노드 포인터 초기화

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", new_reservation->id);

    // [수정] 해시 테이블에 추가 성공 시 타임휠에도 추가
    if (utils_hashtable_insert(manager->reservation_map, id_str, new_reservation)) {
        LOG_INFO("Reservation", "예약 생성 성공: ID=%u, 장비=%s, 사용자=%s, 종료시간=%ld", 
                 reservation_id, device_id, username, end_time);
        time_wheel_add(manager->time_wheel, new_reservation); // [추가] 타임휠에 추가
        
        // [추가] 장비 상태를 RESERVED로 변경하고 active_reservation_id 설정
        if (global_resource_manager) {
            resource_update_device_status(global_resource_manager, device_id, DEVICE_RESERVED, reservation_id);
            LOG_INFO("Reservation", "장비 상태 업데이트 완료: 장비=%s, 상태=AVAILABLE->RESERVED, 예약ID=%u", 
                     device_id, reservation_id);
        }
        
        manager->reservation_count++;
        pthread_mutex_unlock(&manager->mutex);
        // LOG_INFO("Reservation", "예약 생성 성공: ID=%u", reservation_id);
        return reservation_id; 
    } else {
        // [수정] 해시 테이블 추가 실패 시 메모리 해제
        free(new_reservation);
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_HASHTABLE_INSERT_FAILED, "Reservation", "예약 해시 테이블 추가 실패");
        return 0;
    }
}

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

    if (!reservation || strcmp(reservation->username, username) != 0 || reservation->status != RESERVATION_APPROVED) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // --- 수정된 로직 ---
    // 1. 상태를 '취소됨'으로 변경 (메모리는 해제하지 않음)
    LOG_INFO("Reservation", "예약 취소 처리 (상태 변경): ID=%u, 사용자=%s, 장비=%s",
             reservation_id, username, reservation->device_id);
    reservation->status = RESERVATION_CANCELLED;

    // 2. 장비 상태를 '사용 가능'으로 업데이트 (역할 명확화)
    if (global_resource_manager) {
        resource_update_device_status(global_resource_manager, reservation->device_id, DEVICE_AVAILABLE, 0);
    }
    
    // 3. time_wheel_remove와 hashtable_delete 호출 제거
    //    정리는 time_wheel_tick 에서 일괄 처리합니다.

    pthread_mutex_unlock(&manager->mutex);
    return true;
}