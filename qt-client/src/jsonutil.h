#ifndef JSONUTIL_H
#define JSONUTIL_H

/*
 * 防御性 JSON 取值
 *
 * 服务端由 jsoncpp 直接序列化 MYSQL_ROW 的字符串, 数值字段(tk_id/max/num)
 * 在 JSON 里是字符串而非数字; 字段也可能缺失。QJsonValue::toInt() 遇到
 * 字符串会静默返回 0, 因此统一走这两个兼容助手。
 */
#include <QJsonObject>
#include <QString>

namespace JsonUtil {

// 数值字段: 兼容 "12"(字符串) 与 12(数字), 缺失/类型不符返回默认值
inline int asInt(const QJsonObject &obj, const QString &key, int def = 0)
{
    const QJsonValue v = obj.value(key);
    if (v.isDouble())
        return v.toInt();
    if (v.isString()) {
        bool ok = false;
        const int n = v.toString().trimmed().toInt(&ok);
        if (ok)
            return n;
    }
    return def;
}

// 字符串字段: 缺失/类型不符返回空串(调用方按业务决定是否容忍)
inline QString asStr(const QJsonObject &obj, const QString &key)
{
    const QJsonValue v = obj.value(key);
    return v.isString() ? v.toString() : QString();
}

} // namespace JsonUtil

#endif // JSONUTIL_H
