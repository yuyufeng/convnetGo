#pragma once
#ifdef __linux__

#include "net/ITapDevice.h"

// LinuxTun —— 基于 /dev/net/tun 的 L3 TUN 设备（IFF_TUN | IFF_NO_PI）。
// 创建/配置 IP 需要 root 或 CAP_NET_ADMIN。

class LinuxTun : public ITapDevice {
public:
    ~LinuxTun() override;
    bool open(QString& ifName, const QString& ip, int prefixLen, QString& err) override;
    int  readPacket(char* buf, int maxLen) override;
    bool writePacket(const char* data, int len) override;
    void close() override;

private:
    int m_fd = -1;
    QString m_ifName;
};

#endif // __linux__
