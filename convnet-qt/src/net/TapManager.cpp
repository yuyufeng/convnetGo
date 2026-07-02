#include "net/TapManager.h"
#include "net/ITapDevice.h"

#include <vector>

namespace {
constexpr int kBufSize = 2048; // 足够容纳一个 MTU 的 IP 包
}

TapManager::TapManager(QObject* parent) : QObject(parent) {}

TapManager::~TapManager() { stop(); }

bool TapManager::start(const QString& ip, int prefixLen,
                       std::function<void(const QByteArray&)> outbound, QString& err)
{
    if (m_running.load())
        return true;

    m_dev = createTapDevice();
    QString ifn;
    if (!m_dev->open(ifn, ip, prefixLen, err)) {
        delete m_dev;
        m_dev = nullptr;
        return false;
    }
    m_ifName = ifn;
    m_outbound = std::move(outbound);
    m_running.store(true);
    m_thread = std::thread([this]() { readLoop(); });
    emit started(m_ifName);
    return true;
}

void TapManager::readLoop()
{
    std::vector<char> buf(kBufSize);
    while (m_running.load()) {
        const int n = m_dev->readPacket(buf.data(), static_cast<int>(buf.size()));
        if (n == 0)
            continue; // 超时无数据：回到循环顶部检查 m_running（便于快速停止）
        if (n < 0) {
            if (m_running.load())
                emit error(QStringLiteral("网卡读取错误"));
            break;
        }
        QByteArray pkt(buf.data(), n);
        if (m_outbound)
            m_outbound(pkt); // -> Router::routeOutbound（读线程内，纯类线程安全）
    }
}

void TapManager::stop()
{
    m_running.store(false);
    // 先等读线程退出（读为超时式，~200ms 内返回并看到 m_running=false），
    // 再关闭设备——避免关闭后读线程仍调用已释放的驱动（Wintun DLL）而崩溃。
    if (m_thread.joinable())
        m_thread.join();
    if (m_dev) {
        m_dev->close();
        delete m_dev;
        m_dev = nullptr;
    }
}

void TapManager::write(const QByteArray& packet)
{
    std::lock_guard<std::mutex> lk(m_writeMtx);
    if (m_dev)
        m_dev->writePacket(packet.constData(), static_cast<int>(packet.size()));
}
