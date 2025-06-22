# 개발자 가이드

이 문서는 프로젝트의 내부 구조, 프로토콜, 성능 최적화 등 개발자를 위한 기술적인 정보를 제공합니다.

## 프로젝트 구조

```
.
├── include/           # 헤더 파일
│   ├── common.h      # 공통 정의
│   ├── message.h     # 메시지 처리
│   ├── network.h     # 네트워크 통신
│   ├── resource.h    # 자원 관리
│   ├── reservation.h # 예약 관리
│   ├── session.h     # 세션 관리
│   ├── ui.h          # 사용자 인터페이스
│   └── utils.h       # 유틸리티 (로깅, 해시테이블 등)
├── src/               # 소스 파일
│   ├── client_main.c
│   └── server_main.c
│   // ... 기타 소스 파일
├── bin/               # 실행 파일
├── obj/               # 오브젝트 파일
├── certs/             # SSL 인증서
├── logs/              # 로그 파일
├── Makefile
├── install.sh         # 자동 설치 스크립트
└── docs/              # 문서
    ├── INSTALL.md
    ├── USAGE.md
    ├── DEVELOPMENT.md
    └── TROUBLESHOOTING.md
```

## 메시지 프로토콜

클라이언트와 서버는 TCP/IP 기반의 고정 길이 헤더와 가변 길이 페이로드를 갖는 메시지를 주고받습니다.

### 메시지 구조
- **Header (8 bytes)**:
  - `type` (4 bytes): 메시지 타입 (e.g., `MSG_LOGIN`)
  - `length` (4 bytes): 뒤따르는 페이로드의 길이
- **Payload (Variable length)**:
  - 데이터 필드는 JSON 형식으로 직렬화됩니다.

### 메시지 타입

- `MSG_LOGIN_REQUEST`: 로그인 요청
  - Payload: `{"username": "...", "password": "..."}`
- `MSG_LOGIN_RESPONSE`: 로그인 응답
  - Payload: `{"success": true/false, "token": "...", "message": "..."}`

- `MSG_LOGOUT_REQUEST`: 로그아웃 요청
  - Payload: `{"token": "..."}`
- `MSG_LOGOUT_RESPONSE`: 로그아웃 응답

- `MSG_RESERVE_REQUEST`: 예약 요청
  - Payload: `{"token": "...", "device_id": "...", "start_time": ..., "end_time": ...}`
- `MSG_RESERVE_RESPONSE`: 예약 응답
  - Payload: `{"success": true/false, "reservation_id": ..., "message": "..."}`

- `MSG_CANCEL_REQUEST`: 예약 취소 요청
  - Payload: `{"token": "...", "reservation_id": ...}`
- `MSG_CANCEL_RESPONSE`: 예약 취소 응답
  - Payload: `{"success": true/false, "message": "..."}`

- `MSG_STATUS_REQUEST`: 장비 상태 요청
  - Payload: `{"token": "..."}`
- `MSG_STATUS_RESPONSE`: 장비 상태 응답
  - Payload: `{"devices": [{"id": "...", "name": "...", "available": true/false}, ...]}`

- `MSG_ERROR`: 오류 응답
  - Payload: `{"error_message": "..."}`

## 성능 최적화

### 버퍼 관리
- **버퍼 풀링**: 메시지 버퍼를 미리 할당하고 재사용하여 `malloc`/`free` 호출 오버헤드를 줄입니다.
- **배치 처리**: 여러 개의 작은 메시지를 모아 한 번에 처리하여 I/O 호출 횟수를 최소화합니다.

### 스레드 모델
- **클라이언트별 스레드**: 각 클라이언트 연결은 독립적인 스레드에서 처리되어 다른 클라이언트에 영향을 주지 않습니다.
- **뮤텍스 기반 동기화**: 공유 자원(예: 장비 목록, 예약 정보)에 대한 접근은 뮤텍스를 사용하여 스레드 안전성을 보장합니다.

### I/O 최적화
- **논블로킹 소켓**: `fcntl`을 사용하여 소켓을 논블로킹 모드로 설정하고, `epoll` (Linux) 또는 `kqueue` (macOS)을 이용한 이벤트 기반 I/O 모델을 사용합니다.

## 로깅 시스템

### 로그 파일
- **서버 로그**: `logs/server.log`
- **클라이언트 로그**: `logs/client.log`
- **로그 로테이션**: 파일 크기가 10MB에 도달하면 자동으로 새 파일에 로깅을 시작합니다. (예: `server.log.1`)

### 로그 레벨
- `ERROR`: 복구 불가능한 심각한 오류
- `WARNING`: 잠재적 문제가 될 수 있는 경고
- `INFO`: 주요 이벤트 및 상태 변경 정보
- `DEBUG`: 개발 및 디버깅을 위한 상세 정보

### 설정 방법 (환경 변수)
```bash
# 로그 레벨 설정 (기본값: INFO)
export LOG_LEVEL=DEBUG

# 로그 파일 경로 설정
export LOG_FILE_PATH=./my_logs/
```

## 빌드 모드

### 디버그 빌드
디버깅 정보(`-g`)를 포함하고 `DEBUG` 매크로를 활성화하여 빌드합니다.

```bash
make clean
make CFLAGS="-Wall -Wextra -g -DDEBUG"
```

### 릴리즈 빌드
최적화(`-O2`)를 적용하고 `NDEBUG` 매크로를 활성화하여 빌드합니다.

```bash
make clean
make CFLAGS="-Wall -Wextra -O2 -DNDEBUG"
``` 