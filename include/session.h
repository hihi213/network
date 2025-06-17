#ifndef SESSION_H
#define SESSION_H


#include "network.h"  // SSLHandler 타입을 위해 추가
#include "common.h"

/* 세션 상태 열거형 */
typedef enum {
    SESSION_DISCONNECTED,
    SESSION_CONNECTING,
    SESSION_LOGGED_IN,
    SESSION_ACTIVE,
    SESSION_ENDED,
    SESSION_EXPIRED
} SessionState;

/* 사용자 정보 구조체 */
typedef struct {
    char username[MAX_USERNAME_LENGTH];
    char password[MAX_PASSWORD_LENGTH]; // 실제 시스템에서는 해시값이어야 함
    time_t last_login;
    bool is_active;
} User;

/* 클라이언트 세션 구조체 */
typedef struct {
    SSL* ssl;
    SSLHandler* ssl_handler;
    int socket_fd;
    char username[MAX_USERNAME_LENGTH];
    char server_ip[MAX_IP_LENGTH];
    int server_port;
    SessionState state;
    time_t last_activity;
} ClientSession;

/* 서버 세션 구조체 */
typedef struct {
    char token[MAX_TOKEN_LENGTH];
    char username[MAX_USERNAME_LENGTH];
    char client_ip[MAX_IP_LENGTH];
    int client_port;
    time_t created_at;
    time_t last_activity;
    time_t expiry_time;
    SessionState state;
} ServerSession;

/* 세션 관리자 구조체 */
typedef struct {
    ServerSession* sessions;
    int session_count;
    pthread_mutex_t mutex;
} SessionManager;

/* 클라이언트 세션 관리 함수 */
int init_client_session(ClientSession* session);
void cleanup_client_session(ClientSession* session);
int update_session_state(ClientSession* session, SessionState state);
void update_client_session_activity(ClientSession* session);
bool is_session_connected(const ClientSession* session);
bool is_session_logged_in(const ClientSession* session);

/* 서버 세션 관리 함수 */
SessionManager* init_session_manager(void);
void cleanup_session_manager(SessionManager* manager);
ServerSession* create_session(SessionManager* manager, const char* username, const char* ip, int port);
int close_session(SessionManager* manager, const char* username);
void cleanup_expired_sessions(SessionManager* manager);
ServerSession* get_session(SessionManager* manager, const char* username);
bool is_session_expired(const ServerSession* session);

#endif /* SESSION_H */