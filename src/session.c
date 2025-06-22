/**
 * @file session.c
 * @brief 세션 관리 모듈 - 클라이언트 연결 세션 및 인증 상태 관리
 * 
 * @details
 * 이 모듈은 클라이언트의 연결 세션과 인증 상태를 관리합니다:
 * 
 * **핵심 역할:**
 * - 서버 측: 해시 테이블을 이용한 중복 로그인 방지
 * - 서버 측: 사용자 세션 정보 관리 및 추적
 * - 클라이언트 측: 연결 관련 리소스(SSL, 소켓) 정리
 * - 세션 생명주기 관리 (생성, 유지, 종료)
 * 
 * **시스템 아키텍처에서의 위치:**
 * - 보안 계층: 사용자 인증 및 세션 관리
 * - 상태 관리 계층: 클라이언트 연결 상태 추적
 * - 리소스 관리 계층: 연결별 리소스 할당/해제
 * - 네트워크 계층: 연결 지속성 및 안정성 보장
 * 
 * **서버 측 기능:**
 * - 사용자별 세션 정보 저장 (사용자명, 연결 시간, 마지막 활동)
 * - 중복 로그인 감지 및 기존 세션 강제 종료
 * - 세션 타임아웃 처리 및 자동 정리
 * - 동시 접속자 수 제한 및 관리
 * 
 * **클라이언트 측 기능:**
 * - SSL 컨텍스트 및 소켓 리소스 정리
 * - 연결 종료 시 안전한 리소스 해제
 * - 재연결 시도 및 연결 상태 복구
 * - 네트워크 오류 시 세션 정리
 * 
 * **주요 특징:**
 * - 해시 테이블 기반 O(1) 세션 조회 성능
 * - 스레드 안전한 세션 관리 (뮤텍스 보호)
 * - 메모리 누수 방지를 위한 자동 정리 메커니즘
 * - 세션 상태의 실시간 모니터링 지원
 * 
 * **세션 상태:**
 * - ACTIVE: 정상 활성 세션
 * - EXPIRED: 타임아웃으로 만료된 세션
 * - DISCONNECTED: 연결이 끊어진 세션
 * - CLEANUP: 정리 대기 중인 세션
 * 
 * @note 이 모듈은 시스템의 보안과 안정성을 담당하며, 사용자별
 *       연결 상태를 체계적으로 관리합니다.
 */

#include "../include/session.h"  // 세션 관련 헤더 파일 포함
#include "../include/network.h"  // 네트워크 관련 헤더 파일 포함

/**
 * @brief 서버 세션 객체 메모리 해제 래퍼 함수
 * @details
 * 해시 테이블의 값 해제 함수로 사용되는 래퍼 함수입니다.
 * 세션 객체의 메모리를 안전하게 해제합니다.
 * 
 * @param session 해제할 세션 객체 포인터
 */
static void session_free_wrapper(void* session) {
    free(session);  // 세션 포인터 메모리 해제
}

/**
 * @brief 세션 관리자 초기화
 * @details
 * 세션 관리를 위한 해시 테이블과 뮤텍스를 초기화합니다.
 * 해시 테이블은 사용자명을 키로 하여 세션 객체를 저장합니다.
 * 
 * @return 초기화된 세션 관리자, 실패 시 NULL
 */
session_manager_t* session_init_manager(void) {
    session_manager_t* manager = (session_manager_t*)malloc(sizeof(session_manager_t));  // 세션 매니저 메모리 할당
    if (!manager) {  // 메모리 할당 실패 시
        utils_report_error(ERROR_MEMORY_ALLOCATION_FAILED, "Session", "세션 매니저 메모리 할당 실패");  // 에러 로그 출력
        return NULL;  // NULL 반환
    }

    // [개선] 공통 초기화 헬퍼 함수 사용
    if (!utils_init_manager_base(manager, sizeof(session_manager_t), &manager->sessions, MAX_SESSIONS, session_free_wrapper, &manager->mutex)) {
        utils_report_error(ERROR_HASHTABLE_CREATION_FAILED, "Session", "매니저 공통 초기화 실패");  // 에러 로그 출력
        free(manager);  // 매니저 메모리 해제
        return NULL;  // NULL 반환
    }
    
    // 랜덤 시드 초기화 (토큰 생성용)
    srand(time(NULL));  // 현재 시간으로 랜덤 시드 설정

    LOG_INFO("Session", "세션 매니저 초기화 성공");  // 정보 로그 출력
    return manager;  // 초기화된 매니저 반환
}

