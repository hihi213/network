/**
 * @file resource.c
 * @brief 리소스 관리 모듈 - 장비 정보 관리 및 상태 제어
 * @details 장비 추가/제거, 상태 변경, 목록 조회 기능을 제공합니다.
 */

#include "../include/resource.h"  // 리소스 관련 헤더 파일 포함
#include "../include/message.h"   // get_device_status_string 함수 사용을 위해 포함

// [개선] Device 구조체 자체를 해제하기 위한 래퍼 함수
static void free_device_wrapper(void* device) {
    free(device);  // 장치 포인터 메모리 해제
}

/**
 * @brief 리소스 매니저를 초기화하고 기본 장비들을 추가합니다.
 * @return 성공 시 초기화된 ResourceManager 포인터, 실패 시 NULL
 */
ResourceManager* init_resource_manager(void) {
    ResourceManager* manager = (ResourceManager*)malloc(sizeof(ResourceManager));  // 리소스 매니저 메모리 할당
    if (!manager) {  // 메모리 할당 실패 시
        error_report(ERROR_RESOURCE_INIT_FAILED, "Resource", "자원 관리자 메모리 할당 실패");  // 에러 로그 출력
        return NULL;  // NULL 반환
    }

    // [개선] 해시 테이블 초기화. 최대 장비 수(MAX_DEVICES)를 크기로 지정.
    manager->devices = ht_create(MAX_DEVICES, free_device_wrapper);  // 해시 테이블 생성
    if (!manager->devices) {  // 해시 테이블 생성 실패 시
        error_report(ERROR_RESOURCE_INIT_FAILED, "Resource", "장치 해시 테이블 생성 실패");  // 에러 로그 출력
        free(manager);  // 매니저 메모리 해제
        return NULL;  // NULL 반환
    }
    
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {  // 뮤텍스 초기화 실패 시
        error_report(ERROR_RESOURCE_INIT_FAILED, "Resource", "뮤텍스 초기화 실패");  // 에러 로그 출력
        ht_destroy(manager->devices);  // 해시 테이블 정리
        free(manager);  // 매니저 메모리 해제
        return NULL;  // NULL 반환
    }

    // 기본 장치 추가
    add_device(manager, "DEV001", "Printer", "HP LaserJet Pro");  // 프린터 장치 추가
    add_device(manager, "DEV002", "Scanner", "Epson Perfection V600");  // 스캐너 장치 추가
    add_device(manager, "DEV003", "Projector", "BenQ MH535");  // 프로젝터 장치 추가
    add_device(manager, "DEV004", "Camera", "Canon EOS R5");  // 카메라 장치 추가
    add_device(manager, "DEV005", "Microphone", "Blue Yeti");  // 마이크 장치 추가

    // LOG_INFO("Resource", "리소스 매니저 초기화 성공");  // 정보 로그 출력
    return manager;  // 초기화된 매니저 반환
}

/**
 * @brief 리소스 매니저의 메모리를 정리합니다.
 * @param manager 정리할 ResourceManager 포인터
 */
