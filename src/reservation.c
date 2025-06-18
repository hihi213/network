#include "../include/reservation.h"
#include "../include/resource.h"


// 만료 예약 정리 스레드 관련 변수
static pthread_t cleanup_thread;
static bool cleanup_thread_running = false;
static ReservationManager* global_manager = NULL;
static ResourceManager* global_resource_manager = NULL;
static void* cleanup_thread_function(void* arg);


// [개선] 해시 테이블은 포인터만 저장하므로, 값을 해제하는 함수는 필요 없음
static void null_free_func(void* data) { (void)data; }
/* 예약 매니저 초기화 */
/**
 * @brief 예약 관리자를 초기화하고 만료 예약 정리 스레드를 시작합니다.
 * @param res_manager 리소스 매니저의 포인터.
 * @return 성공 시 초기화된 ReservationManager 포인터, 실패 시 NULL.
 */

ReservationManager* init_reservation_manager(ResourceManager* res_manager) {
    ReservationManager* manager = (ReservationManager*)malloc(sizeof(ReservationManager));
    if (!manager) {
        LOG_ERROR("Reservation", "예약 관리자 메모리 할당 실패");
        return NULL;
    }

    manager->reservation_count = 0;
    manager->next_reservation_id = 1;

    // 예약 ID를 키로 빠른 조회를 위한 해시 테이블 생성
    manager->reservation_map = ht_create(MAX_RESERVATIONS, null_free_func);
    if (!manager->reservation_map) {
        LOG_ERROR("Reservation", "예약 해시 테이블 생성 실패");
        free(manager);
        return NULL;
    }

    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Reservation", "뮤텍스 초기화 실패");
        ht_destroy(manager->reservation_map);
        free(manager);
        return NULL;
    }

    // 만료 예약 정리를 위한 백그라운드 스레드 시작
    global_manager = manager;
    global_resource_manager = res_manager;
    cleanup_thread_running = true;

    if (pthread_create(&cleanup_thread, NULL, cleanup_thread_function, NULL) != 0) {
        LOG_ERROR("Reservation", "만료 예약 정리 스레드 생성 실패");
        cleanup_thread_running = false;
        global_manager = NULL;
        ht_destroy(manager->reservation_map);
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    LOG_INFO("Reservation", "예약 관리자 초기화 완료");
    return manager;
}
/**
 * @brief 특정 장비에 대해 현재 활성화된 예약을 찾습니다.
 * @param manager 예약 관리자 포인터.
 * @param device_id 검색할 장비의 ID.
 * @return 활성화된 예약이 있으면 Reservation 포인터를, 없으면 NULL을 반환합니다.
 */
