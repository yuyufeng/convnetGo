#ifdef _WIN32

#include "net/WinTap.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winioctl.h>
#include <string>
#include <cstdlib>

// ---- tap-windows6 常量 ----
namespace {
const wchar_t* kAdapterKey =
    L"SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}";
const wchar_t* kNetworkConnKey =
    L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}";

#define TAP_CTL(request, method) CTL_CODE(FILE_DEVICE_UNKNOWN, (request), (method), FILE_ANY_ACCESS)
constexpr DWORD TAP_WIN_IOCTL_SET_MEDIA_STATUS = TAP_CTL(6, METHOD_BUFFERED);

// 枚举网卡类注册表，找到 tap-windows6 适配器的实例 GUID
bool findTapGuid(std::wstring& guid)
{
    HKEY adapters;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kAdapterKey, 0, KEY_READ, &adapters) != ERROR_SUCCESS)
        return false;

    bool found = false;
    for (DWORD i = 0;; ++i) {
        wchar_t sub[256];
        DWORD subLen = 256;
        LONG r = RegEnumKeyExW(adapters, i, sub, &subLen, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS)
            break;
        if (r != ERROR_SUCCESS)
            continue;

        HKEY unit;
        if (RegOpenKeyExW(adapters, sub, 0, KEY_READ, &unit) != ERROR_SUCCESS)
            continue;

        wchar_t cid[256] = {0};
        DWORD cidLen = sizeof(cid);
        DWORD type = 0;
        if (RegQueryValueExW(unit, L"ComponentId", nullptr, &type,
                             reinterpret_cast<LPBYTE>(cid), &cidLen) == ERROR_SUCCESS
            && type == REG_SZ) {
            const std::wstring c = cid;
            // tap-windows6 = "tap0901"，旧驱动 "tap0801"，OpenVPN 有时带 "root\\" 前缀
            if (c == L"tap0901" || c == L"root\\tap0901"
                || c == L"tap0801" || c == L"root\\tap0801") {
                wchar_t id[256] = {0};
                DWORD idLen = sizeof(id);
                if (RegQueryValueExW(unit, L"NetCfgInstanceId", nullptr, nullptr,
                                     reinterpret_cast<LPBYTE>(id), &idLen) == ERROR_SUCCESS) {
                    guid = id;
                    found = true;
                }
            }
        }
        RegCloseKey(unit);
        if (found)
            break;
    }
    RegCloseKey(adapters);
    return found;
}

// 由实例 GUID 取网络连接的“友好名”（供 netsh 配 IP）
std::wstring friendlyName(const std::wstring& guid)
{
    const std::wstring path = std::wstring(kNetworkConnKey) + L"\\" + guid + L"\\Connection";
    HKEY k;
    std::wstring name;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS) {
        wchar_t buf[256] = {0};
        DWORD len = sizeof(buf);
        if (RegQueryValueExW(k, L"Name", nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(buf), &len) == ERROR_SUCCESS)
            name = buf;
        RegCloseKey(k);
    }
    return name;
}
} // namespace

WinTap::~WinTap() { close(); }

bool WinTap::open(QString& ifName, const QString& ip, int prefixLen, QString& err)
{
    std::wstring guid;
    if (!findTapGuid(guid)) {
        err = QStringLiteral("未找到 tap-windows6 网卡（请安装 OpenVPN TAP 驱动后重试）");
        return false;
    }
    std::wstring friendly = friendlyName(guid);
    if (friendly.empty())
        friendly = L"TAP";
    ifName = QString::fromWCharArray(friendly.c_str());

    const std::wstring devPath = std::wstring(L"\\\\.\\Global\\") + guid + L".tap";
    HANDLE h = CreateFileW(devPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        err = QStringLiteral("打开 TAP 设备失败（需管理员权限；错误码 %1）")
                  .arg(static_cast<qulonglong>(GetLastError()));
        return false;
    }
    m_handle = h;

    // 置为“已连接”（media connect），否则协议栈认为网线未插
    ULONG status = 1;
    DWORD ret = 0;
    if (!DeviceIoControl(h, TAP_WIN_IOCTL_SET_MEDIA_STATUS, &status, sizeof(status),
                         &status, sizeof(status), &ret, nullptr)) {
        err = QStringLiteral("TAP 置连接状态失败（错误码 %1）")
                  .arg(static_cast<qulonglong>(GetLastError()));
        CloseHandle(h);
        m_handle = nullptr;
        return false;
    }

    m_readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual-reset
    m_writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    // 配置 IP（netsh，按友好名；掩码由 prefixLen 推导）。用 _wsystem 支持中文/带空格网卡名。
    const quint32 maskBits = prefixLen >= 32 ? 0xFFFFFFFFu : (0xFFFFFFFFu << (32 - prefixLen));
    const QString mask = QStringLiteral("%1.%2.%3.%4")
                             .arg((maskBits >> 24) & 0xFF)
                             .arg((maskBits >> 16) & 0xFF)
                             .arg((maskBits >> 8) & 0xFF)
                             .arg(maskBits & 0xFF);
    const QString cmd = QStringLiteral(
        "netsh interface ip set address name=\"%1\" static %2 %3")
        .arg(ifName, ip, mask);
    ::_wsystem(reinterpret_cast<const wchar_t*>(cmd.utf16()));

    return true;
}

