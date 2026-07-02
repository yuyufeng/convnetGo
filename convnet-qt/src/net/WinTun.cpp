#ifdef _WIN32

#include "net/WinTun.h"

#include <windows.h>
#include <cstring>

// ---- Wintun API 函数指针类型（自声明，免依赖 wintun.h；运行时从 wintun.dll 取符号）----
typedef void* WINTUN_ADAPTER_HANDLE;
typedef void* WINTUN_SESSION_HANDLE;

typedef WINTUN_ADAPTER_HANDLE(__stdcall* WINTUN_CREATE_ADAPTER_FUNC)(
    LPCWSTR Name, LPCWSTR TunnelType, const GUID* RequestedGUID);
typedef void(__stdcall* WINTUN_CLOSE_ADAPTER_FUNC)(WINTUN_ADAPTER_HANDLE Adapter);
typedef WINTUN_SESSION_HANDLE(__stdcall* WINTUN_START_SESSION_FUNC)(
    WINTUN_ADAPTER_HANDLE Adapter, DWORD Capacity);
typedef void(__stdcall* WINTUN_END_SESSION_FUNC)(WINTUN_SESSION_HANDLE Session);
typedef HANDLE(__stdcall* WINTUN_GET_READ_WAIT_EVENT_FUNC)(WINTUN_SESSION_HANDLE Session);
typedef BYTE*(__stdcall* WINTUN_RECEIVE_PACKET_FUNC)(WINTUN_SESSION_HANDLE Session, DWORD* PacketSize);
typedef void(__stdcall* WINTUN_RELEASE_RECEIVE_PACKET_FUNC)(WINTUN_SESSION_HANDLE Session, const BYTE* Packet);
typedef BYTE*(__stdcall* WINTUN_ALLOCATE_SEND_PACKET_FUNC)(WINTUN_SESSION_HANDLE Session, DWORD PacketSize);
typedef void(__stdcall* WINTUN_SEND_PACKET_FUNC)(WINTUN_SESSION_HANDLE Session, const BYTE* Packet);

namespace {
WINTUN_CREATE_ADAPTER_FUNC         pCreateAdapter = nullptr;
WINTUN_CLOSE_ADAPTER_FUNC          pCloseAdapter = nullptr;
WINTUN_START_SESSION_FUNC          pStartSession = nullptr;
WINTUN_END_SESSION_FUNC            pEndSession = nullptr;
WINTUN_GET_READ_WAIT_EVENT_FUNC    pGetReadWaitEvent = nullptr;
WINTUN_RECEIVE_PACKET_FUNC         pReceivePacket = nullptr;
WINTUN_RELEASE_RECEIVE_PACKET_FUNC pReleaseRecv = nullptr;
WINTUN_ALLOCATE_SEND_PACKET_FUNC   pAllocSend = nullptr;
WINTUN_SEND_PACKET_FUNC            pSendPacket = nullptr;

template <typename T>
bool load(HMODULE dll, T& fn, const char* name)
{
    fn = reinterpret_cast<T>(GetProcAddress(dll, name));
    return fn != nullptr;
}
} // namespace

WinTun::~WinTun() { close(); }

bool WinTun::open(QString& ifName, const QString& ip, int prefixLen, QString& err)
{
    HMODULE dll = LoadLibraryA("wintun.dll");
    if (!dll) {
        err = QStringLiteral("加载 wintun.dll 失败（请将 wintun.dll 放到程序目录）");
        return false;
    }
    m_dll = dll;

    bool ok = load(dll, pCreateAdapter, "WintunCreateAdapter")
              && load(dll, pCloseAdapter, "WintunCloseAdapter")
              && load(dll, pStartSession, "WintunStartSession")
              && load(dll, pEndSession, "WintunEndSession")
              && load(dll, pGetReadWaitEvent, "WintunGetReadWaitEvent")
              && load(dll, pReceivePacket, "WintunReceivePacket")
              && load(dll, pReleaseRecv, "WintunReleaseReceivePacket")
              && load(dll, pAllocSend, "WintunAllocateSendPacket")
              && load(dll, pSendPacket, "WintunSendPacket");
    if (!ok) {
        err = QStringLiteral("wintun.dll 缺少必要符号");
        return false;
    }

    m_adapter = pCreateAdapter(L"ConvNet", L"ConvNet", nullptr);
    if (!m_adapter) {
        err = QStringLiteral("创建 Wintun 适配器失败（需管理员权限）");
        return false;
    }

    m_session = pStartSession(m_adapter, 0x400000 /*4MiB ring*/);
    if (!m_session) {
        err = QStringLiteral("WintunStartSession 失败");
        return false;
    }
    m_readEvent = pGetReadWaitEvent(m_session);

    ifName = QStringLiteral("ConvNet");

    // 配置 IP（netsh，按适配器名；掩码由 prefixLen 推导）
    quint32 maskBits = prefixLen >= 32 ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefixLen));
    const QString mask = QStringLiteral("%1.%2.%3.%4")
                             .arg((maskBits >> 24) & 0xFF)
                             .arg((maskBits >> 16) & 0xFF)
                             .arg((maskBits >> 8) & 0xFF)
                             .arg(maskBits & 0xFF);
    const QString cmd = QStringLiteral(
        "netsh interface ip set address name=\"ConvNet\" static %1 %2").arg(ip, mask);
    ::system(cmd.toUtf8().constData());

    return true;
}

int WinTun::readPacket(char* buf, int maxLen)
{
    if (!m_session)
        return -1;
    DWORD size = 0;
    BYTE* pkt = pReceivePacket(m_session, &size);
    if (pkt) {
        int n = static_cast<int>(size);
        if (n > maxLen)
            n = maxLen;
        std::memcpy(buf, pkt, static_cast<size_t>(n));
        pReleaseRecv(m_session, pkt);
        return n;
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS)
        return -1;
    // 无数据：最多等 200ms 后返回 0，让读循环有机会检查停止标志退出
    WaitForSingleObject(reinterpret_cast<HANDLE>(m_readEvent), 200);
    return 0;
}

bool WinTun::writePacket(const char* data, int len)
{
    if (!m_session)
        return false;
    BYTE* pkt = pAllocSend(m_session, static_cast<DWORD>(len));
    if (!pkt)
        return false; // 发送环满，丢弃
    std::memcpy(pkt, data, static_cast<size_t>(len));
    pSendPacket(m_session, pkt);
    return true;
}

void WinTun::close()
{
    if (m_session && pEndSession) {
        pEndSession(m_session);
        m_session = nullptr;
    }
    if (m_adapter && pCloseAdapter) {
        pCloseAdapter(m_adapter);
        m_adapter = nullptr;
    }
    if (m_dll) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_dll));
        m_dll = nullptr;
    }
}

ITapDevice* createTapDevice() { return new WinTun(); }

#endif // _WIN32