Reservation* get_active_reservation_for_device(ReservationManager* manager, const char* device_id) {
    if (!manager || !device_id) {
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);
    
    time_t now = time(NULL);
    // reservation_map이 아닌 실제 데이터가 저장된 배열을 순회해야 합니다.
    for (int i = 0; i < manager->reservation_count; i++) {
        Reservation* r = &manager->reservations[i];

        // 장비 ID가 일치하는지 확인
        if (strcmp(r->device_id, device_id) == 0) {
            
            // 상태가 '승인'이고, 현재 시간이 예약 시간 범위 안에 있는지 확인
            if (r->status == RESERVATION_APPROVED && now >= r->start_time && now < r->end_time) {
                pthread_mutex_unlock(&manager->mutex);
                return r; // 활성화된 예약을 찾았으므로 즉시 반환
            }
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return NULL; // 활성화된 예약 없음
}

/* 만료 예약 정리 스레드 함수 (개선됨) */
static void* cleanup_thread_function(void* arg) {
    (void)arg;
    while (cleanup_thread_running) {
        if (global_manager && global_resource_manager) {
            // [개선] 중첩 루프가 제거된 효율적인 함수 호출
            cleanup_expired_reservations(global_manager, global_resource_manager);
        }
        // [개선] 폴링 간격을 늘려 CPU 부담 감소.
        // 이상적으로는 다음 만료 시간까지 대기해야 하지만, 5초 폴링도 큰 개선임.
        sleep(5); 
    }
    return NULL;
}


/**
 * @brief 예약 관리자의 리소스를 정리하고 백그라운드 스레드를 종료합니다.
 * @param manager 정리할 ReservationManager 포인터.
 */
void cleanup_reservation_manager(ReservationManager* manager) {
    if (!manager) return;

    // 만료 예약 정리 스레드 종료
    if (cleanup_thread_running) {
        cleanup_thread_running = false;
        pthread_join(cleanup_thread, NULL); // 스레드가 완전히 종료될 때까지 대기
        global_manager = NULL;
    }

    // [개선] 예약 조회를 위해 사용된 해시 테이블의 리소스를 해제합니다.
    ht_destroy(manager->reservation_map);

    pthread_mutex_destroy(&manager->mutex);
    free(manager);
    LOG_INFO("Reservation", "예약 관리자 정리 완료");
}

/**
 * @brief 새로운 예약을 생성합니다.
 * @param manager 예약 관리자 포인터.
 * @param device_id 예약할 장비의 ID.
 * @param username 예약을 요청한 사용자의 이름.
 * @param start_time 예약 시작 시간.
 * @param end_time 예약 종료 시간.
 * @param reason 예약 사유.
 * @return 성공 시 true, 실패 시 false.
 */
bool create_reservation(ReservationManager* manager, const char* device_id,
                          const char* username, time_t start_time,
                          time_t end_time, const char* reason) {
    if (!manager || !device_id || !username || !reason) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return false;
    }

    LOG_INFO("Reservation", "예약 생성 시작: 장치=%s, 사용자=%s", device_id, username);

    pthread_mutex_lock(&manager->mutex);

    if (manager->reservation_count >= MAX_RESERVATIONS) {
        LOG_ERROR("Reservation", "예약 최대 개수 초과");
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 시간 유효성 검사
    time_t current_time = time(NULL);
    if (start_time < current_time || end_time <= start_time) {
        LOG_ERROR("Reservation", "잘못된 예약 시간");
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 예약 중복 검사 (특정 장비에 대해 시간이 겹치는지 확인)
    for (int i = 0; i < manager->reservation_count; i++) {
        // 배열에 저장된 실제 예약 객체들을 확인합니다.
        if (strcmp(manager->reservations[i].device_id, device_id) == 0 &&
            manager->reservations[i].status == RESERVATION_APPROVED) {
            // 시간 겹침 검사
            if (!(end_time <= manager->reservations[i].start_time ||
                  start_time >= manager->reservations[i].end_time)) {
                LOG_ERROR("Reservation", "해당 장비는 요청된 시간에 이미 예약이 존재합니다.");
                pthread_mutex_unlock(&manager->mutex);
                return false;
            }
        }
    }

    // 예약 ID 생성
    uint32_t reservation_id = manager->next_reservation_id++;
    
    // 예약 정보 저장 (배열의 다음 빈 공간에 저장)
    Reservation* new_reservation = &manager->reservations[manager->reservation_count];
    new_reservation->id = reservation_id;
    strncpy(new_reservation->device_id, device_id, MAX_DEVICE_ID_LEN - 1);
    new_reservation->device_id[MAX_DEVICE_ID_LEN - 1] = '\0';
    strncpy(new_reservation->username, username, MAX_USERNAME_LENGTH - 1);
    new_reservation->username[MAX_USERNAME_LENGTH - 1] = '\0';
    new_reservation->start_time = start_time;
    new_reservation->end_time = end_time;
    strncpy(new_reservation->reason, reason, MAX_REASON_LEN - 1);
    new_reservation->reason[MAX_REASON_LEN - 1] = '\0';
    new_reservation->status = RESERVATION_APPROVED;
    new_reservation->created_at = time(NULL);

    // [개선] 해시 테이블에 예약 포인터를 추가하여 빠른 조회를 가능하게 합니다.
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", new_reservation->id);
    ht_insert(manager->reservation_map, id_str, new_reservation);

    manager->reservation_count++;

    pthread_mutex_unlock(&manager->mutex);
    LOG_INFO("Reservation", "예약 생성 성공: ID=%u", reservation_id);
    return true;
}

/**
 * @brief 기존 예약을 취소합니다.
 * @param manager 예약 관리자 포인터.
 * @param reservation_id 취소할 예약의 ID.
 * @param username 예약을 취소하려는 사용자의 이름 (권한 확인용).
 * @return 성공 시 true, 실패 시 false.
 */
bool cancel_reservation(ReservationManager* manager, uint32_t reservation_id,
                       const char* username) {
    if (!manager || !username) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return false;
    }

    LOG_INFO("Reservation", "예약 취소 시작: ID=%u, 사용자=%s", reservation_id, username);

    pthread_mutex_lock(&manager->mutex);

    // [개선] 해시 테이블에서 예약 ID를 통해 O(1) 시간 복잡도로 예약을 즉시 검색합니다.
    char id_str[16];
    snprintf(id_str, sizeof(id_str), "%u", reservation_id);
    Reservation* reservation = (Reservation*)ht_get(manager->reservation_map, id_str);

    if (!reservation) {
        LOG_ERROR("Reservation", "예약을 찾을 수 없음: ID=%u", reservation_id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 권한 검사
    if (strcmp(reservation->username, username) != 0) {
        LOG_ERROR("Reservation", "예약 취소 권한 없음: ID=%u, 사용자=%s", reservation_id, username);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 예약 상태 검사
    if (reservation->status != RESERVATION_APPROVED) {
        LOG_ERROR("Reservation", "이미 처리되었거나 취소된 예약입니다: ID=%u", reservation_id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 예약 상태를 '취소됨'으로 변경
    reservation->status = RESERVATION_CANCELLED;

    // [개선] 취소된 예약은 더 이상 조회될 필요가 없으므로 해시 테이블에서 제거합니다.
    ht_delete(manager->reservation_map, id_str);

    LOG_INFO("Reservation", "예약 취소 성공: ID=%u", reservation_id);
    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/* 만료 예약 정리 (개선됨) */
void cleanup_expired_reservations(ReservationManager* manager, ResourceManager* res_manager) {
    if (!manager || !res_manager) return;

    time_t current_time = time(NULL);
    char expired_device_ids[MAX_RESERVATIONS][MAX_DEVICE_ID_LEN];
    int expired_count = 0;

    // [개선] 1. 만료된 예약을 찾아 장비 ID를 수집하고, 바로 해시 테이블에서 제거
    pthread_mutex_lock(&manager->mutex);
    for (int i = 0; i < manager->reservation_count; i++) {
        Reservation* r = &manager->reservations[i];
        if (r->status == RESERVATION_APPROVED && r->end_time < current_time) {
            
            r->status = RESERVATION_COMPLETED;
            
            strncpy(expired_device_ids[expired_count], r->device_id, MAX_DEVICE_ID_LEN - 1);
            expired_count++;
            
            char id_str[16];
            snprintf(id_str, sizeof(id_str), "%u", r->id);
            ht_delete(manager->reservation_map, id_str); // 즉시 맵에서 제거
            
            LOG_INFO("Reservation", "예약 만료 감지: 장비 ID=%s", r->device_id);
        }
    }
    // 참고: 배열 자체를 정리(압축)하는 로직은 복잡도를 높이므로 여기서는 생략.
    // 카운트 기반 시스템에서는 상태 플래그로 비활성화를 표시하는 것이 더 간단할 수 있습니다.
    pthread_mutex_unlock(&manager->mutex);

    // [개선] 2. 수집된 ID의 장비 상태만 한 번에 업데이트 (중첩 루프 없음)
    if (expired_count > 0) {
        for (int i = 0; i < expired_count; i++) {
            // 이 함수는 내부적으로 해시 테이블을 사용하므로 빠릅니다.
            update_device_status(res_manager, expired_device_ids[i], DEVICE_AVAILABLE);
            LOG_INFO("Reservation", "장비 반납 처리 완료: 장비 ID=%s", expired_device_ids[i]);
        }
        LOG_INFO("Reservation", "만료된 예약 정리 완료: 총 %d개 정리됨", expired_count);
    }
}