#!/bin/bash

# 네트워크 예약 시스템 자동 설치 스크립트
# 지원 플랫폼: Ubuntu/Debian, CentOS/RHEL, Fedora, Arch Linux

set -e  # 오류 발생 시 스크립트 중단

# 색상 정의
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 로그 함수들
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 시스템 정보 출력
print_system_info() {
    log_info "=== 네트워크 예약 시스템 자동 설치 ==="
    log_info "OS: $(uname -s)"
    log_info "Architecture: $(uname -m)"
    log_info "Kernel: $(uname -r)"
    echo
}

# 시스템 감지 및 패키지 설치
install_dependencies() {
    log_info "시스템 감지 중..."
    
    if [ -f /etc/debian_version ]; then
        log_info "Debian/Ubuntu 시스템 감지"
        install_debian_packages
    elif [ -f /etc/redhat-release ]; then
        log_info "RedHat/CentOS/Fedora 시스템 감지"
        if command -v dnf >/dev/null 2>&1; then
            log_info "Fedora 패키지 매니저 사용"
            install_fedora_packages
        else
            log_info "YUM 패키지 매니저 사용"
            install_centos_packages
        fi
    elif [ -f /etc/arch-release ]; then
        log_info "Arch Linux 시스템 감지"
        install_arch_packages
    else
        log_error "지원되지 않는 시스템입니다."
        log_error "수동으로 다음 패키지들을 설치해주세요:"
        log_error "- GCC 컴파일러"
        log_error "- OpenSSL 개발 라이브러리"
        log_error "- ncurses 개발 라이브러리"
        exit 1
    fi
}

install_debian_packages() {
    log_info "Debian/Ubuntu 패키지 설치 중..."
    sudo apt update
    sudo apt install -y build-essential libssl-dev libncurses-dev pkg-config
    log_success "Debian/Ubuntu 패키지 설치 완료"
}

install_centos_packages() {
    log_info "CentOS/RHEL 패키지 설치 중..."
    sudo yum groupinstall -y "Development Tools"
    sudo yum install -y openssl-devel
    sudo yum install -y ncurses-devel
    sudo yum install -y pkgconfig
    log_success "CentOS/RHEL 패키지 설치 완료"
}

install_fedora_packages() {
    log_info "Fedora 패키지 설치 중..."
    sudo dnf groupinstall -y "Development Tools"
    sudo dnf install -y openssl-devel
    sudo dnf install -y ncurses-devel
    sudo dnf install -y pkgconfig
    log_success "Fedora 패키지 설치 완료"
}

install_arch_packages() {
    log_info "Arch Linux 패키지 설치 중..."
    sudo pacman -S --noconfirm base-devel
    sudo pacman -S --noconfirm openssl
    sudo pacman -S --noconfirm ncurses
    sudo pacman -S --noconfirm pkg-config
    log_success "Arch Linux 패키지 설치 완료"
}

# 필수 도구 확인
check_tools() {
    log_info "필수 도구 확인 중..."
    
    local missing_tools=()
    
    if ! command -v gcc >/dev/null 2>&1; then
        missing_tools+=("gcc")
    fi
    
    if ! command -v make >/dev/null 2>&1; then
        missing_tools+=("make")
    fi
    
    if ! command -v openssl >/dev/null 2>&1; then
        missing_tools+=("openssl")
    fi
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        log_error "다음 도구들이 설치되지 않았습니다: ${missing_tools[*]}"
        log_error "의존성 설치를 다시 실행해주세요."
        exit 1
    fi
    
    log_success "모든 필수 도구가 설치되어 있습니다"
}

