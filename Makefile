CC = g++
CXXFLAGS = -std=c++14 -Wall -g
# MySQL头文件路径
INCS = -I/usr/include/mysql
# 链接库
LIBS = -lmysqlclient -levent -ljsoncpp -lpthread

# 所有server目录下cpp，包含新增连接池、日志
SRCS = server/ser.cpp \
       server/db_manager.cpp \
       server/buffer.cpp \
       server/connection.cpp \
       server/threadpool.cpp \
       server/log.cpp \
       server/mysql_conn_pool.cpp

# 输出可执行文件
TARGET = server/server
# 自动生成对应.o
OBJS = $(SRCS:.cpp=.o)

all: $(TARGET)

# 链接
$(TARGET): $(OBJS)
	$(CC) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(INCS) $(LIBS)

# 编译规则，适配server/子目录
%.o: %.cpp
	$(CC) $(CXXFLAGS) $(INCS) -c $< -o $@

# 清理所有.o、程序、日志切割文件
clean:
	rm -rf $(OBJS) $(TARGET) *.log.*