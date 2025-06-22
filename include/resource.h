#ifndef RESOURCE_H
#define RESOURCE_H

#include "utils.h" // 해시 테이블 헤더 추가

typedef struct resource_manager {
    hash_table_t* devices;
    pthread_mutex_t mutex;
} resource_manager_t;

typedef enum device_status {
    DEVICE_AVAILABLE,
    DEVICE_RESERVED,
    DEVICE_MAINTENANCE
} device_status_t;

typedef struct device {
    char id[MAX_ID_LENGTH];
    char name[MAX_DEVICE_NAME_LENGTH];
    char type[MAX_DEVICE_TYPE_LENGTH];
    device_status_t status;
    uint32_t active_reservation_id;
    time_t reservation_end_time;
    char reserved_by[MAX_USERNAME_LENGTH];
} device_t;

/* 함수 선언 */
resource_manager_t* resource_init_manager(void);
bool resource_add_device(resource_manager_t* manager, const char* id, const char* type, const char* name);
bool resource_remove_device(resource_manager_t* manager, const char* id);
bool resource_update_device_status(resource_manager_t* manager, const char* device_id, device_status_t new_status, uint32_t active_res_id);
int resource_get_device_list(resource_manager_t* manager, device_t* devices, int max_devices);
bool resource_is_device_available(resource_manager_t* manager, const char* id);
void resource_cleanup_manager(resource_manager_t* manager);
#endif /* RESOURCE_H */ 