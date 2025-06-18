#ifndef RESERVATION_H
#define RESERVATION_H

#include "common.h"

/* 예약 상태 열거형 */
typedef enum {
    RESERVATION_PENDING,
    RESERVATION_APPROVED,
    RESERVATION_REJECTED,
    RESERVATION_CANCELLED,
    RESERVATION_EXPIRED,
    RESERVATION_COMPLETED
} ReservationStatus;

/* 예약 구조체 */
typedef struct {
    uint32_t id;
    char device_id[MAX_ID_LENGTH];
    char username[MAX_USERNAME_LENGTH];
    time_t start_time;
    time_t end_time;
    char reason[MAX_REASON_LEN];
    ReservationStatus status;
    time_t created_at;
} Reservation;

/* 예약 관리자 구조체 */
typedef struct {
    Reservation reservations[MAX_RESERVATIONS];
    int reservation_count;
    uint32_t next_reservation_id;
    pthread_mutex_t mutex;
} ReservationManager;

/* 함수 선언 */
ReservationManager* init_reservation_manager(void);
void cleanup_reservation_manager(ReservationManager* manager);
bool create_reservation(ReservationManager* manager, const char* device_id, const char* username, time_t start_time, time_t end_time, const char* reason);
bool cancel_reservation(ReservationManager* manager, uint32_t reservation_id, const char* username);
int get_device_reservations(ReservationManager* manager, const char* device_id, Reservation* reservations, int max_reservations);
void cleanup_expired_reservations(ReservationManager* manager);
Reservation* get_active_reservation_for_device(ReservationManager* manager, const char* device_id);

#endif /* RESERVATION_H */