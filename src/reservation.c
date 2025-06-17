#include "../include/reservation.h"
#include "../include/logger.h"

// 만료 예약 정리 스레드 관련 변수
static pthread_t cleanup_thread;
static bool cleanup_thread_running = false;
static ReservationManager* global_manager = NULL;

// 만료 예약 정리 스레드 함수
static void* cleanup_thread_function(void* arg) {
    (void)arg;  // 사용되지 않는 매개변수 경고 제거
    while (cleanup_thread_running) {
        if (global_manager) {
            cleanup_expired_reservations(global_manager);
        }
        sleep(60); // 1분마다 실행
    }
    return NULL;
}

/* 예약 매니저 초기화 */
ReservationManager* init_reservation_manager(void) {
    ReservationManager* manager = (ReservationManager*)malloc(sizeof(ReservationManager));
    if (!manager) {
        LOG_ERROR("Reservation", "예약 관리자 메모리 할당 실패");
        return NULL;
    }

    manager->reservation_count = 0;
    manager->next_reservation_id = 1;
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Reservation", "뮤텍스 초기화 실패");
        free(manager);
        return NULL;
    }

    // 만료 예약 정리 스레드 시작
    global_manager = manager;
    cleanup_thread_running = true;
    if (pthread_create(&cleanup_thread, NULL, cleanup_thread_function, NULL) != 0) {
        LOG_ERROR("Reservation", "만료 예약 정리 스레드 생성 실패");
        cleanup_thread_running = false;
        global_manager = NULL;
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }

    LOG_INFO("Reservation", "예약 관리자 초기화 완료");
    return manager;
}

/* 예약 매니저 정리 */
void cleanup_reservation_manager(ReservationManager* manager) {
    if (!manager) return;

    // 만료 예약 정리 스레드 종료
    if (cleanup_thread_running) {
        cleanup_thread_running = false;
        pthread_join(cleanup_thread, NULL);
        global_manager = NULL;
    }

    pthread_mutex_destroy(&manager->mutex);
    free(manager);
    LOG_INFO("Reservation", "예약 관리자 정리 완료");
}

/* 예약 생성 */
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

    // 예약 중복 검사
    for (int i = 0; i < manager->reservation_count; i++) {
        if (strcmp(manager->reservations[i].device_id, device_id) == 0) {
            // 시간 겹침 검사
            if (!(end_time <= manager->reservations[i].start_time ||
                  start_time >= manager->reservations[i].end_time)) {
                LOG_ERROR("Reservation", "예약 시간이 겹침");
                pthread_mutex_unlock(&manager->mutex);
                return false;
            }
        }
    }

    // 예약 ID 생성
    uint32_t reservation_id = ++manager->next_reservation_id;
    
    // 예약 정보 저장
    Reservation* reservation = &manager->reservations[manager->reservation_count];
    reservation->id = reservation_id;
    strncpy(reservation->device_id, device_id, MAX_DEVICE_ID_LEN - 1);
    reservation->device_id[MAX_DEVICE_ID_LEN - 1] = '\0';
    strncpy(reservation->username, username, MAX_USERNAME_LENGTH - 1);
    reservation->username[MAX_USERNAME_LENGTH - 1] = '\0';
    reservation->start_time = start_time;
    reservation->end_time = end_time;
    strncpy(reservation->reason, reason, MAX_REASON_LEN - 1);
    reservation->reason[MAX_REASON_LEN - 1] = '\0';
    reservation->status = RESERVATION_APPROVED;
    reservation->created_at = time(NULL);

    manager->reservation_count++;

    pthread_mutex_unlock(&manager->mutex);
    LOG_INFO("Reservation", "예약 생성 성공: ID=%u", reservation_id);
    return true;
}

