#include "../include/resource.h"


// [개선] Device 구조체 자체를 해제하기 위한 래퍼 함수
static void free_device_wrapper(void* device) {
    free(device);
}

/* 리소스 매니저 초기화 */
ResourceManager* init_resource_manager(void) {
    ResourceManager* manager = (ResourceManager*)malloc(sizeof(ResourceManager));
    if (!manager) {
        LOG_ERROR("Resource", "자원 관리자 메모리 할당 실패");
        return NULL;
    }

    // [개선] 해시 테이블 초기화. 최대 장비 수(MAX_DEVICES)를 크기로 지정.
    manager->devices = ht_create(MAX_DEVICES, free_device_wrapper);
    if (!manager->devices) {
        LOG_ERROR("Resource", "장치 해시 테이블 생성 실패");
        free(manager);
        return NULL;
    }
    
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Resource", "뮤텍스 초기화 실패");
        ht_destroy(manager->devices);
        free(manager);
        return NULL;
    }

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

    // [개선] 해시 테이블의 모든 자원을 해제
    ht_destroy(manager->devices);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/* 장치 추가 */
bool add_device(ResourceManager* manager, const char* id, const char* type, const char* name) {
    if (!manager || !id || !type || !name) return false;

    pthread_mutex_lock(&manager->mutex);

    // [개선] 해시 테이블을 사용하여 이미 존재하는지 빠르게 확인
    if (ht_get(manager->devices, id) != NULL) {
        pthread_mutex_unlock(&manager->mutex);
        return false; // 이미 존재하는 장비
    }

    // 새 장비 객체를 동적으로 할당
    Device* device = (Device*)malloc(sizeof(Device));
    if (!device) {
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    strncpy(device->id, id, MAX_ID_LENGTH - 1);
    strncpy(device->type, type, MAX_DEVICE_TYPE_LENGTH - 1);
    strncpy(device->name, name, MAX_DEVICE_NAME_LENGTH - 1);
    device->status = DEVICE_AVAILABLE;
    device->reserved_by[0] = '\0';

    // [개선] 해시 테이블에 삽입
    bool success = ht_insert(manager->devices, id, device);
    if (!success) {
        free(device); // 삽입 실패 시 메모리 해제
    }

    pthread_mutex_unlock(&manager->mutex);
    return success;
}

/* 장치 제거 */
bool remove_device(ResourceManager* manager, const char* id) {
    if (!manager || !id) {
        LOG_ERROR("Resource", "잘못된 파라미터");
        return false;
    }
    
    pthread_mutex_lock(&manager->mutex);
    
    // [개선] 해시 테이블에서 바로 장치를 찾아옴
    Device* device = (Device*)ht_get(manager->devices, id);
    if (!device) {
        LOG_ERROR("Resource", "장치를 찾을 수 없음: %s", id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    if (device->status == DEVICE_RESERVED) {
        LOG_ERROR("Resource", "예약 중인 장치는 제거할 수 없음: %s", id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    // [개선] 해시 테이블에서 바로 삭제. ht_destroy가 내부적으로 메모리 해제 처리
    bool success = ht_delete(manager->devices, id);
    if(success) LOG_INFO("Resource", "장치 제거 성공: %s", id);

    pthread_mutex_unlock(&manager->mutex);
    return success;
}

/* 장치 상태 변경 */
bool update_device_status(ResourceManager* manager, const char* device_id, DeviceStatus new_status) {
    if (!manager || !device_id) return false;

    pthread_mutex_lock(&manager->mutex);
    
    // [개선] 해시 테이블에서 O(1)에 장치 검색
    Device* device_to_update = (Device*)ht_get(manager->devices, device_id);
    if (device_to_update == NULL) {
        LOG_WARNING("Resource", "상태를 업데이트할 장비를 찾지 못함: ID=%s", device_id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
    device_to_update->status = new_status;
    LOG_INFO("Resource", "장비 상태 업데이트 성공: ID=%s, 새 상태=%d", device_id, new_status);

    pthread_mutex_unlock(&manager->mutex);
    return true;
}

// [개선] 해시 테이블 순회 콜백을 위한 데이터 구조
typedef struct {
    Device* devices;
    int max_devices;
    int current_count;
} TraverseData;

// [개선] 해시 테이블의 각 장치를 배열로 복사하는 콜백 함수
static void copy_device_callback(const char* key, void* value, void* user_data) {
    (void)key;
    TraverseData* data = (TraverseData*)user_data;
    if (data->current_count < data->max_devices) {
        memcpy(&data->devices[data->current_count], (Device*)value, sizeof(Device));
        data->current_count++;
    }
}

/* 장치 목록 조회 */
int get_device_list(ResourceManager* manager, Device* devices, int max_devices) {
    if (!manager || !devices || max_devices <= 0) return -1;
    
    pthread_mutex_lock(&manager->mutex);
    
    // [개선] 해시 테이블을 순회하며 장치 목록을 채움
    TraverseData data = { .devices = devices, .max_devices = max_devices, .current_count = 0 };
    ht_traverse(manager->devices, copy_device_callback, &data);
    int count = data.current_count;

    pthread_mutex_unlock(&manager->mutex);
    return count;
}


/* 장치 상태 확인 */
bool is_device_available(ResourceManager* manager, const char* id) {
    if (!manager || !id) return false;

    pthread_mutex_lock(&manager->mutex);
    
    // [개선] 해시 테이블에서 O(1)에 장치 검색
    Device* device = (Device*)ht_get(manager->devices, id);
    bool available = (device != NULL && device->status == DEVICE_AVAILABLE);
    
    pthread_mutex_unlock(&manager->mutex);
    return available;
}