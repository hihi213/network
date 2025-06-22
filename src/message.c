/**
 * @file message.c
 * @brief 메시지 프로토콜 모듈 - 클라이언트-서버 간 통신 프로토콜 정의
 * 
 * @details
 * 이 모듈은 클라이언트와 서버 간에 교환되는 메시지 프로토콜을 정의하고
 * 관련 유틸리티 함수를 제공합니다:
 * 
 * **핵심 역할:**
 * - 메시지 객체의 생성/파괴 및 메모리 관리
 * - 장비 상태 응답 생성 및 구조화
 * - 에러 메시지 변환 및 표준화
 * - 메시지 타입별 처리 로직 구현
 * 
 * **시스템 아키텍처에서의 위치:**
 * - 프로토콜 계층: 애플리케이션 레벨 메시지 정의
 * - 데이터 계층: 메시지 구조체 및 타입 정의
 * - 비즈니스 로직 계층: 도메인별 메시지 처리
 * - 인터페이스 계층: 상위 모듈과의 메시지 교환
 * 
 * **메시지 타입 분류:**
 * - 인증 메시지: LOGIN, LOGOUT, AUTH_RESPONSE
 * - 장비 관리: EQUIPMENT_LIST, EQUIPMENT_STATUS, EQUIPMENT_UPDATE
 * - 예약 관리: RESERVATION_REQUEST, RESERVATION_RESPONSE, RESERVATION_CANCEL
 * - 시스템 메시지: BROADCAST, ERROR, HEARTBEAT
 * 
 * **주요 특징:**
 * - JSON 기반의 구조화된 메시지 형식
 * - 타입 안전성을 위한 열거형 기반 메시지 타입
 * - 메모리 누수 방지를 위한 자동 정리 메커니즘
 * - 확장 가능한 메시지 프로토콜 구조
 * 
 * **메시지 생명주기:**
 * - 생성: 메시지 객체 할당 및 초기화
 * - 직렬화: JSON 형태로 변환하여 전송
 * - 역직렬화: 수신된 JSON을 메시지 객체로 변환
 * - 처리: 비즈니스 로직에 따른 메시지 처리
 * - 정리: 메모리 해제 및 리소스 정리
 * 
 * @note 이 모듈은 시스템의 통신 프로토콜을 표준화하며, 모든
 *       클라이언트-서버 간 데이터 교환의 기반이 됩니다.
 */

#include "../include/message.h"
#include "../include/reservation.h"
#include "../include/network.h"

/**
 * @brief 메시지 객체 생성
 * @details
 * 메시지 타입과 데이터를 받아 새로운 메시지 객체를 생성합니다.
 * 메모리 할당 실패 시 NULL을 반환하며, 데이터는 안전하게 복사됩니다.
 * 
 * @param type 메시지 타입
 * @param data 메시지 데이터 (NULL 가능)
 * @return 생성된 메시지 객체, 실패 시 NULL
 */
message_t* message_create(message_type_t type, const char *data) {
    message_t* msg = (message_t*)malloc(sizeof(message_t));
    if (!msg) return NULL;
    
    msg->type = type;
    msg->arg_count = 0;
    msg->error_code = ERROR_NONE;
    memset(msg->args, 0, sizeof(msg->args));
    
    if (data) {
        strncpy(msg->data, data, MAX_MESSAGE_LENGTH - 1);
        msg->data[MAX_MESSAGE_LENGTH - 1] = '\0';
    } else {
        msg->data[0] = '\0';
    }
    
    LOG_INFO("Message", "메시지 생성: 타입=%s(%d), 데이터=%s", 
             message_get_type_string(type), type, data ? data : "(없음)");
    
    return msg;
}

/**
 * @brief 상태 응답 메시지의 인자들을 채움
 * @details
 * 장비 목록과 예약 정보를 조합하여 클라이언트가 필요로 하는 모든 정보를
 * 포함한 상태 응답 메시지를 구성합니다.
 * 
 * 각 장비마다 다음 정보가 포함됩니다:
 * - 장비 ID, 이름, 타입
 * - 현재 상태 (available/reserved/maintenance)
 * - 예약 종료 시간 (예약된 경우)
 * - 예약자 사용자명 (예약된 경우)
 * 
 * @param msg 채울 메시지 객체
 * @param devices 장비 배열
 * @param count 장비 개수
 * @param rm 리소스 관리자
 * @param rvm 예약 관리자
 * @return 성공 시 true, 실패 시 false
 */
