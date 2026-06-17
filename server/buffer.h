#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <string>
#include <cstdint>

class Buffer
{
public:
    // 预留头部空间，方便后续封装协议头
    static const size_t kCheapPrepend = 8;
    // 缓冲区初始大小
    static const size_t kInitialSize = 1024;

    explicit Buffer(size_t initialSize = kInitialSize)
        : buffer_(kCheapPrepend + initialSize),
          readIndex_(kCheapPrepend),
          writeIndex_(kCheapPrepend)
    {}

    // 可读字节数
    size_t readableBytes() const
    {
        return writeIndex_ - readIndex_;
    }

    // 可写字节数
    size_t writableBytes() const
    {
        return buffer_.size() - writeIndex_;
    }

    // 头部预留可用空间
    size_t prependableBytes() const
    {
        return readIndex_;
    }

    // 获取可读区域起始指针
    const char* peek() const
    {
        return begin() + readIndex_;
    }

    // 读取4字节网络序int（用来解析我们4字节长度头）
    int32_t peekInt32() const;

    // 取出指定长度数据，返回字符串
    std::string retrieveAsString(size_t len);

    // 读取全部可读数据
    std::string retrieveAllAsString()
    {
        return retrieveAsString(readableBytes());
    }

    // 跳过len长度数据（读完数据移动读指针）
    void retrieve(size_t len);

    // 追加字符串数据到缓冲区
    void append(const std::string& data);
    // 追加字节数组
    void append(const char* data, size_t len);
    // 写入4字节网络序int（发送数据包长度头）
    void appendInt32(int32_t val);

    // 从fd读取数据填充缓冲区
    ssize_t readFd(int fd, int* savedErrno);

private:
    char* begin()
    {
        return &*buffer_.begin();
    }

    const char* begin() const
    {
        return &*buffer_.begin();
    }

    // 扩容或者内存整理
    void ensureWritableBytes(size_t len);
    // 把读指针前面空闲空间利用起来，前移数据避免频繁扩容
    void makeSpace(size_t len);

    std::vector<char> buffer_;
    size_t readIndex_;
    size_t writeIndex_;
};

#endif