#ifndef SESSION_H
#define SESSION_H

#include "common.h"
#include "utils.h"


// --- [수정] 전방 선언 추가 ---
// 실제 정의는 다른 헤더파일에 있지만, 여기서는 포인터만 사용하므로
// 컴파일러에게 해당 이름의 구조체가 존재한다는 것만 알려줍니다.
typedef struct ssl_st SSL;
struct SSLHandler;
// ----------------------------

#define MAX_USERNAME_LENGTH 32
#define MAX_IP_LENGTH 46 // IPv6 주소를 고려
#define MAX_TOKEN_LENGTH 128
#define SESSION_TIMEOUT 300 // 세션 타임아웃 (초)

// 서버 측에서 관리하는 세션의 상태
typedef enum {
    SESSION_ACTIVE,
    SESSION_EXPIRED,
    SESSION_ENDED
} ServerSessionState;

// 클라이언트 측에서 관리하는 세션의 상태
typedef enum {
    SESSION_DISCONNECTED,
    SESSION_CONNECTING,
    SESSION_LOGGED_IN
} SessionState;


// 서버가 관리하는 사용자 세션 정보
typedef struct {
    char username[MAX_USERNAME_LENGTH];
    char client_ip[MAX_IP_LENGTH];
    int client_port;
    char token[MAX_TOKEN_LENGTH];
    ServerSessionState state;
    time_t created_at;
    time_t last_activity;
} ServerSession;

// 클라이언트 프로그램이 서버와의 연결 정보를 담는 구조체
typedef struct {
    int socket_fd;
    SSL* ssl;
    struct SSLHandler* ssl_handler; // [수정] struct 키워드 추가
    char server_ip[MAX_IP_LENGTH];
    int server_port;
    SessionState state;
    char username[MAX_USERNAME_LENGTH];
    time_t last_activity;
} ClientSession;

// 서버의 세션 매니저
typedef struct {
    HashTable* sessions;  // Key: username, Value: ServerSession*
    pthread_mutex_t mutex;
} SessionManager;


/* 함수 프로토타입 */

// 서버용 세션 관리 함수
SessionManager* init_session_manager(void);
void cleanup_session_manager(SessionManager* manager);
ServerSession* create_session(SessionManager* manager, const char* username, const char* client_ip, int client_port);
int close_session(SessionManager* manager, const char* username);

// 클라이언트용 세션 정리 함수
void cleanup_client_session(ClientSession* session);

#endif // SESSION_H