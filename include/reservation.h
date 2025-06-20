#ifndef RESERVATION_H
#define RESERVATION_H

#include "utils.h"
#include "resource.h"

// [수정] ReservationStatus 열거형 정의 추가
typedef enum reservation_status {
    RESERVATION_APPROVED,
    RESERVATION_CANCELLED,
    RESERVATION_COMPLETED,
} reservation_status_t;

// [수정] Reservation 구조체 정의를 Manager보다 먼저 위치
typedef struct reservation {
    uint32_t id;
    char device_id[MAX_DEVICE_ID_LEN];
    char username[MAX_USERNAME_LENGTH];
    time_t start_time;
    time_t end_time;
    char reason[MAX_REASON_LEN];
    reservation_status_t status;
    time_t created_at;
} reservation_t;

// ReservationManager 구조체 정의
typedef struct reservation_manager {
    reservation_t reservations[MAX_RESERVATIONS]; // 실제 데이터 저장소
    hash_table_t* reservation_map; // 예약 ID를 키로 빠른 조회를 위한 해시 테이블 (포인터 저장)
    int reservation_count;
    uint32_t next_reservation_id;
    pthread_mutex_t mutex;
    void (*broadcast_callback)(void);
} reservation_manager_t;

// forward declaration
struct resource_manager;

// 함수 프로토타입
reservation_manager_t* reservation_init_manager(struct resource_manager* res_manager, void (*callback)(void));
void reservation_cleanup_manager(reservation_manager_t* manager);
uint32_t reservation_create(reservation_manager_t* manager, const char* device_id,
                            const char* username, time_t start_time,
                            time_t end_time, const char* reason);
bool reservation_cancel(reservation_manager_t* manager, uint32_t reservation_id,
                       const char* username);
void reservation_cleanup_expired(reservation_manager_t* manager, struct resource_manager* res_manager);

reservation_t* reservation_get_active_for_device(reservation_manager_t* resv_manager, struct resource_manager* rsrc_manager, const char* device_id);
#endif // RESERVATION_H