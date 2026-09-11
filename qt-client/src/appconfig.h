#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QtGlobal>

// 服务端地址(单点定义, 登录对话框/主窗口/重连共用)
// 注: C++14 下头文件里的命名空间级变量用 constexpr(内部链接, 每个编译单元一份),
//     不能用 inline 变量(那是 C++17 特性)
namespace AppConfig {

// C++ 票务服务端(TcpClient 直连形态仍在, 现由 Python 侧对接)
constexpr const char *kServerHost = "127.0.0.1";
constexpr quint16 kServerPort = 6000;

// Agent 服务层(FastAPI)地址: V1-M2 起登录/表格/对话全部走它(Qt 不再直连 TCP)
// 启动: python -m agent.service --port 8000
constexpr const char *kApiBaseUrl = "http://127.0.0.1:8000";

} // namespace AppConfig

#endif // APPCONFIG_H
