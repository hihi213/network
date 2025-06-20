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

// [추가] 만료된 예약을 찾아서 장비 상태를 업데이트하는 콜백 함수
static void reservation_expired_cleanup_callback(const char* key, void* value, void* user_data) {
    (void)key; // 미사용 매개변수
    reservation_t* reservation = (reservation_t*)value;
    time_t current_time = *(time_t*)user_data;
    
    if (reservation->status == RESERVATION_APPROVED && reservation->end_time < current_time) {
        // 만료된 예약 발견 - 장비 상태를 available로 변경
        resource_update_device_status(global_resource_manager, reservation->device_id, DEVICE_AVAILABLE, 0);
        
        // 예약을 완료 상태로 변경
        reservation->status = RESERVATION_COMPLETED;
        
        LOG_INFO("Reservation", "만료된 예약 발견: 장비ID=%s, 예약ID=%u, 사용자=%s", 
                 reservation->device_id, reservation->id, reservation->username);
    }
}

// [추가] 완료된 예약을 해시 테이블에서 제거하는 콜백 함수
static void reservation_remove_completed_callback(const char* key, void* value, void* user_data) {
    reservation_t* reservation = (reservation_t*)value;
    reservation_manager_t* manager = (reservation_manager_t*)user_data;
    
    if (reservation->status == RESERVATION_COMPLETED) {
        // 예약 삭제
        utils_hashtable_delete(manager->reservation_map, key);
        manager->reservation_count--;
        
        LOG_INFO("Reservation", "완료된 예약 삭제: 예약ID=%u", reservation->id);
    }
}

struct cleanup_ctx {
    time_t now;
    char (*keys)[16];
    int* count;
    reservation_manager_t* manager;
    resource_manager_t* res_manager;
};

static void collect_expired_callback(const char* key, void* value, void* user_data) {
    struct cleanup_ctx* ctx = (struct cleanup_ctx*)user_data;
    reservation_t* reservation = (reservation_t*)value;
    if (reservation->status == RESERVATION_APPROVED && reservation->end_time < ctx->now) {
        // 장비 상태 available로 변경
        resource_update_device_status(ctx->res_manager, reservation->device_id, DEVICE_AVAILABLE, 0);
        reservation->status = RESERVATION_COMPLETED;
        if (*(ctx->count) < MAX_RESERVATIONS) {
            strncpy(ctx->keys[*(ctx->count)], key, 15);
            ctx->keys[*(ctx->count)][15] = '\0';
            (*(ctx->count))++;
        }
    }
}

reservation_manager_t* reservation_init_manager(resource_manager_t* res_manager, void (*callback)(void)) {
    reservation_manager_t* manager = (reservation_manager_t*)malloc(sizeof(reservation_manager_t));
    if (!manager) {
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Reservation", "예약 관리자 메모리 할당 실패");
        return NULL;
    }

    manager->reservation_count = 0;
    manager->next_reservation_id = 1;
    manager->broadcast_callback = callback;
    manager->reservation_map = utils_hashtable_create(MAX_RESERVATIONS, free);
    if (!manager->reservation_map) {
        utils_report_error(ERROR_HASHTABLE_CREATION_FAILED, "Reservation", "예약 해시 테이블 생성 실패");
        free(manager);
        return NULL;
    }

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        utils_report_error(ERROR_INVALID_STATE, "Reservation", "뮤텍스 초기화 실패");
        utils_hashtable_destroy(manager->reservation_map);
        free(manager);
        return NULL;
    }

    global_manager = manager;
    global_resource_manager = res_manager;
    cleanup_thread_running = true;

    if (pthread_create(&cleanup_thread, NULL, reservation_cleanup_thread_function, NULL) != 0) {
        utils_report_error(ERROR_INVALID_STATE, "Reservation", "만료 예약 정리 스레드 생성 실패");
        cleanup_thread_running = false;
        global_manager = NULL;
        utils_hashtable_destroy(manager->reservation_map);
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    // LOG_INFO("Reservation", "예약 관리자 초기화 성공");
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

static void* reservation_cleanup_thread_function(void* arg) {
    (void)arg;
    while (cleanup_thread_running) {
        if (global_manager && global_resource_manager) {
            reservation_cleanup_expired(global_manager, global_resource_manager);
        }
        sleep(1);
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

    if (manager->reservation_map) {
        utils_hashtable_destroy(manager->reservation_map);
    }
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
    // LOG_INFO("Reservation", "예약 관리자 정리 완료");
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

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", new_reservation->id);
    utils_hashtable_insert(manager->reservation_map, id_str, new_reservation);

    manager->reservation_count++;

    pthread_mutex_unlock(&manager->mutex);
    // LOG_INFO("Reservation", "예약 생성 성공: ID=%u", reservation_id);
    return reservation_id; 
}

bool reservation_cancel(reservation_manager_t* manager, uint32_t reservation_id,
                       const char* username) {
    if (!manager || !username) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Reservation", "잘못된 파라미터");
        return false;
    }

    // LOG_INFO("Reservation", "예약 취소 시작: ID=%u, 사용자=%s", reservation_id, username);
    pthread_mutex_lock(&manager->mutex);

    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", reservation_id);
    reservation_t* reservation = (reservation_t*)utils_hashtable_get(manager->reservation_map, id_str);

    if (!reservation) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_RESERVATION_NOT_FOUND, "Reservation", "예약을 찾을 수 없음: ID=%u", reservation_id);
        return false;
    }

    if (strcmp(reservation->username, username) != 0) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_RESERVATION_PERMISSION_DENIED, "Reservation", "예약 취소 권한 없음: ID=%u, 사용자=%s", reservation_id, username);
        return false;
    }

    if (reservation->status != RESERVATION_APPROVED) {
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_RESERVATION_CANCELLATION_FAILED, "Reservation", "이미 처리되었거나 취소된 예약입니다: ID=%u", reservation_id);
        return false;
    }

    reservation->status = RESERVATION_CANCELLED;
    utils_hashtable_delete(manager->reservation_map, id_str);

    // LOG_INFO("Reservation", "예약 취소 성공: ID=%u", reservation_id);
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

void reservation_cleanup_expired(reservation_manager_t* manager, resource_manager_t* res_manager) {
    if (!manager || !res_manager) return;

    time_t current_time = time(NULL);
    char to_delete_keys[MAX_RESERVATIONS][16];
    int delete_count = 0;

    pthread_mutex_lock(&manager->mutex);

    // 1차 순회: 만료된 예약의 키(문자열)만 수집
    struct cleanup_ctx ctx = { current_time, to_delete_keys, &delete_count, manager, res_manager };
    utils_hashtable_traverse(manager->reservation_map, collect_expired_callback, &ctx);

    // 2차 루프: 수집된 키만 삭제
    for (int i = 0; i < delete_count; ++i) {
        utils_hashtable_delete(manager->reservation_map, to_delete_keys[i]);
        manager->reservation_count--;
    }

    pthread_mutex_unlock(&manager->mutex);

    if (delete_count > 0 && manager->broadcast_callback) {
        manager->broadcast_callback();
    }
}