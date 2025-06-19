/**
 * @file resource.c
 * @brief 리소스 관리 모듈 - 장비 정보 관리 및 상태 제어
 * @details 장비 추가/제거, 상태 변경, 목록 조회 기능을 제공합니다.
 */

#include "../include/resource.h"


// [개선] Device 구조체 자체를 해제하기 위한 래퍼 함수
static void free_device_wrapper(void* device) {
    free(device);
}

/**
 * @brief 리소스 매니저를 초기화하고 기본 장비들을 추가합니다.
 * @return 성공 시 초기화된 ResourceManager 포인터, 실패 시 NULL
 */
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

/**
 * @brief 리소스 매니저의 메모리를 정리합니다.
 * @param manager 정리할 ResourceManager 포인터
 */
void cleanup_resource_manager(ResourceManager* manager) {
    if (!manager) return;

    // [개선] 해시 테이블의 모든 자원을 해제
    ht_destroy(manager->devices);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * @brief 새로운 장비를 시스템에 추가합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 장비 고유 ID
 * @param type 장비 타입 (예: Printer, Scanner 등)
 * @param name 장비 이름
 * @return 성공 시 true, 실패 시 false
 */
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
    
    device->active_reservation_id = 0; // [추가] 필드 초기화

    bool success = ht_insert(manager->devices, id, device);
    if (!success) {
        free(device); // 삽입 실패 시 메모리 해제
    }

    pthread_mutex_unlock(&manager->mutex);
    return success;
}

/**
 * @brief 시스템에서 장비를 제거합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 제거할 장비의 ID
 * @return 성공 시 true, 실패 시 false
 */
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

/**
 * @brief 장비의 상태를 변경합니다.
 * @param manager 리소스 매니저 포인터
 * @param device_id 상태를 변경할 장비의 ID
 * @param new_status 새로운 상태
 * @param active_res_id 활성 예약 ID (예약 상태일 때만 사용)
 * @return 성공 시 true, 실패 시 false
 */
bool update_device_status(ResourceManager* manager, const char* device_id, DeviceStatus new_status, uint32_t active_res_id) {
    if (!manager || !device_id) return false;

    pthread_mutex_lock(&manager->mutex);
    
    Device* device_to_update = (Device*)ht_get(manager->devices, device_id);
    if (device_to_update == NULL) {
        LOG_WARNING("Resource", "상태를 업데이트할 장비를 찾지 못함: ID=%s", device_id);
        pthread_mutex_unlock(&manager->mutex);
        return false;
    }
    
  
    device_to_update->status = new_status;
    
    // [추가] 상태에 따라 예약 ID 필드 업데이트
    if (new_status == DEVICE_AVAILABLE) {
        device_to_update->active_reservation_id = 0; // 예약이 끝나면 ID를 0으로 초기화
    } else if (new_status == DEVICE_RESERVED) {
        device_to_update->active_reservation_id = active_res_id;
    }

    LOG_INFO("Resource", "장비 상태 업데이트 성공: ID=%s, 새 상태=%d, 예약ID=%u", device_id, new_status, active_res_id);

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

/**
 * @brief 모든 장비 목록을 조회합니다.
 * @param manager 리소스 매니저 포인터
 * @param devices 장비 정보를 저장할 배열
 * @param max_devices 배열의 최대 크기
 * @return 조회된 장비 개수, 실패 시 -1
 */
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


/**
 * @brief 특정 장비가 사용 가능한지 확인합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 확인할 장비의 ID
 * @return 사용 가능하면 true, 아니면 false
 */
bool is_device_available(ResourceManager* manager, const char* id) {
    if (!manager || !id) return false;

    pthread_mutex_lock(&manager->mutex);
    
    // [개선] 해시 테이블에서 O(1)에 장치 검색
    Device* device = (Device*)ht_get(manager->devices, id);
    bool available = (device != NULL && device->status == DEVICE_AVAILABLE);
    
    pthread_mutex_unlock(&manager->mutex);
    return available;
}