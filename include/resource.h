#ifndef RESOURCE_H
#define RESOURCE_H

#include "utils.h" // 해시 테이블 헤더 추가

typedef struct ResourceManager {
    HashTable* devices; // [개선] 배열 대신 해시 테이블 사용
    pthread_mutex_t mutex;
} ResourceManager;

typedef enum {
    DEVICE_AVAILABLE,
    DEVICE_RESERVED,
    DEVICE_MAINTENANCE
} DeviceStatus;

typedef struct {
    // --- 장비의 고유 속성 ---
    char id[MAX_ID_LENGTH];                // 장비의 고유 식별자 (예: "DEV001")
    char name[MAX_DEVICE_NAME_LENGTH];     // 장비의 이름 (예: "HP LaserJet Pro")
    char type[MAX_DEVICE_TYPE_LENGTH];     // 장비의 종류 (예: "Printer")

    // --- 서버에서 관리하는 핵심 상태 정보 ---
    DeviceStatus status;                   // 현재 장비 상태 (available, reserved, maintenance)
    uint32_t active_reservation_id;        // 활성화된 예약의 ID (예약 원본 데이터 연결용 '외래 키')

    // --- 클라이언트 UI 표시를 위해 서버가 조합해주는 정보 ---
    time_t reservation_end_time;           // 예약 종료 시각 (클라이언트의 '남은 시간' 표시용)
    char reserved_by[MAX_USERNAME_LENGTH]; // 현재 예약자 이름 (클라이언트의 '예약자' 표시용)

} Device;

/* 함수 선언 */
ResourceManager* resource_init_manager(void);
bool resource_add_device(ResourceManager* manager, const char* id, const char* type, const char* name);
bool resource_remove_device(ResourceManager* manager, const char* id);
bool resource_update_device_status(ResourceManager* manager, const char* device_id, DeviceStatus new_status, uint32_t active_res_id);
int resource_get_device_list(ResourceManager* manager, Device* devices, int max_devices);
bool resource_is_device_available(ResourceManager* manager, const char* id);
void resource_cleanup_manager(ResourceManager* manager);

#endif /* RESOURCE_H */ 