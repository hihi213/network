# 네트워크프로그래밍 프로젝트 Makefile

# 플랫폼 감지
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS (Homebrew)
    OPENSSL_INCLUDE = -I/opt/homebrew/opt/openssl@3/include
    OPENSSL_LIB = -L/opt/homebrew/opt/openssl@3/lib
    NCURSES_LIBS = -lncurses -lmenu -lform
else
    # Linux (기본 시스템 경로)
    OPENSSL_INCLUDE = 
    OPENSSL_LIB = 
    NCURSES_LIBS = -lncursesw -lmenuw -lformw
endif

CC = gcc
CFLAGS = -Wall -Wextra -g -I./include $(OPENSSL_INCLUDE)
LDFLAGS = $(OPENSSL_LIB) -lssl -lcrypto -lpthread $(NCURSES_LIBS)

# 공통 소스 파일
COMMON_SRCS = src/utils.c \
              src/network.c \
              src/resource.c \
              src/reservation.c \
              src/ui.c \
              src/session.c \
              src/message.c

# 서버 전용 소스 파일
SERVER_SRCS = src/server_main.c

# 클라이언트 전용 소스 파일
CLIENT_SRCS = src/client_main.c

# 오브젝트 파일들
COMMON_OBJS = $(patsubst src/%.c,obj/%.o,$(COMMON_SRCS))
SERVER_OBJS = $(patsubst src/%.c,obj/%.o,$(SERVER_SRCS))
CLIENT_OBJS = $(patsubst src/%.c,obj/%.o,$(CLIENT_SRCS))

# 실행 파일 이름
SERVER_TARGET = bin/server
CLIENT_TARGET = bin/client

# 기본 타겟
all: directories $(SERVER_TARGET) $(CLIENT_TARGET) install_users

# 디렉토리 생성
directories:
	@mkdir -p obj bin

# 서버 실행 파일 생성
$(SERVER_TARGET): $(COMMON_OBJS) $(SERVER_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

# 클라이언트 실행 파일 생성
$(CLIENT_TARGET): $(COMMON_OBJS) $(CLIENT_OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

# 오브젝트 파일 생성
obj/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# users.txt 복사
install_users:
	cp -f users.txt bin/users.txt

# 정리
clean:
	rm -f $(COMMON_OBJS) $(SERVER_OBJS) $(CLIENT_OBJS) $(SERVER_TARGET) $(CLIENT_TARGET)
	rm -rf obj bin

.PHONY: all clean directories 