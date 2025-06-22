# 네트워크 예약 시스템 - 리눅스 빌드 가이드

## 개요
이 프로젝트는 네트워크 프로그래밍을 이용한 장비 예약 시스템입니다. 
리눅스 환경에서 빌드하고 실행할 수 있습니다.

## 🚀 빠른 시작 (자동 설치)

### 원클릭 설치 (권장)

가장 간편한 방법으로 모든 의존성을 자동으로 설치하고 빌드합니다:

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

### 자동 설치 후 실행

```bash
# 서버 실행
./bin/server 8080

# 클라이언트 실행 (다른 터미널에서)
./bin/client 127.0.0.1 8080
```

## 📋 수동 설치

자동 설치가 실패하거나 수동으로 설치하고 싶은 경우:

### 시스템 요구사항
- Linux (Ubuntu, Debian, CentOS, RHEL 등)
- GCC 컴파일러
- OpenSSL 개발 라이브러리
- ncurses 개발 라이브러리

### 의존성 패키지 설치

#### Ubuntu/Debian 계열
```bash
sudo apt update
sudo apt install -y build-essential libssl-dev libncurses-dev
```

#### CentOS/RHEL/Fedora 계열
```bash
# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install openssl-devel
sudo yum install ncurses-devel

# Fedora
sudo dnf groupinstall "Development Tools"
sudo dnf install openssl-devel
sudo dnf install ncurses-devel
```

#### Arch Linux
```bash
sudo pacman -S base-devel
sudo pacman -S openssl
sudo pacman -S ncurses
```

### 빌드 방법

1. 프로젝트 디렉토리로 이동
```bash
cd network
```

2. 빌드 실행
```bash
make clean
make
```

3. 빌드 확인
```bash
ls -la bin/
# server와 client 실행 파일이 생성되어야 합니다
```

### SSL 인증서 설정

#### 자체 서명 인증서 생성 (개발용)
```bash
# certs 디렉토리 생성
mkdir -p certs

# 서버 인증서 생성
openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes -subj "/C=KR/ST=Seoul/L=Seoul/O=NetworkProject/OU=IT/CN=localhost"

# 권한 설정
chmod 600 certs/server.key
chmod 644 certs/server.crt
```

### 사용자 계정 설정

1. `users.txt` 파일 편집
```bash
# 형식: 사용자명:비밀번호
echo "admin:password" > users.txt
echo "user1:pass123" >> users.txt
echo "user2:pass456" >> users.txt
```

2. 실행 파일 디렉토리로 복사
```bash
cp users.txt bin/
```

## 실행 방법

### 1. 서버 실행
```bash
# 기본 포트(8080)로 서버 실행
./bin/server

# 특정 포트로 서버 실행
./bin/server 9090
```

### 2. 클라이언트 실행
```bash
# 로컬 서버에 연결
./bin/client 127.0.0.1 8080

# 원격 서버에 연결
./bin/client 서버IP주소 포트번호
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

#### Ubuntu/Debian 계열
```bash
sudo apt update
sudo apt install -y language-pack-ko
sudo locale-gen ko_KR.UTF-8
```
설치 후 터미널을 재시작하거나 재접속이 필요할 수 있습니다.

### 2. 터미널 멀티플렉서 (서버/클라이언트 동시 실행)

서버와 클라이언트를 동시에 편리하게 실행하고 모니터링하기 위해 `tmux`와 같은 터미널 멀티플렉서를 사용하는 것을 강력히 권장합니다.

#### tmux 설치
```bash
# Ubuntu/Debian
sudo apt install -y tmux

# CentOS/RHEL/Fedora
sudo yum install -y tmux  # 또는 sudo dnf install -y tmux

# Arch Linux
sudo pacman -S --noconfirm tmux
```

#### tmux 기본 사용법
- `tmux` : 새로운 세션 시작
- `Ctrl+b` 누른 후 `%` : 화면 수직 분할
- `Ctrl+b` 누른 후 `"` : 화면 수평 분할
- `Ctrl+b` 누른 후 `방향키` : 분할된 창 간 이동
- `exit` : 현재 창 닫기

### 3. GUI 데스크톱 환경

터미널 멀티플렉서가 익숙하지 않다면, `xfce4`나 `lxde` 같은 가벼운 GUI 데스크톱 환경을 설치하여 여러 터미널 창을 쉽게 열어 사용할 수 있습니다.

## 로그 확인

실행 중 로그는 `logs/` 디렉토리에 저장됩니다:
- `logs/server.log`: 서버 로그
- `logs/client.log`: 클라이언트 로그

## 개발 환경

### 디버그 빌드
```bash
make clean
make CFLAGS="-Wall -Wextra -g -DDEBUG"
```

### 릴리즈 빌드
```bash
make clean
make CFLAGS="-Wall -Wextra -O2 -DNDEBUG"
```

## 기본 사용자 계정

자동 설치 시 생성되는 기본 계정:
- **admin:password**
- **user1:pass123**
- **user2:pass456**

⚠️ **보안 주의**: 실제 운영 환경에서는 반드시 기본 비밀번호를 변경하세요.

## 호환성 테스트

리눅스 호환성을 확인하려면:
```bash
./test_linux_compatibility.sh
```

