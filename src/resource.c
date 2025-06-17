#include "../include/resource.h"

#include "../include/logger.h"

/* 리소스 매니저 초기화 */
ResourceManager* init_resource_manager(void) {
    ResourceManager* manager = (ResourceManager*)malloc(sizeof(ResourceManager));
    if (!manager) {
        LOG_ERROR("Resource", "자원 관리자 메모리 할당 실패");
        return NULL;
    }

    manager->device_count = 0;
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Resource", "뮤텍스 초기화 실패");
        free(manager);
        return NULL;
    }

    // 장치 배열 초기화
    memset(manager->devices, 0, sizeof(manager->devices));

    // 기본 장치 추가
    add_device(manager, "DEV001", "Printer", "HP LaserJet Pro");
    add_device(manager, "DEV002", "Scanner", "Epson Perfection V600");
    add_device(manager, "DEV003", "Projector", "BenQ MH535");
    add_device(manager, "DEV004", "Camera", "Canon EOS R5");
    add_device(manager, "DEV005", "Microphone", "Blue Yeti");

    return manager;
}

/* 리소스 매니저 정리 */
void cleanup_resource_manager(ResourceManager* manager) {
    if (!manager) return;

    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/* 장치 추가 */
bool add_device(ResourceManager* manager, const char* id, const char* type, const char* name) {
    if (!manager || !id || !type || !name) return false;

    pthread_mutex_lock(&manager->mutex);

    // 이미 존재하는 장비인지 확인
    for (int i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].id, id) == 0) {
            pthread_mutex_unlock(&manager->mutex);
            return false;
        }
    }

    // 새 장비 추가
    if (manager->device_count < MAX_DEVICES) {
    Device* device = &manager->devices[manager->device_count];
    strncpy(device->id, id, MAX_ID_LENGTH - 1);
        strncpy(device->type, type, MAX_DEVICE_TYPE_LENGTH - 1);
        strncpy(device->name, name, MAX_DEVICE_NAME_LENGTH - 1);
        device->status = DEVICE_AVAILABLE;  // enum 값 직접 할당
    device->reserved_by[0] = '\0';
    manager->device_count++;
    pthread_mutex_unlock(&manager->mutex);
    return true;
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/* 장치 제거 */
bool remove_device(ResourceManager* manager, const char* id) {
    if (!manager || !id) {
        LOG_ERROR("Resource", "잘못된 파라미터");
        return false;
    }

    LOG_INFO("Resource", "장치 제거 시작: ID=%s", id);

    pthread_mutex_lock(&manager->mutex);

    int index = -1;
    for (int i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].id, id) == 0) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        LOG_ERROR("Resource", "장치를 찾을 수 없음: %s", id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 장치가 예약 중인지 확인
    if (manager->devices[index].status == DEVICE_RESERVED) {
        LOG_ERROR("Resource", "예약 중인 장치는 제거할 수 없음: %s", id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }

    // 장치 제거 (마지막 장치를 현재 위치로 이동)
    if (index < manager->device_count - 1) {
        memmove(&manager->devices[index], &manager->devices[index + 1],
                (manager->device_count - index - 1) * sizeof(Device));
    }
    manager->device_count--;

    LOG_INFO("Resource", "장치 제거 성공: %s", id);

    pthread_mutex_unlock(&manager->mutex);
    return true;
}

/**
 * @brief 특정 장비의 상태(DeviceStatus)를 변경합니다.
 * @param manager 자원 관리자(ResourceManager) 포인터.
 * @param device_id 상태를 변경할 장비의 ID.
 * @param new_status 새로운 장비 상태 (DEVICE_AVAILABLE, DEVICE_RESERVED 등).
 * @return 성공 시 true, 장비를 찾지 못하는 등 실패 시 false를 반환합니다.
 */
bool update_device_status(ResourceManager* manager, const char* device_id, DeviceStatus new_status) {
    // 1. 파라미터 유효성 검사
    if (!manager || !device_id) {
        LOG_ERROR("Resource", "update_device_status: NULL 파라미터 수신");
        return false;
    }

    // 2. 스레드 안전성을 위해 뮤텍스 잠금
    pthread_mutex_lock(&manager->mutex);

    Device* device_to_update = NULL;
    // 3. 관리자 목록에서 해당 ID를 가진 장비 검색
    for (int i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].id, device_id) == 0) {
            device_to_update = &manager->devices[i];
            break;
        }
    }

    // 4. 장비를 찾지 못한 경우 처리
    if (device_to_update == NULL) {
        LOG_WARNING("Resource", "상태를 업데이트할 장비를 찾지 못함: ID=%s", device_id);
        pthread_mutex_unlock(&manager->mutex); // 잠금 해제
        return false;
    }

    // 5. 장비 상태 업데이트
    device_to_update->status = new_status;

    LOG_INFO("Resource", "장비 상태 업데이트 성공: ID=%s, 새 상태=%d", 
             device_id, new_status);

    // 6. 뮤텍스 잠금 해제
    pthread_mutex_unlock(&manager->mutex);

    return true;
}

/* 장치 정보 조회 */
Device* get_device(ResourceManager* manager, const char* id) {
    if (!manager || !id) {
        LOG_ERROR("Resource", "잘못된 파라미터");
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);

    Device* device = NULL;
    for (int i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].id, id) == 0) {
            device = &manager->devices[i];
            break;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return device;
}

/* 장치 목록 조회 */
int get_device_list(ResourceManager* manager, Device* devices, int max_devices) {
    if (!manager || !devices || max_devices <= 0) return -1;

    pthread_mutex_lock(&manager->mutex);

    int count = (manager->device_count < max_devices) ? manager->device_count : max_devices;
    memcpy(devices, manager->devices, count * sizeof(Device));

    pthread_mutex_unlock(&manager->mutex);
    return count;
}

/* 장치 상태 확인 */
bool is_device_available(ResourceManager* manager, const char* id) {
    if (!manager || !id) return false;

    pthread_mutex_lock(&manager->mutex);

    for (int i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].id, id) == 0) {
            bool available = (manager->devices[i].status == DEVICE_AVAILABLE);
            pthread_mutex_unlock(&manager->mutex);
            return available;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return false;
}

/* 장치 예약 상태 확인 */
bool is_device_reserved(ResourceManager* manager, const char* id) {
    if (!manager || !id) {
        LOG_ERROR("Resource", "잘못된 파라미터");
        return false;
    }

    pthread_mutex_lock(&manager->mutex);

    bool reserved = false;
    for (int i = 0; i < manager->device_count; i++) {
        if (strcmp(manager->devices[i].id, id) == 0) {
            reserved = (manager->devices[i].status == DEVICE_RESERVED);
            break;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return reserved;
} 