bool message_fill_status_response_args(message_t* msg, const device_t* devices, int count, resource_manager_t* rm, reservation_manager_t* rvm) {
    if (!msg || !devices || !rm || !rvm) return false;
    msg->arg_count = 0;
    for (int i = 0; i < count; i++) {
        int base_idx = i * DEVICE_INFO_ARG_COUNT;
        if (base_idx + (DEVICE_INFO_ARG_COUNT - 1) >= MAX_ARGS) break;
        const char* status_str = message_get_device_status_string(devices[i].status);
        char end_time_str[32] = "0";
        char username_str[MAX_USERNAME_LENGTH] = "";
        if (devices[i].status == DEVICE_RESERVED) {
            reservation_t* res = reservation_get_active_for_device(rvm, rm, devices[i].id);
            if (res) {
                snprintf(end_time_str, sizeof(end_time_str), "%ld", res->end_time);
                strncpy(username_str, res->username, sizeof(username_str) - 1);
                username_str[sizeof(username_str) - 1] = '\0';
            }
        }
        msg->args[base_idx]     = strdup(devices[i].id);
        msg->args[base_idx + 1] = strdup(devices[i].name);
        msg->args[base_idx + 2] = strdup(devices[i].type);
        msg->args[base_idx + 3] = strdup(status_str);
        msg->args[base_idx + 4] = strdup(end_time_str);
        msg->args[base_idx + 5] = strdup(username_str);
        if (!msg->args[base_idx] || !msg->args[base_idx+1] || !msg->args[base_idx+2] || !msg->args[base_idx+3] || !msg->args[base_idx+4] || !msg->args[base_idx+5]) {
            return false;
        }
        msg->arg_count += DEVICE_INFO_ARG_COUNT;
    }
    return true;
}

/**
 * @brief 상태 응답 메시지 생성
 * @details
 * 장비 목록과 예약 정보를 포함한 완전한 상태 응답 메시지를 생성합니다.
 * 이 메시지는 클라이언트가 장비 현황을 파악하는 데 필요한 모든 정보를 포함합니다.
 * 
 * @param devices 장비 배열
 * @param device_count 장비 개수
 * @param resource_manager 리소스 관리자
 * @param reservation_manager 예약 관리자
 * @return 생성된 메시지 객체, 실패 시 NULL
 */
message_t* message_create_status_response(const device_t *devices, int device_count, resource_manager_t* resource_manager, reservation_manager_t* reservation_manager) {
    message_t *message = message_create(MSG_STATUS_RESPONSE, NULL);
    if (!message) return NULL;
    if (!message_fill_status_response_args(message, devices, device_count, resource_manager, reservation_manager)) {
        message_destroy(message);
        return NULL;
    }
    return message;
}

/**
 * @brief 에러 코드를 포함한 에러 메시지 생성
 * @details
 * 에러 코드와 사용자 친화적 메시지를 포함한 에러 응답을 생성합니다.
 * 클라이언트는 이 정보를 바탕으로 적절한 에러 처리를 수행할 수 있습니다.
 * 
 * @param error_code 에러 코드
 * @param error_message 에러 메시지
 * @return 생성된 에러 메시지 객체, 실패 시 NULL
 */
message_t* message_create_error_with_code(error_code_t error_code, const char* error_message) {
    message_t* msg = message_create(MSG_ERROR, error_message);
    if (msg) {
        msg->error_code = error_code;
    }
    return msg;
}

/**
 * @brief 메시지 타입을 문자열로 변환
 * @details
 * 디버깅과 로깅을 위해 메시지 타입을 사람이 읽기 쉬운 문자열로 변환합니다.
 * 
 * @param type 메시지 타입
 * @return 메시지 타입 문자열
 */