/* 예약 취소 */
bool cancel_reservation(ReservationManager* manager, uint32_t reservation_id,
                       const char* username) {
    if (!manager || !username) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return false;
    }

    LOG_INFO("Reservation", "예약 취소 시작: ID=%u, 사용자=%s", reservation_id, username);

    pthread_mutex_lock(&manager->mutex);

    Reservation* reservation = NULL;
    for (int i = 0; i < manager->reservation_count; i++) {
        if (manager->reservations[i].id == reservation_id) {
            reservation = &manager->reservations[i];
            break;
        }
    }

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
        LOG_ERROR("Reservation", "이미 취소된 예약: ID=%u", reservation_id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    reservation->status = RESERVATION_CANCELLED;
    LOG_INFO("Reservation", "예약 취소 성공: ID=%u", reservation_id);

    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/* 예약 정보 조회 */
Reservation* get_reservation(ReservationManager* manager, uint32_t reservation_id) {
    if (!manager) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);

    Reservation* reservation = NULL;
    for (int i = 0; i < manager->reservation_count; i++) {
        if (manager->reservations[i].id == reservation_id) {
            reservation = &manager->reservations[i];
            break;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return reservation;
}

/* 사용자의 예약 목록 조회 */
int get_user_reservations(ReservationManager* manager, const char* username,
                         Reservation* reservations, int max_reservations) {
    if (!manager || !username || !reservations) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    int count = 0;
    for (int i = 0; i < manager->reservation_count && count < max_reservations; i++) {
        if (strcmp(manager->reservations[i].username, username) == 0) {
            memcpy(&reservations[count], &manager->reservations[i], sizeof(Reservation));
            count++;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return count;
}

/* 장치의 예약 목록 조회 */
int get_device_reservations(ReservationManager* manager, const char* device_id,
                          Reservation* reservations, int max_reservations) {
    if (!manager || !device_id || !reservations) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);

    int count = 0;
    for (int i = 0; i < manager->reservation_count && count < max_reservations; i++) {
        if (strcmp(manager->reservations[i].device_id, device_id) == 0) {
            memcpy(&reservations[count], &manager->reservations[i], sizeof(Reservation));
            count++;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return count;
}

/* 예약 상태 확인 */
bool is_reservation_active(ReservationManager* manager, uint32_t reservation_id) {
    if (!manager) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return false;
    }

    LOG_INFO("Reservation", "예약 상태 확인: ID=%u", reservation_id);

    pthread_mutex_lock(&manager->mutex);

    bool active = false;
    for (int i = 0; i < manager->reservation_count; i++) {
        if (manager->reservations[i].id == reservation_id) {
            active = (manager->reservations[i].status == RESERVATION_APPROVED);
            break;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return active;
}

/* 예약 상태 문자열 변환 */
const char* get_reservation_status_string(ReservationStatus status) {
    switch (status) {
        case RESERVATION_APPROVED:
            return "Approved";
        case RESERVATION_CANCELLED:
            return "Cancelled";
        case RESERVATION_COMPLETED:
            return "Completed";
        default:
            return "Unknown";
    }
}

/* 만료된 예약 정리 */
void cleanup_expired_reservations(ReservationManager* manager) {
    if (!manager) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return;
    }

    LOG_INFO("Reservation", "만료된 예약 정리 시작");
    time_t current_time = time(NULL);
    int removed_count = 0;

    pthread_mutex_lock(&manager->mutex);

    for (int i = 0; i < manager->reservation_count; i++) {
        if (manager->reservations[i].status == RESERVATION_APPROVED &&
            manager->reservations[i].end_time < current_time) {
            manager->reservations[i].status = RESERVATION_COMPLETED;
            removed_count++;
        }
    }

    pthread_mutex_unlock(&manager->mutex);

    LOG_INFO("Reservation", "만료된 예약 정리 완료: %d개 정리됨", removed_count);
}

/* 장비 ID로 예약 조회 */
Reservation* get_reservation_by_device(ReservationManager* manager, const char* device_id) {
    if (!manager || !device_id) {
        LOG_ERROR("Reservation", "잘못된 파라미터");
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);

    for (int i = 0; i < manager->reservation_count; i++) {
        if (strcmp(manager->reservations[i].device_id, device_id) == 0) {
            Reservation* reservation = &manager->reservations[i];
            pthread_mutex_unlock(&manager->mutex);
            return reservation;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return NULL;
} 

Reservation* get_active_reservation_for_device(ReservationManager* manager, const char* device_id) {
    if (!manager || !device_id) return NULL;

    pthread_mutex_lock(&manager->mutex);
    
    time_t now = time(NULL);
    for (int i = 0; i < manager->reservation_count; i++) {
        Reservation* r = &manager->reservations[i];
        if (strcmp(r->device_id, device_id) == 0) {
            // [중요] 상태가 승인이고, 현재 시간이 예약 시간 범위 안에 있는지 확인
            if (r->status == RESERVATION_APPROVED && now >= r->start_time && now < r->end_time) {
                pthread_mutex_unlock(&manager->mutex);
                return r; // 활성화된 예약을 찾았으므로 반환
            }
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return NULL; // 활성화된 예약 없음
}

