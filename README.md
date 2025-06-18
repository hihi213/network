# 장치 예약 시스템

이 프로젝트는 네트워크 프로그래밍을 이용한 장치 예약 시스템입니다. 클라이언트-서버 아키텍처를 사용하며, SSL/TLS를 통한 보안 통신을 지원합니다.

## 기능

- 사용자 인증 (로그인/로그아웃)
- 장치 목록 조회
- 장치 예약 및 취소
- 실시간 상태 업데이트
- SSL/TLS를 통한 보안 통신
- ncurses 기반의 텍스트 UI
- 로깅 시스템
- 성능 모니터링

## 시스템 제한사항

- 최대 장비 수: 100개
- 최대 예약 수: 1000개
- 최대 세션 수: 100개
- 사용자당 최대 예약 수: 5개
- 세션 타임아웃: 1시간
- 최대 메시지 크기: 4096바이트
- 최대 사용자 이름 길이: 32자
- 최대 장비 ID 길이: 32자
- 최대 장비 이름 길이: 64자

## 요구사항

### 필수 라이브러리
- GCC 컴파일러
- OpenSSL 라이브러리
- ncurses 라이브러리
- pthread 라이브러리

### 설치 방법

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev libncurses5-dev
```

#### macOS
```bash
brew install openssl ncurses
```

## 빌드 방법

```bash
make
```

## 실행 방법

### 서버 실행

```bash
./network <포트>
```

### 클라이언트 실행

```bash
./network <서버 IP> <포트>
```

## 프로젝트 구조

```
.
├── include/           # 헤더 파일들
│   ├── common.h      # 공통 정의
│   ├── message.h     # 메시지 처리
│   ├── network.h     # 네트워크 통신
│   ├── resource.h    # 자원 관리
│   ├── reservation.h # 예약 관리
│   ├── session.h     # 세션 관리
│   ├── ui.h          # 사용자 인터페이스
│   └── utils.h       # 유틸리티(로깅, 해시테이블, 성능 등 통합)
├── src/              # 소스 파일들
│   ├── message.c
│   ├── network.c
│   ├── resource.c
│   ├── reservation.c
│   ├── session.c
│   ├── ui.c
│   ├── utils.c       # 유틸리티(로깅, 해시테이블, 성능 등 통합)
│   ├── client_main.c
│   └── server_main.c
├── certs/            # SSL 인증서
├── Makefile         # 빌드 스크립트
└── README.md        # 프로젝트 설명
```

- **bin/**, **obj/** 디렉토리는 더 이상 사용하지 않습니다. 빌드 결과는 `network` 실행 파일로 생성됩니다.**

## 메시지 프로토콜

### 메시지 타입

- MSG_LOGIN: 로그인 요청/응답
  - 요청: username
  - 응답: success/failure, token

- MSG_LOGOUT: 로그아웃 요청
  - 요청: token

- MSG_RESERVE_REQUEST: 예약 요청
  - 요청: device_id, start_time, end_time, reason
  - 응답: reservation_id, success/failure, message

- MSG_CANCEL_REQUEST: 예약 취소 요청
  - 요청: reservation_id
  - 응답: success/failure, message

- MSG_STATUS_REQUEST: 상태 요청
  - 요청: (없음)
  - 응답: device_list

- MSG_ERROR: 오류 메시지
  - 응답: error_message

## 보안

### SSL/TLS 설정
- 서버 인증서: certs/server.crt
- 서버 개인키: certs/server.key
- 클라이언트 인증서: certs/client.crt
- 클라이언트 개인키: certs/client.key

### 인증서 생성
```bash
# 서버 인증서 생성
openssl req -x509 -newkey rsa:2048 -keyout certs/server.key -out certs/server.crt -days 365 -nodes

# 클라이언트 인증서 생성
openssl req -x509 -newkey rsa:2048 -keyout certs/client.key -out certs/client.crt -days 365 -nodes
```

### 세션 관리
- 토큰 기반 인증
- 32바이트 랜덤 토큰
- 1시간 세션 타임아웃
- 자동 세션 갱신

## 성능 최적화

### 버퍼 관리
- 버퍼 풀 크기: 100
- 배치 처리 크기: 100
- 쓰기 버퍼 크기: 8192바이트
- 읽기 버퍼 크기: 4096바이트

### 스레드 관리
- 클라이언트별 독립 스레드
- 뮤텍스 기반 동기화
- 스레드 안전한 메모리 관리

### I/O 최적화
- 논블로킹 소켓
- 이벤트 기반 I/O
- 배치 처리

## UI 기능

### 메인 화면
- 장비 목록 표시
- 예약 상태 표시
- 시스템 상태 표시
- 명령어 입력 영역

### 메뉴 구조
- 장비 예약
- 예약 취소
- 상태 조회
- 로그아웃

### 상태 표시
- 연결 상태
- 장비 가용성
- 예약 현황
- 시스템 메시지

## 로깅

### 로그 파일
- 위치: server.log
- 최대 크기: 10MB
- 로테이션: 일별

### 로그 레벨
- ERROR: 심각한 오류
- WARNING: 경고 메시지
- INFO: 일반 정보
- DEBUG: 디버깅 정보

### 로그 설정
```bash
# 로그 레벨 설정
export LOG_LEVEL=INFO

# 로그 파일 위치 설정
export LOG_FILE=server.log
```

## 에러 처리

### 일반적인 에러
- 연결 실패
- 인증 실패
- 예약 충돌
- 시스템 오류

### 문제 해결
1. 연결 문제
   - 방화벽 설정 확인
   - 포트 사용 가능 여부 확인
   - SSL 인증서 유효성 확인

2. 인증 문제
   - 사용자 이름 확인
   - 세션 토큰 유효성 확인
   - 인증서 설정 확인

3. 예약 문제
   - 장비 가용성 확인
   - 예약 시간 충돌 확인
   - 사용자 예약 한도 확인