const char *message_get_type_string(message_type_t type) {
    switch (type) {
        case MSG_LOGIN: return "LOGIN";
        case MSG_LOGOUT: return "LOGOUT";
        case MSG_RESERVE_REQUEST: return "RESERVE_REQUEST";
        case MSG_RESERVE_RESPONSE: return "RESERVE_RESPONSE";
        case MSG_CANCEL_REQUEST: return "CANCEL_REQUEST";
        case MSG_CANCEL_RESPONSE: return "CANCEL_RESPONSE";
        case MSG_STATUS_REQUEST: return "STATUS_REQUEST";
        case MSG_STATUS_RESPONSE: return "STATUS_RESPONSE";
        case MSG_STATUS_UPDATE: return "STATUS_UPDATE";
        case MSG_TIME_SYNC_REQUEST: return "TIME_SYNC_REQUEST";
        case MSG_TIME_SYNC_RESPONSE: return "TIME_SYNC_RESPONSE";
        case MSG_PING: return "PING";
        case MSG_PONG: return "PONG";
        case MSG_PING_RESPONSE: return "PING_RESPONSE";
        case MSG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/**
 * @brief 메시지 객체 메모리 해제
 * @details
 * 메시지 객체와 모든 동적 할당된 인자들의 메모리를 안전하게 해제합니다.
 * 
 * @param msg 해제할 메시지 객체
 */
void message_destroy(message_t *msg) {
    if (!msg) return;
    for (int i = 0; i < msg->arg_count; i++) {
        if (msg->args[i]) {
            free(msg->args[i]);
            msg->args[i] = NULL;
        }
    }
    free(msg);
}

/**
 * @brief SSL 연결에서 메시지 인자들을 읽어옴
 * @details
 * 네트워크에서 메시지 인자들을 순차적으로 읽어옵니다.
 * 각 인자는 길이(4바이트) + 내용(가변 길이) 형태로 전송됩니다.
 * 
 * @param ssl SSL 연결 객체
 * @param msg 메시지 객체
 * @param arg_count 읽어올 인자 개수
 * @return 성공 시 true, 실패 시 false
 */
static bool message_read_arguments(SSL* ssl, message_t* msg, uint32_t arg_count) {
    for (uint32_t i = 0; i < arg_count; i++) {
        uint32_t arg_len_net;
        if (network_recv(ssl, &arg_len_net, sizeof(arg_len_net)) != sizeof(arg_len_net)) return false;
        uint32_t arg_len = ntohl(arg_len_net);
        if (arg_len >= MAX_ARG_LENGTH) return false;
        msg->args[i] = (char*)malloc(arg_len + 1);
        if (!msg->args[i]) return false;
        if (network_recv(ssl, msg->args[i], arg_len) != (ssize_t)arg_len) {
            free(msg->args[i]);
            msg->args[i] = NULL;
            return false;
        }
        msg->args[i][arg_len] = '\0';
        msg->arg_count++;
    }
    return true;
}

/**
 * @brief SSL 연결에서 메시지 데이터 필드를 읽어옴
 * @details
 * 메시지의 데이터 필드를 읽어옵니다. 데이터 필드도 길이 + 내용 형태로 전송됩니다.
 * 
 * @param ssl SSL 연결 객체
 * @param msg 메시지 객체
 * @return 성공 시 true, 실패 시 false
 */
static bool message_read_data(SSL* ssl, message_t* msg) {
    uint32_t data_len_net;
    if (network_recv(ssl, &data_len_net, sizeof(data_len_net)) != sizeof(data_len_net)) return false;
    uint32_t data_len = ntohl(data_len_net);
    if (data_len > 0) {
        if (data_len >= MAX_MESSAGE_LENGTH) return false;
        if (network_recv(ssl, msg->data, data_len) != (ssize_t)data_len) return false;
        msg->data[data_len] = '\0';
    }
    return true;
}

/**
 * @brief SSL 연결에서 에러 코드를 읽어옴
 * @details
 * MSG_ERROR 타입 메시지에서 에러 코드를 읽어옵니다.
 * 
 * @param ssl SSL 연결 객체
 * @param msg 메시지 객체
 * @return 성공 시 true, 실패 시 false
 */
static bool message_read_error_code(SSL* ssl, message_t* msg) {
    uint32_t error_code_net;
    if (network_recv(ssl, &error_code_net, sizeof(error_code_net)) != sizeof(error_code_net)) return false;
    msg->error_code = ntohl(error_code_net);
    return true;
}

/**
 * @brief SSL 연결에서 메시지를 수신
 * @details
 * 네트워크에서 완전한 메시지를 수신하고 파싱합니다.
 * 
 * 메시지 프로토콜 구조:
 * 1. 메시지 타입 (4바이트)
 * 2. 인자 개수 (4바이트)
 * 3. 에러 코드 (MSG_ERROR 타입인 경우, 4바이트)
 * 4. 인자들 (각각 길이 + 내용)
 * 5. 데이터 필드 (길이 + 내용)
 * 
 * @param ssl SSL 연결 객체
 * @return 수신된 메시지 객체, 실패 시 NULL
 */
message_t* message_receive(SSL* ssl) {
    uint32_t type_net, arg_count_net;
    if (network_recv(ssl, &type_net, sizeof(type_net)) != sizeof(type_net)) return NULL;
    if (network_recv(ssl, &arg_count_net, sizeof(arg_count_net)) != sizeof(arg_count_net)) return NULL;
    
    message_type_t type = ntohl(type_net);
    uint32_t arg_count = ntohl(arg_count_net);
    
    LOG_INFO("Message", "메시지 수신 시작: 타입=%s(%d), 인자수=%d", 
             message_get_type_string(type), type, arg_count);
    
    message_t* msg = message_create(type, NULL);
    if (!msg) return NULL;
    
    if (type == MSG_ERROR) {
        if (!message_read_error_code(ssl, msg)) {
            message_destroy(msg);
            return NULL;
        }
    }
    
    if (arg_count > 0) {
        if (!message_read_arguments(ssl, msg, arg_count)) {
            message_destroy(msg);
            return NULL;
        }
    }
    
    if (!message_read_data(ssl, msg)) {
        message_destroy(msg);
        return NULL;
    }
    
    LOG_INFO("Message", "메시지 수신 완료: 타입=%s(%d), 인자수=%d, 데이터길이=%zu", 
             message_get_type_string(type), type, msg->arg_count, strlen(msg->data));
    
    return msg;
}

/**
 * @brief 장비 상태를 문자열로 변환
 * @details
 * 장비 상태 열거형을 클라이언트가 이해할 수 있는 문자열로 변환합니다.
 * 
 * @param status 장비 상태
 * @return 상태 문자열
 */
const char *message_get_device_status_string(device_status_t status) {
    switch (status) {
        case DEVICE_AVAILABLE: return "available";
        case DEVICE_RESERVED: return "reserved";
        case DEVICE_MAINTENANCE: return "maintenance";
        default: return "unknown";
    }
}

/**
 * @brief 에러 코드를 사용자 친화적 메시지로 변환
 * @details
 * 시스템 에러 코드를 사용자가 이해하기 쉬운 한국어 메시지로 변환합니다.
 * 
 * @param error_code 에러 코드
 * @return 사용자 친화적 에러 메시지
 */
const char* message_get_error_string(error_code_t error_code) {
    switch (error_code) {
        case ERROR_NONE: return "성공";
        case ERROR_SESSION_AUTHENTICATION_FAILED: return "아이디 또는 비밀번호가 틀립니다";
        case ERROR_SESSION_ALREADY_EXISTS: return "이미 로그인된 사용자입니다";
        case ERROR_RESOURCE_IN_USE: return "장비를 사용할 수 없습니다";
        case ERROR_RESOURCE_MAINTENANCE_MODE: return "점검 중인 장비입니다";
        case ERROR_RESERVATION_ALREADY_EXISTS: return "이미 예약된 장비입니다";
        case ERROR_RESERVATION_NOT_FOUND: return "예약을 찾을 수 없습니다";
        case ERROR_RESERVATION_PERMISSION_DENIED: return "본인의 예약이 아닙니다";
        case ERROR_RESERVATION_INVALID_TIME: return "유효하지 않은 예약 시간입니다";
        case ERROR_UNKNOWN: return "서버 내부 오류가 발생했습니다";
        case ERROR_NETWORK_CONNECT_FAILED: return "네트워크 연결 오류가 발생했습니다";
        case ERROR_INVALID_PARAMETER: return "잘못된 요청입니다";
        case ERROR_SESSION_INVALID_STATE: return "세션이 만료되었습니다";
        case ERROR_PERMISSION_DENIED: return "권한이 없습니다";
        default: return "알 수 없는 오류가 발생했습니다";
    }
}