#pragma once

// ITapDevice —— 虚拟网卡后端抽象（L3 / TUN，收发原始 IP 包）。
// 平台实现：LinuxTun（/dev/net/tun）、WinTun（wintun.dll）。

#include <QString>

class ITapDevice {
public:
    virtual ~ITapDevice() = default;

    // 打开设备并配置 IP（ip 形如 "10.110.0.12"，prefixLen=8 即 /8）。
    // 成功返回 true；ifName 回填实际接口名，err 回填错误信息。
    virtual bool open(QString& ifName, const QString& ip, int prefixLen, QString& err) = 0;

    // 阻塞读取一个 IP 包到 buf，返回字节数；<=0 表示出错/关闭。
    virtual int readPacket(char* buf, int maxLen) = 0;

    // 写入一个 IP 包，成功返回 true。
    virtual bool writePacket(const char* data, int len) = 0;

    virtual void close() = 0;
};

// 工厂：按平台返回对应实现（Linux -> LinuxTun，Windows -> WinTun）。
ITapDevice* createTapDevice();
