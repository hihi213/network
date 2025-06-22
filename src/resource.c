/**
 * @file resource.c
 * @brief 리소스 관리 모듈 - 시스템 장비 정보 및 상태 관리
 * 
 * @details
 * 이 모듈은 시스템이 관리하는 장비(리소스) 정보를 관리합니다:
 * 
 * **핵심 역할:**
 * - 장비의 추가/제거 및 기본 정보 관리
 * - 장비 상태 변경 및 실시간 업데이트
 * - 전체 장비 목록 조회 및 필터링
 * - 장비별 예약 상태 추적 및 관리
 * 
 * **시스템 아키텍처에서의 위치:**
 * - 데이터 계층: 장비 정보 저장 및 관리
 * - 비즈니스 로직 계층: 장비 상태 변경 처리
 * - 상태 관리 계층: 실시간 장비 상태 추적
 * - 인터페이스 계층: 클라이언트 요청 처리
 * 
 * **장비 정보 구조:**
 * - 기본 정보: ID, 이름, 설명, 위치
 * - 상태 정보: 사용 가능/불가, 점검 중, 예약됨
 * - 예약 정보: 현재 예약자, 예약 시간, 예약 상태
 * - 메타데이터: 생성 시간, 마지막 업데이트, 카테고리
 * 
 * **주요 기능:**
 * - 해시 테이블을 통한 O(1) 장비 조회
 * - 스레드 안전한 장비 정보 수정
 * - 장비 상태 변경 시 자동 브로드캐스트
 * - 장비별 상세 정보 및 통계 제공
 * 
 * **장비 상태 관리:**
 * - AVAILABLE: 사용 가능한 상태
 * - IN_USE: 현재 사용 중인 상태
 * - MAINTENANCE: 점검 중인 상태
 * - RESERVED: 예약된 상태
 * - OFFLINE: 오프라인 상태
 * 
 * **성능 최적화:**
 * - 해시 테이블 기반 빠른 검색
 * - 상태 변경 시 최소한의 락 사용
 * - 메모리 효율적인 데이터 구조
 * - 캐시 친화적인 메모리 레이아웃
 * 
 * @note 이 모듈은 시스템의 핵심 데이터를 관리하며, 모든
 *       장비 관련 작업의 중앙 집중식 처리점 역할을 합니다.
 */

#include "../include/resource.h"  // 리소스 관련 헤더 파일 포함
#include "../include/message.h"   // message_get_device_status_string 함수 사용을 위해 포함

// [개선] device_t 구조체 자체를 해제하기 위한 래퍼 함수
static void resource_free_device_wrapper(void* device) {
    free(device);  // 장치 포인터 메모리 해제
}

/**
 * @brief 리소스 매니저를 초기화하고 기본 장비들을 추가합니다.
 * @return 성공 시 초기화된 resource_manager_t 포인터, 실패 시 NULL
 */
resource_manager_t* resource_init_manager(void) {
    resource_manager_t* manager = (resource_manager_t*)malloc(sizeof(resource_manager_t));  // 자원 관리자 메모리 할당
    if (!manager) {  // 메모리 할당 실패 시
        utils_report_error(ERROR_RESOURCE_INIT_FAILED, "Resource", "자원 관리자 메모리 할당 실패");  // 에러 로그 출력
        return NULL;  // NULL 반환
    }

    // [개선] 공통 초기화 헬퍼 함수 사용
    if (!utils_init_manager_base(manager, sizeof(resource_manager_t), &manager->devices, MAX_DEVICES, resource_free_device_wrapper, &manager->mutex)) {
        utils_report_error(ERROR_RESOURCE_INIT_FAILED, "Resource", "매니저 공통 초기화 실패");  // 에러 로그 출력
        free(manager);  // 관리자 메모리 해제
        return NULL;  // NULL 반환
    }

    // 기본 장치 추가
    resource_add_device(manager, "DEV001", "Printer", "HP LaserJet Pro");  // 프린터 장치 추가
    resource_add_device(manager, "DEV002", "Scanner", "Epson Perfection V600");  // 스캐너 장치 추가
    resource_add_device(manager, "DEV003", "Projector", "BenQ MH535");  // 프로젝터 장치 추가
    resource_add_device(manager, "DEV004", "Camera", "Canon EOS R5");  // 카메라 장치 추가
    resource_add_device(manager, "DEV005", "Microphone", "Blue Yeti");  // 마이크 장치 추가

    // LOG_INFO("Resource", "리소스 매니저 초기화 성공");  // 정보 로그 출력
    return manager;  // 초기화된 매니저 반환
}