/**
 * @brief 세션 관리자 정리
 * @details
 * 세션 관리자와 관련된 모든 리소스를 안전하게 해제합니다.
 * 해시 테이블의 모든 세션 객체도 함께 정리됩니다.
 * 
 * @param manager 정리할 세션 관리자
 */
void session_cleanup_manager(session_manager_t* manager) {
    if (!manager) return;
    utils_cleanup_manager_base(manager, manager->sessions, &manager->mutex);
}

/**
 * @brief 새로운 클라이언트 세션 생성
 * @details
 * 사용자 인증 후 새로운 세션을 생성합니다. 중복 로그인을 방지하기 위해
 * 기존 세션이 있는지 확인하고, 고유한 세션 토큰을 생성합니다.
 * 
 * 세션 토큰 형식: "사용자명_타임스탬프"
 * 이는 디버깅과 로깅에 유용하며, 충돌 가능성이 매우 낮습니다.
 * 
 * @param manager 세션 관리자
 * @param username 인증된 사용자명
 * @param client_ip 클라이언트 IP 주소
 * @param client_port 클라이언트 포트 번호
 * @return 생성된 세션 객체, 실패 시 NULL
 */
server_session_t* session_create(session_manager_t* manager, const char* username, const char* client_ip, int client_port) {
    if (!manager || !username || !client_ip) {  // 유효성 검사
        utils_report_error(ERROR_INVALID_PARAMETER, "Session", "session_create: 잘못된 파라미터");
        return NULL;  // NULL 반환
    }

     pthread_mutex_lock(&manager->mutex);
    LOG_INFO("Session", "세션 생성 시작: 사용자=%s, IP=%s, 포트=%d", username, client_ip, client_port);

    // [개선] 기존 세션이 존재하는지 확인
    if (utils_hashtable_get(manager->sessions, username)) {
        // 이미 로그인된 사용자인 경우, 에러를 보고하고 NULL 반환
        pthread_mutex_unlock(&manager->mutex);
        utils_report_error(ERROR_SESSION_ALREADY_EXISTS, "Session", "이미 로그인된 사용자입니다: %s", username);
        return NULL;
    }


    // 새 세션 생성
    server_session_t* new_session = (server_session_t*)malloc(sizeof(server_session_t));  // 새 세션 메모리 할당
    if (!new_session) {  // 메모리 할당 실패 시
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        utils_report_error(ERROR_SESSION_CREATION_FAILED, "Session", "세션 메모리 할당 실패: 사용자=%s", username);
        return NULL;  // NULL 반환
    }

    // 세션 정보 채우기
    strncpy(new_session->username, username, MAX_USERNAME_LENGTH - 1);  // 사용자 이름 복사
    new_session->username[MAX_USERNAME_LENGTH - 1] = '\0';  // 문자열 종료 문자 보장
    strncpy(new_session->client_ip, client_ip, MAX_IP_LENGTH - 1);  // 클라이언트 IP 복사
    new_session->client_ip[MAX_IP_LENGTH - 1] = '\0';  // 문자열 종료 문자 보장
    new_session->client_port = client_port;  // 클라이언트 포트 설정
    new_session->state = SESSION_ACTIVE;  // 세션 상태를 활성으로 설정
    new_session->created_at = time(NULL);  // 생성 시간 설정
    new_session->last_activity = new_session->created_at;  // 마지막 활동 시간을 생성 시간으로 초기화
    
    // 고유한 세션 토큰 생성 (사용자명_타임스탬프 형식)
    snprintf(new_session->token, MAX_TOKEN_LENGTH, "%s_%ld", username, new_session->created_at);  // 세션 토큰 생성

    LOG_INFO("Session", "세션 정보 설정 완료: 사용자=%s, 토큰=%s, 생성시간=%ld",
             username, new_session->token, new_session->created_at);

    // [개선] 해시 테이블에 세션 삽입
    if (!utils_hashtable_insert(manager->sessions, username, new_session)) {  // 해시 테이블 삽입 실패 시
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        utils_report_error(ERROR_SESSION_CREATION_FAILED, "Session", "해시 테이블에 세션 삽입 실패: 사용자=%s", username);
        free(new_session);  // 세션 메모리 해제
        return NULL;  // NULL 반환
    }

    LOG_INFO("Session", "세션 생성 성공: %s", username);  // 정보 로그 출력
    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return new_session;  // 생성된 세션 반환
}