int WinTap::readPacket(char* buf, int maxLen)
{
    if (!m_handle)
        return -1;
    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = static_cast<HANDLE>(m_readEvent);
    ResetEvent(static_cast<HANDLE>(m_readEvent));

    DWORD n = 0;
    if (ReadFile(static_cast<HANDLE>(m_handle), buf, static_cast<DWORD>(maxLen), &n, &ov))
        return static_cast<int>(n); // 同步完成
    if (GetLastError() != ERROR_IO_PENDING)
        return -1;

    const DWORD w = WaitForSingleObject(static_cast<HANDLE>(m_readEvent), 200);
    if (w == WAIT_TIMEOUT) {
        // 超时无数据：取消挂起的读并等它真正结束（避免缓冲区仍被内核占用后被复用）
        CancelIoEx(static_cast<HANDLE>(m_handle), &ov);
        DWORD got = 0;
        GetOverlappedResult(static_cast<HANDLE>(m_handle), &ov, &got, TRUE);
        return got > 0 ? static_cast<int>(got) : 0; // 竞态下可能刚好收到一帧
    }
    if (w != WAIT_OBJECT_0)
        return -1;
    if (!GetOverlappedResult(static_cast<HANDLE>(m_handle), &ov, &n, FALSE))
        return -1;
    return static_cast<int>(n);
}

bool WinTap::writePacket(const char* data, int len)
{
    if (!m_handle)
        return false;
    OVERLAPPED ov;
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = static_cast<HANDLE>(m_writeEvent);
    ResetEvent(static_cast<HANDLE>(m_writeEvent));

    DWORD n = 0;
    if (WriteFile(static_cast<HANDLE>(m_handle), data, static_cast<DWORD>(len), &n, &ov))
        return n == static_cast<DWORD>(len);
    if (GetLastError() != ERROR_IO_PENDING)
        return false;
    if (WaitForSingleObject(static_cast<HANDLE>(m_writeEvent), 1000) != WAIT_OBJECT_0) {
        CancelIoEx(static_cast<HANDLE>(m_handle), &ov);
        GetOverlappedResult(static_cast<HANDLE>(m_handle), &ov, &n, TRUE);
        return false;
    }
    if (!GetOverlappedResult(static_cast<HANDLE>(m_handle), &ov, &n, FALSE))
        return false;
    return n == static_cast<DWORD>(len);
}

void WinTap::close()
{
    if (m_handle) {
        ULONG status = 0;
        DWORD ret = 0;
        DeviceIoControl(static_cast<HANDLE>(m_handle), TAP_WIN_IOCTL_SET_MEDIA_STATUS,
                        &status, sizeof(status), &status, sizeof(status), &ret, nullptr);
        CancelIoEx(static_cast<HANDLE>(m_handle), nullptr); // 兜底取消残留 I/O
        CloseHandle(static_cast<HANDLE>(m_handle));
        m_handle = nullptr;
    }
    if (m_readEvent) {
        CloseHandle(static_cast<HANDLE>(m_readEvent));
        m_readEvent = nullptr;
    }
    if (m_writeEvent) {
        CloseHandle(static_cast<HANDLE>(m_writeEvent));
        m_writeEvent = nullptr;
    }
}

#endif // _WIN32
