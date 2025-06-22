# 장치 예약 시스템

이 프로젝트는 네트워크 프로그래밍을 이용한 장치 예약 시스템입니다. 클라이언트-서버 아키텍처를 사용하며, SSL/TLS를 통한 보안 통신을 지원합니다.

**지원 플랫폼**: Linux, macOS

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

## 빠른 시작 (자동 설치)

### 🚀 원클릭 설치 (권장)

리눅스에서 자동으로 모든 의존성을 설치하고 빌드합니다:

```bash
# 프로젝트 디렉토리로 이동
cd network

# 자동 설치 스크립트 실행
./install.sh
```

이 스크립트는 다음 작업을 자동으로 수행합니다:
- ✅ 시스템 감지 (Ubuntu/Debian, CentOS/RHEL, Fedora, Arch Linux)
- ✅ 필수 패키지 자동 설치
- ✅ SSL 인증서 자동 생성
- ✅ 기본 사용자 계정 생성
- ✅ 프로젝트 자동 빌드
- ✅ 호환성 테스트 실행

### 📋 수동 설치

자동 설치가 실패하거나 수동으로 설치하고 싶은 경우:

#### 요구사항

### 필수 라이브러리
- GCC 컴파일러
- OpenSSL 라이브러리
- ncurses 라이브러리
- pthread 라이브러리

### 설치 방법

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev libncurses-dev
```

#### CentOS/RHEL/Fedora
```bash
# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install openssl-devel ncurses-devel

# Fedora
sudo dnf groupinstall "Development Tools"
sudo dnf install openssl-devel ncurses-devel
```

#### Arch Linux
```bash
sudo pacman -S base-devel openssl ncurses
```

#### macOS
```bash
brew install openssl ncurses
```

## 빌드 방법

```bash
make clean
make
```

## 실행 방법

### 서버 실행

```bash
./bin/server [포트번호]
# 예시: ./bin/server 8080
```

### 클라이언트 실행

```bash
./bin/client [서버IP] [포트번호]
# 예시: ./bin/client 127.0.0.1 8080
```

## 리눅스 호환성

이 프로젝트는 리눅스 환경에서 완전히 호환됩니다. 자세한 내용은 [README_LINUX.md](README_LINUX.md)를 참조하세요.

### 리눅스 호환성 테스트
```bash
./test_linux_compatibility.sh
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
├── bin/              # 실행 파일들
│   ├── server        # 서버 실행 파일
│   ├── client        # 클라이언트 실행 파일
│   └── users.txt     # 사용자 계정 파일
├── obj/              # 오브젝트 파일들
├── certs/            # SSL 인증서
├── logs/             # 로그 파일들
├── Makefile         # 빌드 스크립트
├── install.sh       # 자동 설치 스크립트
├── README.md        # 프로젝트 설명
├── README_LINUX.md  # 리눅스 빌드 가이드
└── test_linux_compatibility.sh # 리눅스 호환성 테스트
```

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
# certs 디렉토리 생성
mkdir -p certs

# 서버 인증서 생성
openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes -subj "/C=KR/ST=Seoul/L=Seoul/O=NetworkProject/OU=IT/CN=localhost"

# 권한 설정
chmod 600 certs/server.key
chmod 644 certs/server.crt
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
- 위치: logs/server.log, logs/client.log
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

# 로그 파일 경로 설정
export LOG_FILE=logs/app.log
```

## 문제 해결

### 자동 설치 스크립트 오류
- **권한 오류**: `chmod +x install.sh` 실행
- **패키지 설치 실패**: 수동 설치 방법 참조
- **빌드 실패**: `make` 명령어로 상세 오류 확인

### 빌드 오류
- **"ncurses.h not found"**: `libncurses5-dev` 패키지 설치 필요
- **"ssl.h not found"**: `libssl-dev` 패키지 설치 필요
- **"pthread.h not found"**: `build-essential` 패키지 설치 필요

### 실행 오류
- **"Permission denied"**: 실행 권한 확인
  ```bash
  chmod +x bin/server bin/client
  ```
- **"SSL certificate not found"**: 인증서 파일 확인
- **"Port already in use"**: 다른 포트 사용 또는 기존 프로세스 종료

### 네트워크 연결 오류
- 방화벽 설정 확인
- 포트 개방 확인
- 서버 IP 주소 확인

## 가상머신(VM) 및 최소 설치 환경 권장사항

가상머신이나 서버용 최소 설치 환경에서 클라이언트를 실행할 경우, 아래 사항들을 권장합니다.

### 1. 한글 언어 팩 설치 (한글 깨짐 방지)

ncurses UI에서 한글이 깨지는 현상을 방지하기 위해 시스템에 한글 언어 팩과 로케일(locale) 설정이 필요합니다.

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install language-pack-ko
sudo locale-gen ko_KR.UTF-8
```
설치 후 터미널을 재시작하거나 재접속이 필요할 수 있습니다.

### 2. 터미널 멀티플렉서 (서버/클라이언트 동시 실행)

서버와 클라이언트를 동시에 편리하게 실행하고 모니터링하기 위해 `tmux`와 같은 터미널 멀티플렉서를 사용하는 것을 강력히 권장합니다.

#### tmux 설치
```bash
# Ubuntu/Debian
sudo apt-get install tmux

# CentOS/RHEL/Fedora
sudo yum install tmux  # 또는 sudo dnf install tmux

# Arch Linux
sudo pacman -S tmux
```

#### tmux 기본 사용법
- `tmux` : 새로운 세션 시작
- `Ctrl+b` 누른 후 `%` : 화면 수직 분할
- `Ctrl+b` 누른 후 `"` : 화면 수평 분할
- `Ctrl+b` 누른 후 `방향키` : 분할된 창 간 이동
- `exit` : 현재 창 닫기

### 3. GUI 데스크톱 환경

터미널 멀티플렉서가 익숙하지 않다면, `xfce4`나 `lxde` 같은 가벼운 GUI 데스크톱 환경을 설치하여 여러 터미널 창을 쉽게 열어 사용할 수 있습니다.

## 기본 사용자 계정

자동 설치 시 생성되는 기본 계정:
- **admin:password**
- **user1:pass123**
- **user2:pass456**

⚠️ **보안 주의**: 실제 운영 환경에서는 반드시 기본 비밀번호를 변경하세요.