# SSL 인증서 생성
create_ssl_certificates() {
    log_info "SSL 인증서 생성 중..."
    
    if [ ! -d "certs" ]; then
        mkdir -p certs
        log_info "certs 디렉토리 생성"
    fi
    
    if [ ! -f "certs/server.crt" ] || [ ! -f "certs/server.key" ]; then
        log_info "서버 인증서 생성 중..."
        openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes -subj "/C=KR/ST=Seoul/L=Seoul/O=NetworkProject/OU=IT/CN=localhost" >/dev/null 2>&1
        
        if [ $? -eq 0 ]; then
            chmod 600 certs/server.key
            chmod 644 certs/server.crt
            log_success "SSL 인증서 생성 완료"
        else
            log_error "SSL 인증서 생성 실패"
            exit 1
        fi
    else
        log_info "SSL 인증서가 이미 존재합니다"
    fi
}

# 사용자 파일 확인
check_users_file() {
    log_info "사용자 파일 확인 중..."
    
    if [ ! -f "users.txt" ]; then
        log_warning "users.txt 파일이 없습니다. 기본 사용자 파일을 생성합니다."
        cat > users.txt << EOF
admin:password
user1:pass123
user2:pass456
EOF
        log_success "기본 사용자 파일 생성 완료"
    else
        log_info "users.txt 파일이 존재합니다"
    fi
}

# 빌드 실행
build_project() {
    log_info "프로젝트 빌드 중..."
    
    if [ ! -f "Makefile" ]; then
        log_error "Makefile을 찾을 수 없습니다."
        log_error "올바른 프로젝트 디렉토리에서 실행해주세요."
        exit 1
    fi
    
    # 기존 빌드 정리
    log_info "기존 빌드 정리 중..."
    make clean >/dev/null 2>&1 || true
    
    # 빌드 실행
    log_info "컴파일 중..."
    if make >/dev/null 2>&1; then
        log_success "빌드 완료"
    else
        log_error "빌드 실패"
        log_error "오류 로그를 확인하려면 'make' 명령어를 직접 실행해주세요."
        exit 1
    fi
}

# 실행 파일 확인
check_executables() {
    log_info "실행 파일 확인 중..."
    
    if [ ! -f "bin/server" ] || [ ! -x "bin/server" ]; then
        log_error "서버 실행 파일이 없거나 실행 권한이 없습니다."
        exit 1
    fi
    
    if [ ! -f "bin/client" ] || [ ! -x "bin/client" ]; then
        log_error "클라이언트 실행 파일이 없거나 실행 권한이 없습니다."
        exit 1
    fi
    
    log_success "실행 파일 확인 완료"
}

# 호환성 테스트 실행
run_compatibility_test() {
    log_info "리눅스 호환성 테스트 실행 중..."
    
    if [ -f "test_linux_compatibility.sh" ]; then
        chmod +x test_linux_compatibility.sh
        if ./test_linux_compatibility.sh >/dev/null 2>&1; then
            log_success "호환성 테스트 통과"
        else
            log_warning "호환성 테스트에서 일부 문제가 발견되었습니다."
            log_warning "수동으로 './test_linux_compatibility.sh'를 실행하여 자세한 내용을 확인해주세요."
        fi
    else
        log_warning "호환성 테스트 스크립트를 찾을 수 없습니다."
    fi
}

# 설치 완료 메시지
print_completion_message() {
    echo
    log_success "=== 설치 완료 ==="
    echo
    log_info "네트워크 예약 시스템이 성공적으로 설치되었습니다!"
    echo
    log_info "실행 방법:"
    echo "  1. 서버 실행: ./bin/server [포트번호]"
    echo "     예시: ./bin/server 8080"
    echo
    echo "  2. 클라이언트 실행: ./bin/client [서버IP] [포트번호]"
    echo "     예시: ./bin/client 127.0.0.1 8080"
    echo
    log_info "문제가 발생하면 다음 파일들을 확인해주세요:"
    echo "  - README_LINUX.md: 리눅스 빌드 가이드"
    echo "  - logs/server.log: 서버 로그"
    echo "  - logs/client.log: 클라이언트 로그"
    echo
}

# 메인 함수
main() {
    print_system_info
    install_dependencies
    check_tools
    create_ssl_certificates
    check_users_file
    build_project
    check_executables
    run_compatibility_test
    print_completion_message
}

# 스크립트 실행
main "$@" 