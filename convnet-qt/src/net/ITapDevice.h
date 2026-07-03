#pragma once

// ITapDevice —— 虚拟网卡后端抽象。支持两种模式：
//   TUN（L3）：收发裸 IP 包，按目的 IP 路由，无 ARP/广播。
//   TAP（L2）：收发以太网帧，可承载任意二层协议（如 IPX），支持广播发现。
// 平台实现：LinuxTun（/dev/net/tun，IFF_TUN/IFF_TAP）、
//           WinTun（wintun.dll，仅 TUN）、WinTap（tap-windows6，仅 TAP）。

#include <QString>

enum class NicMode { Tun, Tap }; // L3 / L2

class ITapDevice {
public:
    virtual ~ITapDevice() = default;

    // 打开设备并配置 IP（ip 形如 "10.110.0.12"，prefixLen=8 即 /8）。
    // 成功返回 true；ifName 回填实际接口名，err 回填错误信息。
    virtual bool open(QString& ifName, const QString& ip, int prefixLen, QString& err) = 0;

    // TUN：读/写一个 IP 包；TAP：读/写一个以太网帧。读返回字节数，<=0 表示出错/关闭；
    // 读为超时式：无数据时返回 0（便于读循环及时检查停止标志）。
    virtual int readPacket(char* buf, int maxLen) = 0;
    virtual bool writePacket(const char* data, int len) = 0;

    virtual void close() = 0;

    // 是否为 L2（TAP）后端：Router 据此决定按 MAC + 广播 fanout 还是按 IP 路由。
    virtual bool isLayer2() const { return false; }
};

// 工厂：按平台 + 模式返回实现（Windows: Tun->WinTun / Tap->WinTap；Linux: LinuxTun(mode)）。
ITapDevice* createTapDevice(NicMode mode);
