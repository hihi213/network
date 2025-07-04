# 장치 예약 시스템

이 프로젝트는 C언어로 개발된 네트워크 장치 예약 시스템입니다. 클라이언트-서버 아키텍처를 기반으로 하며, SSL/TLS를 통한 보안 통신과 ncurses를 이용한 텍스트 기반 UI를 제공합니다.

**지원 플랫폼**: Linux, macOS

## 주요 기능

-   **사용자 인증**: 안전한 로그인/로그아웃 기능
-   **장치 관리**: 실시간 장치 목록 조회 및 상태 확인
-   **예약 시스템**: 장치 예약 및 취소 기능
-   **보안 통신**: SSL/TLS를 통해 모든 통신을 암호화
-   **텍스트 UI**: ncurses 기반의 편리한 사용자 인터페이스
-   **동시성 제어**: 멀티스레딩을 통한 다중 사용자 동시 접속 처리

## 문서

프로젝트에 대한 자세한 정보는 아래의 문서를 참고하세요.

-   **[🚀 INSTALL.md](./INSTALL.md)**: 시스템 요구사항, 의존성 설치, 빌드 방법을 안내합니다. (`./install.sh` 스크립트 완료 시 불필요한 문서)
-   **[🔧 DEVELOPMENT.md](./DEVELOPMENT.md)**: 프로젝트 구조, 메시지 프로토콜 등 개발자를 위한 기술 정보를 제공합니다.
-   **[❓ TROUBLESHOOTING.md](./TROUBLESHOOTING.md)**: 설치 및 실행 중 발생할 수 있는 문제와 해결 방법을 안내합니다.

## 빠른 시작 (Linux)

Linux 사용자는 아래 스크립트를 통해 한 번에 인증서,의존패키지 설치와 빌드를 완료할 수 있습니다.

```bash
./install.sh
```

위의 스크립트가 실패할 시 [INSTALL.md](./INSTALL.md)를 참조하세요.

## 빌드 및 실행

### 1. 빌드
```bash
make clean
make
```

### 2. 사용자 계정 설정
프로젝트 루트의 `users.txt` 파일에서 사용자 계정을 관리할 수 있습니다.
계정 형식: `사용자명:비밀번호` (한 줄에 하나씩)으로, 로그인시 입력하면 됩니다.
```bash
# 기본 계정 (users.txt 파일에 포함됨)
user1:pw1
user2:pw2
user3:pw3
user4:pw4
user5:pw5
1:1
```


### 3. 서버 실행
서버를 먼저 실행해야 클라이언트가 접속할 수 있습니다.

```bash
# 기본 포트(8080)로 서버 실행
./bin/server

# 특정 포트로 서버 실행 (예: 9090)
./bin/server 9090
```

### 4. 클라이언트 실행
서버가 실행된 후, 새 터미널에서 클라이언트를 실행하여 서버에 접속합니다.

```bash
# 로컬 서버에 연결
./bin/client 127.0.0.1 9090

# 원격 서버에 연결
./bin/client <서버_IP_주소> <포트번호>
```

## UI 기능

ncurses 기반의 텍스트 UI는 다음과 같은 기능을 제공합니다.

### 메인 화면
- **장비 목록**: 실시간으로 예약 가능한 장비와 현재 상태를 표시합니다.
- **상태 창**: 서버와의 연결 상태, 시스템 메시지 등을 보여줍니다.
- **명령어 입력**: 하단의 입력창을 통해 명령어를 입력할 수 있습니다.

### 주요 명령어
- **예약**: `reserve <장비ID> <시작시간> <종료시간>`
- **취소**: `cancel <예약ID>`
- **상태 확인**: `status` (주기적으로 자동 갱신됨)
- **도움말**: `help`
- **종료**: `quit` 또는 `exit`

## 시스템 제한사항

-   최대 장비 수: 100개
-   최대 예약 수: 1000개
-   최대 동시 접속자 수: 100명
-   세션 타임아웃: 1시간
-   최대 메시지 크기: 4096 바이트



## 클라이언트 ui별 기능 설명

  ### 1. 로그인 화면 (APP_STATE_LOGIN)