void cleanup_resource_manager(ResourceManager* manager) {
    if (!manager) return;  // 매니저가 NULL이면 함수 종료

    // [개선] 해시 테이블의 모든 자원을 해제
    ht_destroy(manager->devices);  // 해시 테이블 정리
    pthread_mutex_destroy(&manager->mutex);  // 뮤텍스 정리
    free(manager);  // 매니저 메모리 해제
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
    if (!manager || !id || !type || !name) {  // 유효성 검사
        error_report(ERROR_INVALID_PARAMETER, "Resource", "add_device: 잘못된 파라미터");
        return false;  // 에러 코드 반환
    }

    // LOG_INFO("Resource", "장비 추가 시작: ID=%s, 타입=%s, 이름=%s", id, type, name);

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금

    // [개선] 해시 테이블에서 기존 장비 확인
    Device* existing_device = (Device*)ht_get(manager->devices, id);
    if (existing_device) {  // 기존 장비가 존재하는 경우
        // LOG_INFO("Resource", "기존 장비 발견, 교체: ID=%s", id);
        ht_delete(manager->devices, id);  // 기존 장비 삭제
    }

    // 새 장비 생성
    Device* new_device = (Device*)malloc(sizeof(Device));  // 새 장비 메모리 할당
    if (!new_device) {  // 메모리 할당 실패 시
        error_report(ERROR_MEMORY_ALLOCATION_FAILED, "Resource", "장비 메모리 할당 실패: ID=%s", id);
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return false;  // 에러 코드 반환
    }

    // 장비 정보 초기화
    strncpy(new_device->id, id, MAX_DEVICE_ID_LEN - 1);  // 장비 ID 복사
    new_device->id[MAX_DEVICE_ID_LEN - 1] = '\0';  // 문자열 종료
    strncpy(new_device->type, type, MAX_DEVICE_TYPE_LENGTH - 1);  // 장비 타입 복사
    new_device->type[MAX_DEVICE_TYPE_LENGTH - 1] = '\0';  // 문자열 종료
    strncpy(new_device->name, name, MAX_DEVICE_NAME_LENGTH - 1);  // 장비 이름 복사
    new_device->name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';  // 문자열 종료
    new_device->status = DEVICE_AVAILABLE;  // 기본 상태를 사용 가능으로 설정
    new_device->reservation_end_time = 0;  // 예약 종료 시간을 0으로 초기화
    new_device->reserved_by[0] = '\0';  // 예약자 정보를 빈 문자열로 초기화

    // [개선] 해시 테이블에 장비 삽입
    if (!ht_insert(manager->devices, id, new_device)) {  // 해시 테이블 삽입 실패 시
        error_report(ERROR_RESOURCE_INIT_FAILED, "Resource", "해시 테이블에 장비 삽입 실패: ID=%s", id);
        free(new_device);  // 장비 메모리 해제
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return false;  // 에러 코드 반환
    }

    // LOG_INFO("Resource", "장비 추가 성공: ID=%s", id);  // 정보 로그 출력
    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return true;  // 성공 코드 반환
}

/**
 * @brief 시스템에서 장비를 제거합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 제거할 장비의 ID
 * @return 성공 시 true, 실패 시 false
 */
bool remove_device(ResourceManager* manager, const char* id) {
    if (!manager || !id) {  // 유효성 검사
        error_report(ERROR_INVALID_PARAMETER, "Resource", "잘못된 파라미터");  // 에러 로그 출력
        return false;  // 에러 코드 반환
    }
    
    // LOG_INFO("Resource", "장비 제거 시작: ID=%s", id);

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    // [개선] 해시 테이블에서 장비 조회
    Device* device = (Device*)ht_get(manager->devices, id);
    if (!device) {  // 장비를 찾을 수 없는 경우
        error_report(ERROR_RESOURCE_NOT_FOUND, "Resource", "장치를 찾을 수 없음: %s", id);  // 에러 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return false;  // 에러 코드 반환
    }
    
    if (device->status == DEVICE_RESERVED) {  // 예약 중인 장치인 경우
        error_report(ERROR_RESOURCE_IN_USE, "Resource", "예약 중인 장치는 제거할 수 없음: %s", id);  // 에러 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return false;  // 에러 코드 반환
    }
    
    // [개선] 해시 테이블에서 장비 삭제
    if (ht_delete(manager->devices, id)) {  // 해시 테이블에서 장비 삭제 성공 시
        // LOG_INFO("Resource", "장비 제거 성공: ID=%s", id);  // 정보 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return true;  // 성공 코드 반환
    } else {  // 장비 삭제 실패 시
        error_report(ERROR_RESOURCE_NOT_FOUND, "Resource", "장비 삭제 실패: ID=%s", id);  // 에러 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return false;  // 에러 코드 반환
    }
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
    if (!manager || !device_id) return false;  // 유효성 검사

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    Device* device_to_update = (Device*)ht_get(manager->devices, device_id);  // 해시 테이블에서 장치 조회
    if (device_to_update == NULL) {  // 장치를 찾을 수 없는 경우
        // LOG_WARNING("Resource", "상태를 업데이트할 장비를 찾지 못함: ID=%s", device_id);  // 경고 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return false;  // false 반환
    }
    
  
    device_to_update->status = new_status;  // 장치 상태 업데이트
    
    // [추가] 상태에 따라 예약 ID 필드 업데이트
    if (new_status == DEVICE_AVAILABLE) {  // 사용 가능 상태로 변경 시
        device_to_update->active_reservation_id = 0; // 예약이 끝나면 ID를 0으로 초기화
    } else if (new_status == DEVICE_RESERVED) {  // 예약 상태로 변경 시
        device_to_update->active_reservation_id = active_res_id;  // 활성 예약 ID 설정
    }

    // LOG_INFO("Resource", "장비 상태 업데이트 성공: ID=%s, 새 상태=%d, 예약ID=%u", device_id, new_status, active_res_id);  // 성공 로그 출력

    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return true;  // true 반환
}


