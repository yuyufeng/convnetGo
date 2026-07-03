#pragma once
#ifdef _WIN32

#include "net/ITapDevice.h"

// WinTap —— 基于 tap-windows6（OpenVPN TAP-Windows）驱动的 L2/TAP 设备。
// 需预装 tap-windows6 驱动（随 OpenVPN 或单独安装）；创建/配置需管理员权限。
// 收发完整以太网帧，可承载 IPX 等非 IP 协议，支持广播发现。
class WinTap : public ITapDevice {
public:
    ~WinTap() override;
    bool open(QString& ifName, const QString& ip, int prefixLen, QString& err) override;
    int  readPacket(char* buf, int maxLen) override;
    bool writePacket(const char* data, int len) override;
    void close() override;
    bool isLayer2() const override { return true; }

private:
    void* m_handle = nullptr;     // HANDLE 设备句柄（overlapped）
    void* m_readEvent = nullptr;  // HANDLE 读完成事件
    void* m_writeEvent = nullptr; // HANDLE 写完成事件
};

#endif // _WIN32
