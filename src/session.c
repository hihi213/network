#include "../include/session.h"

#include "../include/logger.h"
#include "../include/network.h"


/* 세션 매니저 초기화 */
SessionManager* init_session_manager(void) {
    SessionManager* manager = (SessionManager*)malloc(sizeof(SessionManager));
    if (!manager) {
        LOG_ERROR("Session", "세션 매니저 메모리 할당 실패");
        return NULL;
    }

    manager->session_count = 0;
    if (pthread_mutex_init(&manager->mutex, NULL) != 0) {
        LOG_ERROR("Session", "뮤텍스 초기화 실패");
        free(manager);
        return NULL;
    }

    // sessions 배열 메모리 할당
    ServerSession* sessions = (ServerSession*)malloc(sizeof(ServerSession) * MAX_SESSIONS);
    if (!sessions) {
        LOG_ERROR("Session", "sessions 배열 메모리 할당 실패");
        pthread_mutex_destroy(&manager->mutex);
        free(manager);
        return NULL;
    }
    memset(sessions, 0, sizeof(ServerSession) * MAX_SESSIONS);
    manager->sessions = sessions;

    // 랜덤 시드 초기화
    srand(time(NULL));

    return manager;
}

/* 세션 매니저 정리 */
void cleanup_session_manager(SessionManager* manager) {
    if (!manager) return;
    
    pthread_mutex_destroy(&manager->mutex);
    if (manager->sessions) free(manager->sessions);
    free(manager);
}

ServerSession* create_session(SessionManager* manager, const char* username, const char* client_ip, int client_port) {
    if (!manager || !username || !client_ip) {
        LOG_ERROR("Session", "잘못된 파라미터");
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);

    LOG_INFO("Session", "세션 생성 시작: 사용자=%s, IP=%s, 포트=%d", username, client_ip, client_port);

    // 기존에 동일한 사용자의 세션이 있다면 비활성화
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].username, username) == 0) {
            LOG_INFO("Session", "기존 세션 발견, 비활성화 처리: %s", username);
            manager->sessions[i].state = SESSION_ENDED; 
        }
    }
    
    // 비활성화된 세션을 정리하고 새로운 세션을 위한 공간 확보
    int new_count = 0;
    for (int i = 0; i < manager->session_count; i++) {
        if (manager->sessions[i].state != SESSION_ENDED && manager->sessions[i].state != SESSION_EXPIRED) {
            if (i != new_count) {
                manager->sessions[new_count] = manager->sessions[i];
            }
            new_count++;
        }
    }
    manager->session_count = new_count;


    if (manager->session_count >= MAX_SESSIONS) {
        LOG_ERROR("Session", "세션 최대 개수 초과");
        pthread_mutex_unlock(&manager->mutex);
        return NULL;
    }

    // 새 세션 생성
    ServerSession* session = &manager->sessions[manager->session_count];
    
    // --- [수정된 부분 시작] ---
    // 아래 코드가 메모리 침범을 일으키는 원인이었습니다. 올바르게 수정합니다.
    strncpy(session->username, username, MAX_USERNAME_LENGTH - 1);
    session->username[MAX_USERNAME_LENGTH - 1] = '\0';
    // --- [수정된 부분 끝] ---

    strncpy(session->client_ip, client_ip, MAX_IP_LENGTH - 1);
    session->client_ip[MAX_IP_LENGTH - 1] = '\0';
    session->client_port = client_port;
    session->state = SESSION_ACTIVE;
    session->created_at = time(NULL);
    session->last_activity = session->created_at;

    // 토큰 생성
    snprintf(session->token, MAX_TOKEN_LENGTH, "%s_%ld", username, session->created_at);
    manager->session_count++;

    LOG_INFO("Session", "세션 생성 완료: 사용자=%s, 토큰=%s", username, session->token);
    pthread_mutex_unlock(&manager->mutex);
    return session;
}


int close_session(SessionManager* manager, const char* username) {
    if (!manager || !username) {
        LOG_ERROR("Session", "잘못된 매개변수");
        return -1;
    }

    pthread_mutex_lock(&manager->mutex);
    
    // 세션 찾기
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].username, username) == 0) {
            // 세션 제거 (마지막 세션을 현재 위치로 이동)
            if (i < manager->session_count - 1) {
                manager->sessions[i] = manager->sessions[manager->session_count - 1];
            }
            manager->session_count--;
            pthread_mutex_unlock(&manager->mutex);
            LOG_INFO("Session", "세션 종료: %s", username);
            return 0;
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    LOG_WARNING("Session", "세션을 찾을 수 없음: %s", username);
    return -1;
}


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
