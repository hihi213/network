#include "../include/session.h"

#include "../include/logger.h"
#include "../include/network.h"


// 함수 프로토타입 선언 추가
static bool is_session_timed_out(const ServerSession* session);

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

/* 세션 검증 */
ServerSession* validate_session(SessionManager* manager, const char* token) {
    if (!manager || !token) {
        LOG_ERROR("Session", "잘못된 파라미터");
        return NULL;
    }

    pthread_mutex_lock(&manager->mutex);

    time_t current_time = time(NULL);
    ServerSession* session = NULL;

    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].token, token) == 0) {
            if (manager->sessions[i].expiry_time > current_time) {
                session = &manager->sessions[i];
                session->last_activity = current_time;
                break;
            } else {
                // 만료된 세션 제거
                manager->sessions[i].token[0] = '\0';
                manager->sessions[i].expiry_time = 0;
                LOG_INFO("Session", "만료된 세션 제거: 토큰=%s", token);
            }
        }
    }

    pthread_mutex_unlock(&manager->mutex);
    return session;
}

/* 세션 갱신 */
bool refresh_session(SessionManager* manager, const char* token) {
    if (!manager || !token) {
        LOG_ERROR("Session", "잘못된 파라미터");
        return false;
    }

    LOG_INFO("Session", "세션 갱신 시작: 토큰=%s", token);

    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].token, token) == 0) {
            manager->sessions[i].last_activity = time(NULL);
            LOG_INFO("Session", "세션 갱신 완료");
            return true;
        }
    }

    LOG_ERROR("Session", "세션을 찾을 수 없음");
    return false;
}



/* 사용자의 세션 조회 (수정된 버전) */
ServerSession* get_user_session(SessionManager* manager, const char* username) {
    if (!manager || !username || username[0] == '\0') {
        return NULL;
    }

    // 뮤텍스는 이 함수를 호출하는 쪽에서 관리합니다. (client_thread_func에서 lock/unlock)

    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].username, username) == 0 && manager->sessions[i].state == SESSION_ACTIVE) {
            return &manager->sessions[i];
        }
    }

    return NULL;
}

/* 만료된 세션 정리 */
void cleanup_expired_sessions(SessionManager* manager) {
    if (!manager) {
        LOG_ERROR("Session", "잘못된 파라미터");
        return;
    }

    LOG_INFO("Session", "만료된 세션 정리 시작");
    // time_t current_time = time(NULL); // <-- 사용하지 않으므로 이 줄을 삭제합니다.
    int removed_count = 0;

    for (int i = 0; i < manager->session_count; i++) {
        if (is_session_timed_out(&manager->sessions[i])) {
            manager->sessions[i].state = SESSION_EXPIRED;
            removed_count++;
        }
    }

    LOG_INFO("Session", "만료된 세션 정리 완료: %d개 정리됨", removed_count);
}

/* 세션 타임아웃 확인 */
bool is_session_timed_out(const ServerSession* session) {
    if (!session || session->state != SESSION_ACTIVE) return false;

    // 변수 없이 바로 time(NULL)을 사용
    return (time(NULL) - session->last_activity) > SESSION_TIMEOUT;
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

bool is_valid_session_username(const char* username) {
    if (!username) return false;

    // 사용자 이름 길이 검사
    size_t len = strlen(username);
    if (len == 0 || len >= MAX_USERNAME_LENGTH) {
        return false;
    }

    // 사용자 이름 문자 검사 (알파벳, 숫자, 언더스코어만 허용)
    for (size_t i = 0; i < len; i++) {
        if (!isalnum(username[i]) && username[i] != '_') {
            return false;
        }
    }

    return true;
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

// 누락된 함수 기본 구현 추가
int init_client_session(ClientSession* session) {
    if (!session) return -1;
    memset(session, 0, sizeof(ClientSession));
    session->socket_fd = -1;
    session->state = SESSION_DISCONNECTED;
    return 0;
}

int update_session_state(ClientSession* session, SessionState state) {
    if (!session) return -1;
    session->state = state;
    return 0;
}

int save_session_credentials(ClientSession* session, const char* username) {
    if (!session || !username) return -1;
    strncpy(session->username, username, MAX_USERNAME_LENGTH - 1);
    session->username[MAX_USERNAME_LENGTH - 1] = '\0';
    return 0;
}

int restore_session_credentials(ClientSession* session) {
    (void)session;  // 사용되지 않는 매개변수 경고 제거
    return 0;
}

bool should_reconnect(const ClientSession* session) {
    if (!session) return false;
    return session->state == SESSION_DISCONNECTED;
}

void update_client_session_activity(ClientSession* session) {
    if (!session) return;
    session->last_activity = time(NULL);
}

time_t get_session_last_activity(const ClientSession* session) {
    if (!session) return 0;
    return session->last_activity;
}

ServerSession* get_session(SessionManager* manager, const char* username) {
    if (!manager || !username) return NULL;
    pthread_mutex_lock(&manager->mutex);
    for (int i = 0; i < manager->session_count; i++) {
        if (strcmp(manager->sessions[i].username, username) == 0) {
            pthread_mutex_unlock(&manager->mutex);
            return &manager->sessions[i];
        }
    }
    pthread_mutex_unlock(&manager->mutex);
    return NULL;
}

bool is_session_connected(const ClientSession* session) {
    if (!session) return false;
    return session->state != SESSION_DISCONNECTED;
}

bool is_session_logged_in(const ClientSession* session) {
    if (!session) return false;
    return session->state == SESSION_LOGGED_IN;
}

bool is_session_expired(const ServerSession* session) {
    if (!session) return false;
    return (time(NULL) - session->last_activity) > SESSION_TIMEOUT;
}

bool is_valid_session_ip(const char* ip) {
    if (!ip) return false;
    // 간단한 IPv4 유효성 검사
    int dots = 0;
    for (const char* p = ip; *p; ++p) {
        if (*p == '.') dots++;
        else if (!isdigit(*p)) return false;
    }
    return dots == 3;
}

bool is_valid_session_port(int port) {
    return port > 0 && port < 65536;
} 