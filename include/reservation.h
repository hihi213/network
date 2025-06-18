#ifndef RESERVATION_H
#define RESERVATION_H

#include "utils.h"
#include "resource.h"

// [수정] ReservationStatus 열거형 정의 추가
typedef enum {
    RESERVATION_APPROVED,
    RESERVATION_CANCELLED,
    RESERVATION_COMPLETED,
} ReservationStatus;

// [수정] Reservation 구조체 정의를 Manager보다 먼저 위치
typedef struct {
    uint32_t id;
    char device_id[MAX_DEVICE_ID_LEN];
    char username[MAX_USERNAME_LENGTH];
    time_t start_time;
    time_t end_time;
    char reason[MAX_REASON_LEN];
    ReservationStatus status;
    time_t created_at;
} Reservation;

// ReservationManager 구조체 정의
typedef struct ReservationManager {
    Reservation reservations[MAX_RESERVATIONS]; // 실제 데이터 저장소
    HashTable* reservation_map; // 예약 ID를 키로 빠른 조회를 위한 해시 테이블 (포인터 저장)
    int reservation_count;
    uint32_t next_reservation_id;
    pthread_mutex_t mutex;
    void (*broadcast_callback)(void);
} ReservationManager;

// forward declaration
struct ResourceManager;

// 함수 프로토타입
ReservationManager* init_reservation_manager(ResourceManager* res_manager, void (*callback)(void));
void cleanup_reservation_manager(ReservationManager* manager);
uint32_t create_reservation(ReservationManager* manager, const char* device_id,
                            const char* username, time_t start_time,
                            time_t end_time, const char* reason);
bool cancel_reservation(ReservationManager* manager, uint32_t reservation_id,
                       const char* username);
void cleanup_expired_reservations(ReservationManager* manager, struct ResourceManager* res_manager);
Reservation* get_active_reservation_for_device(ReservationManager* resv_manager, struct ResourceManager* rsrc_manager, const char* device_id);
#endif // RESERVATION_H