# 编译器
CXX = g++
# 头文件路径
INCS = -I/usr/include/mysql
# 依赖库：libevent mysql jsoncpp pthread
LIBS = -levent -lmysqlclient -ljsoncpp -lpthread
# 编译选项
CXXFLAGS = -g -Wall $(INCS) -std=c++14
# 所有服务端源文件
SERVER_SRC = server/ser.cpp server/db_manager.cpp server/buffer.cpp server/connection.cpp server/threadpool.cpp
SERVER_OBJ = $(SERVER_SRC:.cpp=.o)
SERVER_TARGET = server/server

# 客户端源文件
CLIENT_SRC = client/client.cpp
CLIENT_OBJ = $(CLIENT_SRC:.cpp=.o)
CLIENT_TARGET = client/client

# 总目标
all: $(SERVER_TARGET) $(CLIENT_TARGET)

# 编译服务端
$(SERVER_TARGET): $(SERVER_OBJ)
	$(CXX) $(CXXFLAGS) $(SERVER_OBJ) -o $@ $(LIBS)

# 编译客户端
$(CLIENT_TARGET): $(CLIENT_OBJ)
	$(CXX) $(CXXFLAGS) $(CLIENT_OBJ) -o $@ $(LIBS)

# 自动生成.o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# 清理编译产物
clean:
	rm -rf $(SERVER_OBJ) $(CLIENT_OBJ) $(SERVER_TARGET) $(CLIENT_TARGET) *.out