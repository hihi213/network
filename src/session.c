/**
 * @file session.c
 * @brief 세션 관리 모듈 - 클라이언트 연결 세션 관리
 * @details 서버 측 세션 생성/관리 및 클라이언트 세션 정리 기능을 제공합니다.
 */

#include "../include/session.h"  // 세션 관련 헤더 파일 포함
#include "../include/network.h"  // 네트워크 관련 헤더 파일 포함


// [개선] server_session_t 구조체를 해제하기 위한 래퍼 함수
static void session_free_wrapper(void* session) {
    free(session);  // 세션 포인터 메모리 해제
}

/**
 * @brief 세션 매니저를 초기화합니다.
 * @return 성공 시 초기화된 session_manager_t 포인터, 실패 시 NULL
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

    // LOG_INFO("Session", "세션 매니저 초기화 성공");  // 정보 로그 출력
    return manager;  // 초기화된 매니저 반환
}

/**
 * @brief 세션 매니저의 메모리를 정리합니다.
 * @param manager 정리할 session_manager_t 포인터
 */
void session_cleanup_manager(session_manager_t* manager) {
    if (!manager) return;
    utils_cleanup_manager_base(manager, manager->sessions, &manager->mutex);
}

/**
 * @brief 새로운 클라이언트 세션을 생성합니다.
 * @param manager 세션 매니저 포인터
 * @param username 사용자 이름
 * @param client_ip 클라이언트 IP 주소
 * @param client_port 클라이언트 포트 번호
 * @return 생성된 server_session_t 포인터, 실패 시 NULL
 */
server_session_t* session_create(session_manager_t* manager, const char* username, const char* client_ip, int client_port) {
    if (!manager || !username || !client_ip) {  // 유효성 검사
        utils_report_error(ERROR_INVALID_PARAMETER, "Session", "session_create: 잘못된 파라미터");
        return NULL;  // NULL 반환
    }

     pthread_mutex_lock(&manager->mutex);
    // LOG_INFO("Session", "세션 생성 시작: 사용자=%s, IP=%s, 포트=%d", username, client_ip, client_port);

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

    // LOG_INFO("Session", "세션 정보 설정 완료: 사용자=%s, 토큰=%s, 생성시간=%ld",
    //          username, new_session->token, new_session->created_at);

    // [개선] 해시 테이블에 세션 삽입
    if (!utils_hashtable_insert(manager->sessions, username, new_session)) {  // 해시 테이블 삽입 실패 시
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        utils_report_error(ERROR_SESSION_CREATION_FAILED, "Session", "해시 테이블에 세션 삽입 실패: 사용자=%s", username);
        free(new_session);  // 세션 메모리 해제
        return NULL;  // NULL 반환
    }

    // LOG_INFO("Session", "세션 생성 성공: %s", username);  // 정보 로그 출력
    pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
    return new_session;  // 생성된 세션 반환
}

/**
 * @brief 클라이언트 세션을 종료합니다.
 * @param manager 세션 매니저 포인터
 * @param username 종료할 세션의 사용자 이름
 * @return 성공 시 0, 실패 시 -1
 */
int session_close(session_manager_t* manager, const char* username) {
    if (!manager || !username) {  // 유효성 검사
        utils_report_error(ERROR_INVALID_PARAMETER, "Session", "session_close: 잘못된 매개변수");
        return -1;  // 에러 코드 반환
    }

    // LOG_INFO("Session", "세션 종료 시작: 사용자=%s", username);

    pthread_mutex_lock(&manager->mutex);  // 뮤텍스 잠금
    
    // [개선] 해시 테이블에서 바로 삭제. 성공하면 0, 없으면 -1 반환.
    if (utils_hashtable_delete(manager->sessions, username)) {  // 해시 테이블에서 세션 삭제 성공 시
        // LOG_INFO("Session", "세션 종료 성공: %s", username);  // 정보 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return 0;  // 성공 코드 반환
    } else {  // 세션을 찾을 수 없는 경우
        // LOG_WARNING("Session", "세션을 찾을 수 없음: %s", username);  // 경고 로그 출력
        pthread_mutex_unlock(&manager->mutex);  // 뮤텍스 해제
        return -1;  // 에러 코드 반환
    }
}

/**
 * @brief 클라이언트 세션의 메모리를 정리합니다.
 * @param session 정리할 client_session_t 포인터
 */
void session_cleanup_client(client_session_t* session) {
    if (!session) return;  // 세션이 NULL이면 함수 종료

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
}
