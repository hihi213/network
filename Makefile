# 네트워크프로그래밍 프로젝트 Makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -I./src -I./include -I/opt/homebrew/opt/openssl/include
LDFLAGS = -L/usr/local/lib -L/usr/lib -L/opt/homebrew/opt/openssl/lib
LIBS = -lssl -lcrypto -lncurses -lpthread -lform -lmenu

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

# 소스 파일 목록
SRCS = $(wildcard $(SRC_DIR)/*.c)
OBJS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# 오브젝트 파일 목록 (공통)
OBJS_COMMON = obj/logger.o obj/message.o obj/network.o obj/performance.o obj/reservation.o obj/resource.o obj/session.o obj/ui.o

# 서버용 오브젝트
OBJS_SERVER = obj/server_main.o $(OBJS_COMMON)
# 클라이언트용 오브젝트
OBJS_CLIENT = obj/client_main.o $(OBJS_COMMON)

# 실행 파일
CLIENT = $(BIN_DIR)/client
SERVER = $(BIN_DIR)/server

# 기본 타겟
all: directories bin/server bin/client

# 디렉토리 생성
directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR)

# 클라이언트 빌드
$(CLIENT): $(OBJS_CLIENT)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# 서버 빌드
$(SERVER): $(OBJS_SERVER)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

# 오브젝트 파일 빌드
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# 클린
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR)

# 재빌드
rebuild: clean all

.PHONY: all clean rebuild directories 