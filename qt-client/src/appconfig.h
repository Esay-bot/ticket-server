#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QtGlobal>

// 服务端地址(单点定义, 登录对话框/主窗口/重连共用)
// 注: C++14 下头文件里的命名空间级变量用 constexpr(内部链接, 每个编译单元一份),
//     不能用 inline 变量(那是 C++17 特性)
namespace AppConfig {

constexpr const char *kServerHost = "127.0.0.1";
constexpr quint16 kServerPort = 6000;

} // namespace AppConfig

#endif // APPCONFIG_H
