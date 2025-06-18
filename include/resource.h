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

/* 장비 구조체 */
typedef struct {
    char id[MAX_ID_LENGTH];
    char name[MAX_DEVICE_NAME_LENGTH];
    char type[MAX_DEVICE_TYPE_LENGTH];
    DeviceStatus status;
    uint32_t active_reservation_id; 
    time_t reservation_end_time; // [추가] 예약 종료 시각 저장 필드
} Device;

/* 함수 선언 */
ResourceManager* init_resource_manager(void);
bool add_device(ResourceManager* manager, const char* id, const char* type, const char* name);
bool remove_device(ResourceManager* manager, const char* id);
bool update_device_status(ResourceManager* manager, const char* device_id, DeviceStatus new_status, uint32_t active_res_id);
Device* get_device(ResourceManager* manager, const char* id);
int get_device_list(ResourceManager* manager, Device* devices, int max_devices);
bool is_device_available(ResourceManager* manager, const char* id);
void cleanup_resource_manager(ResourceManager* manager);

#endif /* RESOURCE_H */ 