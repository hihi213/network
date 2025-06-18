#include "../include/session.h"
#include "../include/network.h"


// [개선] ServerSession 구조체를 해제하기 위한 래퍼 함수
static void free_session_wrapper(void* session) {
    free(session);
}

/* 세션 매니저 초기화 */
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
    
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Session", "뮤텍스 초기화 실패");
        ht_destroy(manager->sessions);
        free(manager);
        return NULL;
    }
    
    srand(time(NULL));
    return manager;
}

/* 세션 매니저 정리 */
void cleanup_session_manager(SessionManager* manager) {
    if (!manager) return;
    
    // [개선] 해시 테이블의 모든 자원을 해제
    ht_destroy(manager->sessions);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/* 세션 생성 */
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


/* 세션 종료 */
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

// ... (cleanup_client_session 함수는 변경 없음) ...
void cleanup_client_session(ClientSession* session) {
    if (!session) return;

    if (session->ssl_handler) {
        cleanup_ssl_handler(session->ssl_handler);
        session->ssl_handler = NULL;
    }
    
    if (session->ssl) {
        SSL_shutdown(session->ssl);
        SSL_free(session->ssl);
        session->ssl = NULL;
    }

    if (session->socket_fd >= 0) {
        close(session->socket_fd);
        session->socket_fd = -1;
    }

    session->state = SESSION_DISCONNECTED;
    memset(session->username, 0, sizeof(session->username));
    memset(session->server_ip, 0, sizeof(session->server_ip));
    session->server_port = 0;
    session->last_activity = 0;
}
