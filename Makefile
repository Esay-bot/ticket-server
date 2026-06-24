CC = g++
CXXFLAGS = -std=c++14 -Wall -g
# MySQL头文件路径
INCS = -I/usr/include/mysql
# 链接依赖库
LIBS = -lmysqlclient -levent -ljsoncpp -lpthread

# ===================== 服务端源码 =====================
SERVER_DIR = server
SERVER_SRCS = $(SERVER_DIR)/ser.cpp \
              $(SERVER_DIR)/db_manager.cpp \
              $(SERVER_DIR)/buffer.cpp \
              $(SERVER_DIR)/connection.cpp \
              $(SERVER_DIR)/threadpool.cpp \
              $(SERVER_DIR)/log.cpp \
              $(SERVER_DIR)/mysql_conn_pool.cpp
SERVER_OBJS = $(SERVER_SRCS:.cpp=.o)
SERVER_TARGET = $(SERVER_DIR)/server

# ===================== 客户端源码 =====================
CLIENT_DIR = client
CLIENT_SRCS = $(CLIENT_DIR)/client.cpp
CLIENT_OBJS = $(CLIENT_SRCS:.cpp=.o)
CLIENT_TARGET = $(CLIENT_DIR)/client

# 默认：同时编译服务+客户端
all: server client

# 编译服务端
server: $(SERVER_TARGET)
$(SERVER_TARGET): $(SERVER_OBJS)
	$(CC) $(CXXFLAGS) $(SERVER_OBJS) -o $(SERVER_TARGET) $(INCS) $(LIBS)

# 编译客户端
client: $(CLIENT_TARGET)
$(CLIENT_TARGET): $(CLIENT_OBJS)
	$(CC) $(CXXFLAGS) $(CLIENT_OBJS) -o $(CLIENT_TARGET) $(INCS) $(LIBS)

# 通用编译规则，任意目录cpp自动生成.o
%.o: %.cpp
	$(CC) $(CXXFLAGS) $(INCS) -c $< -o $@

# 一键清理所有.o + 可执行程序
clean:
	rm -rf $(SERVER_OBJS) $(SERVER_TARGET)
	rm -rf $(CLIENT_OBJS) $(CLIENT_TARGET)
	rm -rf *.log.* core