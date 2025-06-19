/**
 * @file session.c
 * @brief 세션 관리 모듈 - 클라이언트 연결 세션 관리
 * @details 서버 측 세션 생성/관리 및 클라이언트 세션 정리 기능을 제공합니다.
 */

#include "../include/session.h"
#include "../include/network.h"


// [개선] ServerSession 구조체를 해제하기 위한 래퍼 함수
static void free_session_wrapper(void* session) {
    free(session);
}

/**
 * @brief 세션 매니저를 초기화합니다.
 * @return 성공 시 초기화된 SessionManager 포인터, 실패 시 NULL
 */
SessionManager* init_session_manager(void) {
    SessionManager* manager = (SessionManager*)malloc(sizeof(SessionManager));
    if (!manager) {
        LOG_ERROR("Session", "세션 매니저 메모리 할당 실패");
        return NULL;
    }

    // [개선] 해시 테이블 초기화. 최대 세션 수(MAX_SESSIONS)를 크기로 지정.
    manager->sessions = ht_create(MAX_SESSIONS, free_session_wrapper);
    if(!manager->sessions){
        LOG_ERROR("Session", "세션 해시 테이블 생성 실패");
        free(manager);
        return NULL;
    }
    
    // 뮤텍스 초기화
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Session", "뮤텍스 초기화 실패");
        ht_destroy(manager->sessions);
        free(manager);
        return NULL;
    }
    
    // 랜덤 시드 초기화 (토큰 생성용)
    srand(time(NULL));
    return manager;
}

/**
 * @brief 세션 매니저의 메모리를 정리합니다.
 * @param manager 정리할 SessionManager 포인터
 */
void cleanup_session_manager(SessionManager* manager) {
    if (!manager) return;
    
    // [개선] 해시 테이블의 모든 자원을 해제
    ht_destroy(manager->sessions);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * @brief 새로운 클라이언트 세션을 생성합니다.
 * @param manager 세션 매니저 포인터
 * @param username 사용자 이름
 * @param client_ip 클라이언트 IP 주소
 * @param client_port 클라이언트 포트 번호
 * @return 생성된 ServerSession 포인터, 실패 시 NULL
 */
ServerSession* create_session(SessionManager* manager, const char* username, const char* client_ip, int client_port) {
    if (!manager || !username || !client_ip) {
        LOG_ERROR("Session", "잘못된 파라미터");
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);
    LOG_INFO("Session", "세션 생성 시작: 사용자=%s, IP=%s", username, client_ip);

    // [개선] 로직이 매우 단순해짐.
    // 1. 새 세션 객체를 동적 할당
    ServerSession* session = (ServerSession*)malloc(sizeof(ServerSession));
    if(!session){
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }

    // 2. 세션 정보 채우기
    strncpy(session->username, username, MAX_USERNAME_LENGTH - 1);
    session->username[MAX_USERNAME_LENGTH - 1] = '\0';
    strncpy(session->client_ip, client_ip, MAX_IP_LENGTH - 1);
    session->client_ip[MAX_IP_LENGTH - 1] = '\0';
    session->client_port = client_port;
    session->state = SESSION_ACTIVE;
    session->created_at = time(NULL);
    session->last_activity = session->created_at;
    
    // 고유한 세션 토큰 생성 (사용자명_타임스탬프 형식)
    snprintf(session->token, MAX_TOKEN_LENGTH, "%s_%ld", username, session->created_at);

    // 3. 해시 테이블에 삽입 (키: username). 이미 키가 존재하면 자동으로 덮어씀.
    if (!ht_insert(manager->sessions, username, session)) {
        free(session); // 삽입 실패 시 메모리 해제
        session = NULL;
    } else {
        LOG_INFO("Session", "세션 생성/갱신 완료: 사용자=%s", username);
    }
    
    pthread_mutex_unlock(&manager->mutex);
    return session;
}

/**
 * @brief 클라이언트 세션을 종료합니다.
 * @param manager 세션 매니저 포인터
 * @param username 종료할 세션의 사용자 이름
 * @return 성공 시 0, 실패 시 -1
 */
int close_session(SessionManager* manager, const char* username) {
    if (!manager || !username) {
        LOG_ERROR("Session", "잘못된 매개변수");
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    
    // [개선] 해시 테이블에서 바로 삭제. 성공하면 0, 없으면 -1 반환.
    if (ht_delete(manager->sessions, username)) {
        LOG_INFO("Session", "세션 종료: %s", username);
        pthread_mutex_unlock(&manager->mutex);
        return 0;
    } else {
        LOG_WARNING("Session", "세션을 찾을 수 없음: %s", username);
        pthread_mutex_unlock(&manager->mutex);
        return -1;
    }
}

/**
 * @brief 클라이언트 세션의 메모리를 정리합니다.
 * @param session 정리할 ClientSession 포인터
 */
void cleanup_client_session(ClientSession* session) {
    if (!session) return;

    // SSL 핸들러 정리
    if (session->ssl_handler) {
        cleanup_ssl_handler(session->ssl_handler);
        session->ssl_handler = NULL;
    }
    
    // SSL 연결 정리
    if (session->ssl) {
        SSL_shutdown(session->ssl);
        SSL_free(session->ssl);
        session->ssl = NULL;
    }

    // 소켓 정리
    if (session->socket_fd >= 0) {
        close(session->socket_fd);
        session->socket_fd = -1;
    }

    // 세션 정보 초기화
    session->state = SESSION_DISCONNECTED;
    memset(session->username, 0, sizeof(session->username));
    memset(session->server_ip, 0, sizeof(session->server_ip));
    session->server_port = 0;
    session->last_activity = 0;
}
