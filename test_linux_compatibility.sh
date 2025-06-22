#!/bin/bash

# 리눅스 호환성 테스트 스크립트
# 이 스크립트는 리눅스 환경에서 네트워크 예약 시스템의 호환성을 확인합니다.

echo "=== 네트워크 예약 시스템 리눅스 호환성 테스트 ==="
echo

# 1. 시스템 정보 확인
echo "1. 시스템 정보:"
echo "   OS: $(uname -s)"
echo "   Architecture: $(uname -m)"
echo "   Kernel: $(uname -r)"
echo

# 2. 필수 도구 확인
echo "2. 필수 도구 확인:"
echo -n "   GCC: "
if command -v gcc >/dev/null 2>&1; then
    echo "✓ $(gcc --version | head -n1)"
else
    echo "✗ 설치 필요"
    exit 1
fi

echo -n "   Make: "
if command -v make >/dev/null 2>&1; then
    echo "✓ $(make --version | head -n1)"
else
    echo "✗ 설치 필요"
    exit 1
fi

echo -n "   OpenSSL: "
if command -v openssl >/dev/null 2>&1; then
    echo "✓ $(openssl version)"
else
    echo "✗ 설치 필요"
    exit 1
fi

echo -n "   ncurses: "
if pkg-config --exists ncurses 2>/dev/null; then
    echo "✓ $(pkg-config --modversion ncurses)"
else
    echo "✗ 설치 필요"
    exit 1
fi

echo

# 3. 빌드 테스트
echo "3. 빌드 테스트:"
if [ -f "Makefile" ]; then
    echo "   Makefile 발견 ✓"
    
    # 기존 빌드 정리
    make clean >/dev/null 2>&1
    
    # 빌드 실행
    echo "   빌드 중..."
    if make >/dev/null 2>&1; then
        echo "   빌드 성공 ✓"
    else
        echo "   빌드 실패 ✗"
        exit 1
    fi
else
    echo "   Makefile 없음 ✗"
    exit 1
fi

echo

# 4. 실행 파일 확인
echo "4. 실행 파일 확인:"
if [ -f "bin/server" ] && [ -x "bin/server" ]; then
    echo "   서버 실행 파일 ✓"
else
    echo "   서버 실행 파일 없음 ✗"
    exit 1
fi

if [ -f "bin/client" ] && [ -x "bin/client" ]; then
    echo "   클라이언트 실행 파일 ✓"
else
    echo "   클라이언트 실행 파일 없음 ✗"
    exit 1
fi

echo

# 5. 의존성 라이브러리 확인
echo "5. 의존성 라이브러리 확인:"
if ldd bin/server >/dev/null 2>&1; then
    echo "   서버 라이브러리 의존성 ✓"
    ldd bin/server | grep -E "(ssl|crypto|ncurses|pthread)" | while read line; do
        echo "     $line"
    done
else
    echo "   서버 라이브러리 의존성 확인 실패 ✗"
fi

echo

# 6. SSL 인증서 확인
echo "6. SSL 인증서 확인:"
if [ -f "certs/server.crt" ] && [ -f "certs/server.key" ]; then
    echo "   SSL 인증서 파일 ✓"
else
    echo "   SSL 인증서 파일 없음 (생성 필요)"
    echo "   다음 명령어로 생성하세요:"
    echo "   mkdir -p certs"
    echo "   openssl req -x509 -newkey rsa:4096 -keyout certs/server.key -out certs/server.crt -days 365 -nodes -subj '/C=KR/ST=Seoul/L=Seoul/O=NetworkProject/OU=IT/CN=localhost'"
fi

echo

# 7. 사용자 파일 확인
echo "7. 사용자 파일 확인:"
if [ -f "users.txt" ]; then
    echo "   users.txt 파일 ✓"
    echo "   사용자 수: $(wc -l < users.txt)"
else
    echo "   users.txt 파일 없음 ✗"
    exit 1
fi

echo

# 8. 포트 사용 가능성 확인
echo "8. 포트 사용 가능성 확인:"
PORT=8080
if netstat -tuln 2>/dev/null | grep -q ":$PORT "; then
    echo "   포트 $PORT 사용 중 ✗"
    echo "   다른 포트를 사용하거나 기존 프로세스를 종료하세요"
else
    echo "   포트 $PORT 사용 가능 ✓"
fi

echo

# 9. 네트워크 인터페이스 확인
echo "9. 네트워크 인터페이스 확인:"
if command -v ip >/dev/null 2>&1; then
    echo "   네트워크 인터페이스:"
    ip addr show | grep -E "inet.*scope global" | while read line; do
        echo "     $line"
    done
else
    echo "   네트워크 인터페이스 확인 불가"
fi

echo

echo "=== 테스트 완료 ==="
echo
echo "모든 테스트가 통과했습니다! 리눅스에서 정상적으로 실행할 수 있습니다."
echo
echo "실행 방법:"
echo "1. 서버: ./bin/server [포트번호]"
echo "2. 클라이언트: ./bin/client [서버IP] [포트번호]"
echo
echo "예시:"
echo "  ./bin/server 8080"
echo "  ./bin/client 127.0.0.1 8080" 