// [개선] 해시 테이블 순회 콜백을 위한 데이터 구조
typedef struct {
    Device* devices;  // 장치 배열 포인터
    int max_devices;  // 최대 장치 개수
    int current_count;  // 현재 장치 개수
} TraverseData;

// [개선] 해시 테이블의 각 장치를 배열로 복사하는 콜백 함수
static void copy_device_callback(const char* key, void* value, void* user_data) {
    (void)key;  // 키는 사용하지 않음
    TraverseData* data = (TraverseData*)user_data;  // 사용자 데이터 캐스팅
    if (data->current_count < data->max_devices) {  // 최대 개수 제한 확인
        Device* source_device = (Device*)value;  // 소스 장치 포인터
        Device* dest_device = &data->devices[data->current_count];  // 대상 장치 포인터
        
        // LOG_INFO("Resource", "장비 복사 중: %d번째, ID=%s, 이름=%s, 타입=%s, 상태=%s", 
        //          data->current_count, source_device->id, source_device->name, 
        //          source_device->type, get_device_status_string(source_device->status));
        
        memcpy(dest_device, source_device, sizeof(Device));  // 장치 정보 복사
        data->current_count++;  // 현재 개수 증가
    } else {
        // LOG_WARNING("Resource", "최대 장비 수 초과로 복사 중단: 현재=%d, 최대=%d", 
        //             data->current_count, data->max_devices);
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
    if (!manager || !devices || max_devices <= 0) {
        error_report(ERROR_INVALID_PARAMETER, "Resource", "get_device_list: 잘못된 파라미터");
        return -1;  // 유효성 검사
    }
    
    // LOG_INFO("Resource", "장비 목록 조회 시작: 최대장비수=%d", max_devices);
    
    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    // [개선] 해시 테이블을 순회하며 장치 목록을 채움
    TraverseData data = { .devices = devices, .max_devices = max_devices, .current_count = 0 };  // 순회 데이터 초기화
    ht_traverse(manager->devices, copy_device_callback, &data);  // 해시 테이블 순회
    int count = data.current_count;  // 조회된 장치 개수

    // LOG_INFO("Resource", "해시 테이블 순회 완료: 조회된 장비수=%d", count);

    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return count;  // 장치 개수 반환
}


/**
 * @brief 특정 장비가 사용 가능한지 확인합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 확인할 장비의 ID
 * @return 사용 가능하면 true, 아니면 false
 */
bool is_device_available(ResourceManager* manager, const char* id) {
    if (!manager || !id) return false;  // 유효성 검사

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    // [개선] 해시 테이블에서 O(1)에 장치 검색
    Device* device = (Device*)ht_get(manager->devices, id);  // 해시 테이블에서 장치 조회
    bool available = (device != NULL && device->status == DEVICE_AVAILABLE);  // 사용 가능 여부 확인
    
    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return available;  // 사용 가능 여부 반환
}