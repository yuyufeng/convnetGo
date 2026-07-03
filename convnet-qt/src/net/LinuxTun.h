#pragma once
#ifdef __linux__

#include "net/ITapDevice.h"

// LinuxTun —— 基于 /dev/net/tun：TUN(IFF_TUN) 或 TAP(IFF_TAP)，均 IFF_NO_PI。
// 创建/配置 IP 需要 root 或 CAP_NET_ADMIN。
class LinuxTun : public ITapDevice {
public:
    explicit LinuxTun(NicMode mode) : m_mode(mode) {}
    ~LinuxTun() override;
    bool open(QString& ifName, const QString& ip, int prefixLen, QString& err) override;
    int  readPacket(char* buf, int maxLen) override;
    bool writePacket(const char* data, int len) override;
    void close() override;
    bool isLayer2() const override { return m_mode == NicMode::Tap; }

private:
    NicMode m_mode = NicMode::Tun;
    int m_fd = -1;
    QString m_ifName;
};

#endif // __linux__
