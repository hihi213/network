#ifndef RESOURCE_H
#define RESOURCE_H

#include "common.h"

/* 장비 상태 열거형 */
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
    char reserved_by[MAX_USERNAME_LENGTH];
} Device;

/* 자원 관리자 구조체 */
typedef struct {
    Device devices[MAX_DEVICES];
    int device_count;
    pthread_mutex_t mutex;
} ResourceManager;

/* 함수 선언 */
ResourceManager* init_resource_manager(void);
void cleanup_resource_manager(ResourceManager* manager);
bool add_device(ResourceManager* manager, const char* id, const char* type, const char* name);
bool remove_device(ResourceManager* manager, const char* id);
bool update_device_status(ResourceManager* manager, const char* id, const char* status, const char* username);
Device* get_device(ResourceManager* manager, const char* id);
int get_device_list(ResourceManager* manager, Device* devices, int max_devices);
bool is_device_available(ResourceManager* manager, const char* id);

#endif /* RESOURCE_H */ 