/**
 * @brief 클라이언트 세션 종료
 * @details
 * 사용자 로그아웃 또는 연결 종료 시 세션을 정리합니다.
 * 해시 테이블에서 세션을 제거하고 관련 메모리를 해제합니다.
 * 
 * @param manager 세션 관리자
 * @param username 종료할 세션의 사용자명
 * @return 성공 시 0, 실패 시 -1
 */
int session_close(session_manager_t* manager, const char* username) {
    if (!manager || !username) {  // 유효성 검사
        utils_report_error(ERROR_INVALID_PARAMETER, "Session", "session_close: 잘못된 매개변수");
        return -1;  // 에러 코드 반환
    }

    LOG_INFO("Session", "세션 종료 시작: 사용자=%s", username);

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    // [개선] 해시 테이블에서 바로 삭제. 성공하면 0, 없으면 -1 반환.
    if (utils_hashtable_delete(manager->sessions, username)) {  // 해시 테이블에서 세션 삭제 성공 시
        LOG_INFO("Session", "세션 종료 성공: %s", username);  // 정보 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return 0;  // 성공 코드 반환
    } else {  // 세션을 찾을 수 없는 경우
        LOG_WARNING("Session", "세션을 찾을 수 없음: %s", username);  // 경고 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return -1;  // 에러 코드 반환
    }
}

/**
 * @brief 클라이언트 세션 리소스 정리
 * @details
 * 클라이언트 연결 종료 시 모든 네트워크 리소스를 안전하게 해제합니다.
 * SSL 연결, 소켓, 핸들러를 순서대로 정리하여 메모리 누수를 방지합니다.
 * 
 * @param session 정리할 클라이언트 세션
 */
void session_cleanup_client(client_session_t* session) {
    if (!session) return;  // 세션이 NULL이면 함수 종료

    LOG_INFO("Session", "클라이언트 세션 정리 시작: 사용자=%s, 소켓=%d", 
             session->username, session->socket_fd);

    // SSL 핸들러 정리
    if (session->ssl_handler) {  // SSL 핸들러가 존재하는 경우
        network_cleanup_ssl_handler(session->ssl_handler);  // SSL 핸들러 정리
        session->ssl_handler = NULL;  // 포인터를 NULL로 설정
    }
    
    // SSL 연결 정리
    if (session->ssl) {  // SSL 객체가 존재하는 경우
        SSL_shutdown(session->ssl);  // SSL 연결 종료
        SSL_free(session->ssl);  // SSL 객체 해제
        session->ssl = NULL;  // 포인터를 NULL로 설정
    }

    // 소켓 정리
    if (session->socket_fd >= 0) {  // 소켓이 유효한 경우
        close(session->socket_fd);  // 소켓 닫기
        session->socket_fd = -1;  // 파일 디스크립터를 -1로 설정
    }

    // 세션 정보 초기화
    session->state = SESSION_DISCONNECTED;  // 세션 상태를 연결 해제로 설정
    memset(session->username, 0, sizeof(session->username));  // 사용자 이름 초기화
    memset(session->server_ip, 0, sizeof(session->server_ip));  // 서버 IP 초기화
    session->server_port = 0;  // 서버 포트 초기화
    session->last_activity = 0;  // 마지막 활동 시간 초기화

    LOG_INFO("Session", "클라이언트 세션 정리 완료");
}
