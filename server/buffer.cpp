#include "buffer.h"
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <string.h>
int32_t Buffer::peekInt32() const
{
    if (readableBytes() >= sizeof(int32_t))
    {
        int32_t val = 0;
        ::memcpy(&val, peek(), sizeof(val));
        // 网络字节序转主机字节序
        return ntohl(val);
    }
    return 0;
}

std::string Buffer::retrieveAsString(size_t len)
{
    std::string res(peek(), len);
    retrieve(len);
    return res;
}

void Buffer::retrieve(size_t len)
{
    if (len < readableBytes())
    {
        readIndex_ += len;
    }
    else
    {
        // 读完所有数据，重置双指针到预留位置
        readIndex_ = kCheapPrepend;
        writeIndex_ = kCheapPrepend;
    }
}

void Buffer::append(const std::string &data)
{
    append(data.data(), data.size());
}

void Buffer::append(const char *data, size_t len)
{
    ensureWritableBytes(len);
    std::copy(data, data + len, begin() + writeIndex_);
    writeIndex_ += len;
}

void Buffer::appendInt32(int32_t val)
{
    uint32_t netVal = htonl(static_cast<uint32_t>(val));
    append(reinterpret_cast<const char *>(&netVal), sizeof(netVal));
}

void Buffer::ensureWritableBytes(size_t len)
{
    if (writableBytes() < len)
    {
        makeSpace(len);
    }
}

void Buffer::makeSpace(size_t len)
{
    // 空闲总空间不够，需要扩容
    if (writableBytes() + prependableBytes() < len + kCheapPrepend)
    {
        buffer_.resize(writeIndex_ + len);
    }
    else
    {
        // 前移数据，复用前面空闲空间，不扩容
        size_t readable = readableBytes();
        std::copy(begin() + readIndex_,
                  begin() + writeIndex_,
                  begin() + kCheapPrepend);
        readIndex_ = kCheapPrepend;
        writeIndex_ = kCheapPrepend + readable;
    }
}

ssize_t Buffer::readFd(int fd, int *savedErrno)
{
    // 采用分散IO，预留临时缓冲区防止本buffer不够装
    char extraBuf[65536];
    struct iovec vec[2];

    const size_t writable = writableBytes();
    vec[0].iov_base = begin() + writeIndex_;
    vec[0].iov_len = writable;
    vec[1].iov_base = extraBuf;
    vec[1].iov_len = sizeof(extraBuf);

    const int iovCnt = (writable < sizeof(extraBuf)) ? 2 : 1;
    ssize_t n = ::readv(fd, vec, iovCnt);

    if (n < 0)
    {
        *savedErrno = errno;
    }
    else if (static_cast<size_t>(n) <= writable)
    {
        // 数据全部写入当前buffer
        writeIndex_ += n;
    }
    else
    {
        // 一部分写入临时缓冲区，追加到buffer尾部
        writeIndex_ = buffer_.size();
        append(extraBuf, n - writable);
    }
    return n;
}