/**
 * @brief 리소스 매니저의 메모리를 정리합니다.
 * @param manager 정리할 resource_manager_t 포인터
 */
void resource_cleanup_manager(resource_manager_t* manager) {
    if (!manager) return;
    if (!manager) return;  // 매니저가 NULL이면 함수 종료

    // [개선] 해시 테이블의 모든 자원을 해제
    if (manager->devices) {
        utils_hashtable_destroy(manager->devices);  // 해시 테이블 정리
    }
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
bool resource_add_device(resource_manager_t* manager, const char* id, const char* type, const char* name) {
    if (!manager || !id || !type || !name) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Resource", "resource_add_device: 잘못된 파라미터");
        return false;
    }

    pthread_mutex_lock(&manager->mutex);

    // [개선된 로직] 해시 테이블에서 기존 장비가 있는지 '조회'만 합니다.
    device_t* existing_device = (device_t*)utils_hashtable_get(manager->devices, id);

    if (existing_device) {
        // --- 장비 정보 갱신 (In-place Update) ---
        LOG_INFO("Resource", "기존 장비 정보 갱신 시작: ID=%s", id);

        // 기존 메모리를 재사용하여 내용만 복사합니다.
        strncpy(existing_device->type, type, MAX_DEVICE_TYPE_LENGTH - 1);
        existing_device->type[MAX_DEVICE_TYPE_LENGTH - 1] = '\0';
        strncpy(existing_device->name, name, MAX_DEVICE_NAME_LENGTH - 1);
        existing_device->name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';

        // 정보를 갱신했으므로, 상태를 초기화하는 것이 바람직할 수 있습니다.
        // (정책에 따라 이 부분은 달라질 수 있습니다.)
        if (existing_device->status == DEVICE_MAINTENANCE) {
            LOG_INFO("Resource", "점검 중인 장비 정보 갱신, 상태 유지: ID=%s", id);
        } else {
            // 예약이 꼬이는 것을 방지하기 위해 사용 가능 상태로 초기화
            existing_device->status = DEVICE_AVAILABLE;
            existing_device->reservation_end_time = 0;
            existing_device->active_reservation_id = 0;
            memset(existing_device->reserved_by, 0, sizeof(existing_device->reserved_by));
        }

        pthread_mutex_unlock(&manager->mutex);
        return true; // 갱신 성공

    } else {
        // --- 신규 장비 추가 (기존 로직과 동일) ---
        LOG_INFO("Resource", "신규 장비 추가 시작: ID=%s", id);
        
        device_t* new_device = (device_t*)malloc(sizeof(device_t));
        if (!new_device) {
            pthread_mutex_unlock(&manager->mutex);
            utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Resource", "장비 메모리 할당 실패: ID=%s", id);
            return false;
        }

        // 장비 정보 초기화
        strncpy(new_device->id, id, MAX_DEVICE_ID_LEN - 1);
        new_device->id[MAX_DEVICE_ID_LEN - 1] = '\0';
        strncpy(new_device->type, type, MAX_DEVICE_TYPE_LENGTH - 1);
        new_device->type[MAX_DEVICE_TYPE_LENGTH - 1] = '\0';
        strncpy(new_device->name, name, MAX_DEVICE_NAME_LENGTH - 1);
        new_device->name[MAX_DEVICE_NAME_LENGTH - 1] = '\0';
        new_device->status = DEVICE_AVAILABLE;
        new_device->reservation_end_time = 0;
        new_device->reserved_by[0] = '\0';
        new_device->active_reservation_id = 0;

        if (!utils_hashtable_insert(manager->devices, id, new_device)) {
            pthread_mutex_unlock(&manager->mutex);
            free(new_device);
            utils_report_error(ERROR_RESOURCE_INIT_FAILED, "Resource", "해시 테이블에 장비 삽입 실패: ID=%s", id);
            return false;
        }

        pthread_mutex_unlock(&manager->mutex);
        return true;
    }
}

