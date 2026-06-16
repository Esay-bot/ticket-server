# C++ 高并发票务预约系统
## 项目介绍
基于 C++ + libevent 实现C/S架构票务预约系统，包含客户端、事件驱动服务端；
项目分两个分支：
- libevent-original：原始版本，纯libevent实现基础业务
- main：优化重构版本，参考muduo设计思想封装网络层组件，优化并发性能

## 技术栈
C++11、Linux、libevent、多线程、自定义Buffer、MySQL、JsonCpp

## 优化方向
1. 参照muduo Buffer实现自定义缓冲区，搭配4字节头协议处理TCP粘包
2. IO线程与业务线程分离，线程池异步处理数据库耗时操作
3. 定时器实现空闲连接自动回收，完善日志与服务优雅退出
4. MySQL事务保障票务预约，防止并发超卖、SQL注入

## 编译运行
# 编译服务端
g++ server/ser.cpp -o server.out -levent -lmysqlclient -ljsoncpp -lpthread
# 编译客户端
g++ client/client.cpp -o client.out

# 启动服务
./server.out
# 启动客户端
./client.out