화면 구성:
아이디 입력 필드
비밀번호 입력 필드 (마스킹 처리)
도움말 메시지
조작 가능한 키:
Tab: 아이디 ↔ 비밀번호 필드 전환
Enter:
아이디 필드에서: 비밀번호 필드로 이동
비밀번호 필드에서: 로그인 시도 (두 필드 모두 입력된 경우)
Backspace/Delete: 문자 삭제
일반 문자: 텍스트 입력
ESC: 메인 메뉴로 돌아가기

### 2. 시간 동기화 화면 (APP_STATE_SYNCING)
화면 구성:
"서버와 시간을 동기화하는 중입니다..." 메시지
"잠시만 기다려주세요..." 도움말
조작: 사용자 입력 불가 (자동 처리)

### 3. 메인 메뉴 (APP_STATE_MAIN_MENU)
화면 구성:
로그인 (하이라이트 가능)
종료 (하이라이트 가능)
도움말: "[↑↓] 이동 [Enter] 선택 [ESC] 종료"
조작 가능한 키:
↑/↓: 메뉴 항목 이동
Enter: 선택된 메뉴 실행
로그인 선택 → 로그인 화면으로 이동
종료 선택 → 프로그램 종료
ESC: 프로그램 종료

### 4. 로그인 후 메뉴 (APP_STATE_LOGGED_IN_MENU)
화면 구성:
장비 현황 조회 및 예약 (하이라이트 가능)
로그아웃 (하이라이트 가능)
도움말: "[↑↓] 이동 [Enter] 선택 [ESC] 로그아웃"
조작 가능한 키:
↑/↓: 메뉴 항목 이동
Enter: 선택된 메뉴 실행
장비 현황 조회 및 예약 → 장비 목록 화면으로 이동
로그아웃 → 로그아웃 처리 후 메인 메뉴로 이동
ESC: 로그아웃

### 5. 장비 목록 화면 (APP_STATE_DEVICE_LIST)
화면 구성:
장비 목록 테이블 (ID, 이름, 상태, 예약자, 남은 시간)
도움말: "[↑↓] 이동 [Enter] 예약/선택 [C] 예약취소 [ESC] 뒤로"
상황별 도움말 메시지
조작 가능한 키:
↑/↓: 장비 목록에서 이동
Enter: 선택된 장비에 대한 액션
사용 가능한 장비 → 예약 시간 입력 화면으로 이동
본인이 예약한 장비 → "이미 예약한 장비입니다" 메시지
다른 사용자가 예약한 장비 → "다른 사용자가 예약한 장비입니다" 메시지
점검 중인 장비 → "점검 중인 장비입니다" 메시지
C: 예약 취소 (본인이 예약한 장비인 경우)
ESC: 로그인 후 메뉴로 돌아가기

### 6. 예약 시간 입력 화면 (APP_STATE_RESERVATION_TIME)
화면 구성:
장비 목록 테이블 (위와 동일)
예약 시간 입력 필드
도움말: "[숫자] 시간 입력 [Enter] 예약 [ESC] 취소"
상세 도움말: "1 ~ 86400 사이의 예약 시간(초)을 입력하고 Enter를 누르세요."
조작 가능한 키:
숫자 키 (0-9): 예약 시간 입력 (1~86400초)
Enter: 예약 요청 전송
유효한 시간 (1~86400초) → 예약 요청 후 장비 목록으로 복귀
유효하지 않은 시간 → 에러 메시지 표시
Backspace/Delete: 문자 삭제
ESC: 예약 취소 후 장비 목록으로 돌아가기

### 7. 상태 표시 및 피드백
공통 기능:
상태 바: 현재 작업 상태, 성공/에러 메시지 표시
실시간 업데이트: 서버로부터 받은 메시지에 따른 UI 자동 업데이트
에러 처리: 다양한 에러 상황에 대한 사용자 친화적 메시지

### 8. 전체적인 사용자 경험 흐름
시작 → 메인 메뉴
로그인 → 로그인 화면 → 시간 동기화 → 로그인 후 메뉴
장비 조회/예약 → 장비 목록 → 예약 시간 입력 → 예약 완료
예약 관리 → 장비 목록에서 예약 취소
로그아웃 → 메인 메뉴로 복귀
종료 → 프로그램 안전 종료

### 9. 부가기능
- 클라이언트가 3000초 동안 요청을 보내지 않으면 소켓 타임아웃으로 인해 자동으로 종료하는 기능을 넣음(안쓰는 클라이언트 정리)
src/network.c:488-496줄의
timeout.tv_sec = 3000; 이 줄이 시간결정 변수