/**
 * @brief 시스템에서 장비를 제거합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 제거할 장비의 ID
 * @return 성공 시 true, 실패 시 false
 */
bool resource_remove_device(resource_manager_t* manager, const char* id) {
    if (!manager || !id) {  // 유효성 검사
        utils_report_error(ERROR_INVALID_PARAMETER, "Resource", "잘못된 파라미터");  // 에러 로그 출력
        return false;  // 에러 코드 반환
    }
    
    // LOG_INFO("Resource", "장비 제거 시작: ID=%s", id);

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    // [개선] 해시 테이블에서 장비 조회
    device_t* device = (device_t*)utils_hashtable_get(manager->devices, id);
    if (!device) {  // 장비를 찾을 수 없는 경우
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        utils_report_error(ERROR_RESOURCE_NOT_FOUND, "Resource", "장치를 찾을 수 없음: %s", id);  // 에러 로그 출력
        return false;  // 에러 코드 반환
    }
    
    if (device->status == DEVICE_RESERVED) {  // 예약 중인 장치인 경우
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        utils_report_error(ERROR_RESOURCE_IN_USE, "Resource", "예약 중인 장치는 제거할 수 없음: %s", id);  // 에러 로그 출력
        return false;  // 에러 코드 반환
    }
    
    // [개선] 해시 테이블에서 장비 삭제
    if (utils_hashtable_delete(manager->devices, id)) {  // 해시 테이블에서 장비 삭제 성공 시
        // LOG_INFO("Resource", "장비 제거 성공: ID=%s", id);  // 정보 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return true;  // 성공 코드 반환
    } else {  // 장비 삭제 실패 시
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        utils_report_error(ERROR_RESOURCE_NOT_FOUND, "Resource", "장비 삭제 실패: ID=%s", id);  // 에러 로그 출력
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
bool resource_update_device_status(resource_manager_t* manager, const char* device_id, device_status_t new_status, uint32_t active_res_id) {
    if (!manager || !device_id) return false;  // 유효성 검사

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    device_t* device_to_update = (device_t*)utils_hashtable_get(manager->devices, device_id);  // 해시 테이블에서 장치 조회
    if (device_to_update == NULL) {  // 장치를 찾을 수 없는 경우
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

    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return true;  // true 반환
}

// [개선] 해시 테이블 순회 콜백을 위한 데이터 구조
typedef struct {
    device_t* devices;
    int max_devices;
    int current_count;
} TraverseData;

static void resource_copy_device_callback(const char* key, void* value, void* user_data) {
    (void)key; // 미사용 매개변수 경고 해결
    TraverseData* data = (TraverseData*)user_data;
    if (data->current_count >= data->max_devices) return;
    device_t* source_device = (device_t*)value;  // 소스 장치 포인터
    device_t* dest_device = &data->devices[data->current_count];  // 대상 장치 포인터
    memcpy(dest_device, source_device, sizeof(device_t));
    data->current_count++;
}

int resource_get_device_list(resource_manager_t* manager, device_t* devices, int max_devices) {
    if (!manager || !devices || max_devices <= 0) {
        utils_report_error(ERROR_INVALID_PARAMETER, "Resource", "resource_get_device_list: 잘못된 파라미터");
        return -1;  // 유효성 검사
    }
    
    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    TraverseData data = { .devices = devices, .max_devices = max_devices, .current_count = 0 };  // 순회 데이터 초기화
    utils_hashtable_traverse(manager->devices, resource_copy_device_callback, &data);  // 해시 테이블 순회
    int count = data.current_count;  // 조회된 장치 개수

    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return count;  // 장치 개수 반환
}

/**
 * @brief 특정 장비가 사용 가능한지 확인합니다.
 * @param manager 리소스 매니저 포인터
 * @param id 확인할 장비의 ID
 * @return 사용 가능하면 true, 아니면 false
 */
bool resource_is_device_available(resource_manager_t* manager, const char* id) {
    if (!manager || !id) return false;  // 유효성 검사

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    device_t* device = (device_t*)utils_hashtable_get(manager->devices, id);  // 해시 테이블에서 장치 조회
    bool available = (device != NULL && device->status == DEVICE_AVAILABLE);  // 사용 가능 여부 확인
    
    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return available;  // 사용 가능 여부 반환
}