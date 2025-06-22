# 설치 가이드

이 문서는 네트워크 예약 시스템을 다양한 운영체제에 설치하는 방법을 안내합니다.

> **💡 참고**: `./install.sh` 자동 설치 스크립트가 성공적으로 완료되면 이 문서의 대부분 내용은 더 이상 필요하지 않습니다. 이 문서는 자동 설치가 실패한 경우나 수동 설치를 원하는 경우에만 참조하세요.

## 🚀 빠른 시작: 자동 설치 (Linux 권장)

가장 간편한 방법으로, 아래 스크립트는 Linux 환경에서 모든 의존성 설치와 빌드 과정을 자동으로 수행합니다.

```bash
# 프로젝트 디렉토리로 이동
cd network

# 자동 설치 스크립트 실행
./install.sh
```

### 자동 설치 스크립트 기능
- ✅ 시스템 감지 (Ubuntu/Debian, CentOS/RHEL, Fedora, Arch Linux)
- ✅ 필수 패키지 자동 설치
- ✅ SSL 인증서 자동 생성
- ✅ 기본 사용자 계정 생성
- ✅ 프로젝트 자동 빌드
- ✅ 호환성 테스트 실행

---

## 📋 수동 설치

자동 설치가 실패하거나, macOS 사용자, 또는 수동으로 설치하고 싶은 경우 아래의 안내를 따르세요.

### 1. 시스템 요구사항

- **운영체제**: Linux 또는 macOS
- **필수 라이브러리**:
  - GCC 컴파일러
  - OpenSSL 라이브러리
  - ncurses 라이브러리
  - pthread 라이브러리
Linux 환경에서 호환성을 확인하려면 아래 스크립트를 실행하세요.

```bash
./test_linux_compatibility.sh
```
### 2. 의존성 패키지 설치

#### macOS
```bash
brew install openssl ncurses
```

#### Linux - Ubuntu/Debian 계열
```bash
sudo apt-get update
sudo apt-get install build-essential libssl-dev libncurses-dev
```

#### Linux - CentOS/RHEL/Fedora 계열
```bash
# CentOS/RHEL
sudo yum groupinstall "Development Tools"
sudo yum install openssl-devel ncurses-devel

# Fedora
sudo dnf groupinstall "Development Tools"
sudo dnf install openssl-devel ncurses-devel
```

#### Linux - Arch Linux 계열
```bash
sudo pacman -S base-devel openssl ncurses
```

### 3. 소스 코드 빌드

의존성 설치 후, `make` 명령어로 프로젝트를 빌드합니다.

```bash
make clean && make
```
빌드가 성공하면 `bin/` 디렉토리에 `server`와 `client` 실행 파일이 생성됩니다.

### 4. SSL 인증서 생성

안전한 통신을 위해 서버는 SSL 인증서가 필요합니다.

```bash
# certs 디렉토리 생성
mkdir -p certs

# 서버 인증서와 개인키 생성
openssl req -x509 -newkey rsa:4096 \
        -keyout certs/server.key -out certs/server.crt \
        -days 365 -nodes \
        -subj "/C=KR/ST=Seoul/L=Seoul/O=NetworkProject/OU=IT/CN=localhost"

# 권한 설정
chmod 600 certs/server.key
chmod 644 certs/server.crt
```

---

## 가상머신(VM) 및 최소 설치 환경 권장사항

가상머신이나 서버용 최소 설치 환경에서 클라이언트를 원활하게 사용하기 위해 다음 설정을 권장합니다.

### 1. 한글 언어 팩 설치 (한글 깨짐 방지)

ncurses UI에서 한글이 깨지는 현상을 방지하기 위해 시스템에 한글 언어 팩과 로케일(locale) 설정이 필요합니다.

#### Ubuntu/Debian 계열
```bash
sudo apt update
sudo apt install -y language-pack-ko
sudo locale-gen ko_KR.UTF-8
```
설치 후 터미널을 재시작하거나 재접속해야 설정이 적용됩니다.

### 2. 터미널 멀티플렉서 (서버/클라이언트 동시 실행)

서버와 클라이언트를 하나의 터미널에서 동시에 실행하고 모니터링하기 위해 `tmux` 사용을 강력히 권장합니다.

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
- `tmux`: 새로운 세션 시작
- `Ctrl+b` 누른 후 `%`: 화면 수직 분할
- `Ctrl+b` 누른 후 `"`: 화면 수평 분할
- `Ctrl+b` 누른 후 `방향키`: 분할된 창 간 이동
- `exit`: 현재 창 닫기

---

## 문제 해결

### 자동 설치 스크립트 오류

- **권한 오류**: `bash: ./install.sh: Permission denied`
  - **해결**: 스크립트에 실행 권한을 부여하세요.
    ```bash
    chmod +x install.sh
    ```

- **패키지 설치 실패**: `E: Unable to locate package ...` 또는 `No package ... found`
  - **원인**: 패키지 관리자가 필요한 패키지를 찾지 못하는 경우입니다. OS 버전이나 미러 사이트 문제일 수 있습니다.
  - **해결**: 위의 [수동 설치 가이드](#📋-수동-설치)를 참조하여 직접 의존성 패키지를 설치해 보세요.

### 빌드 오류

- **"ncurses.h: No such file or directory"**
  - **원인**: ncurses 개발 라이브러리가 설치되지 않았습니다.
  - **해결**: `libncurses-dev` 또는 `ncurses-devel` 패키지를 설치하세요.

- **"openssl/ssl.h: No such file or directory"**
  - **원인**: OpenSSL 개발 라이브러리가 설치되지 않았습니다.
  - **해결**: `libssl-dev` 또는 `openssl-devel` 패키지를 설치하세요.

- **"pthread.h: No such file or directory"**
  - **원인**: 기본적인 빌드 도구 및 C 표준 라이브러리가 부족합니다.
  - **해결**: `build-essential` 또는 "Development Tools" 그룹을 설치하세요. 