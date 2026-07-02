#pragma once
#ifdef _WIN32

#include "net/ITapDevice.h"

// WinTun —— 基于 Wintun（wintun.dll）的 L3 TUN 设备。
// 运行时需 wintun.dll（与程序同目录），创建适配器需管理员权限。

class WinTun : public ITapDevice {
public:
    ~WinTun() override;
    bool open(QString& ifName, const QString& ip, int prefixLen, QString& err) override;
    int  readPacket(char* buf, int maxLen) override;
    bool writePacket(const char* data, int len) override;
    void close() override;

private:
    void* m_dll = nullptr;      // HMODULE
    void* m_adapter = nullptr;  // WINTUN_ADAPTER_HANDLE
    void* m_session = nullptr;  // WINTUN_SESSION_HANDLE
    void* m_readEvent = nullptr; // HANDLE
};

#endif // _WIN32
