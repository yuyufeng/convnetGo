#include "net/Router.h"
#include "net/Firewall.h"
#include "p2p/P2PManager.h"
#include "p2p/RelayTransport.h"

#include <QStringList>

namespace {
// 从 publicId (md5:userID) 解析 userID
quint64 uidOf(const QString& pid)
{
    const int idx = pid.lastIndexOf(':');
    return idx < 0 ? 0 : pid.mid(idx + 1).toULongLong();
}
} // namespace

Router::Router(P2PManager* p2p, RelayTransport* relay)
    : m_p2p(p2p), m_relay(relay)
{
}

void Router::setRoute(quint32 cvnIp, const QString& peerPublicId)
{
    if (cvnIp == 0 || peerPublicId.isEmpty())
        return;
    std::lock_guard<std::mutex> lk(m_mtx);
    m_routes.insert(cvnIp, peerPublicId);
}

void Router::removeRoute(quint32 cvnIp)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_routes.remove(cvnIp);
}

void Router::clearRoutes()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_routes.clear();
    m_sent.clear();
    m_recv.clear();
}

void Router::traffic(const QString& peer, quint64& sent, quint64& recv) const
{
    std::lock_guard<std::mutex> lk(m_mtx);
    sent = m_sent.value(peer, 0);
    recv = m_recv.value(peer, 0);
}

quint32 Router::parseIpv4(const QString& dotted)
{
    const QStringList parts = dotted.split('.');
    if (parts.size() != 4)
        return 0;
    quint32 ip = 0;
    for (const QString& p : parts) {
        bool ok = false;
        const uint v = p.toUInt(&ok);
        if (!ok || v > 255)
            return 0;
        ip = (ip << 8) | (v & 0xFF);
    }
    return ip;
}

void Router::routeOutbound(const QByteArray& packet)
{
    // 仅处理 IPv4；TUN 为 IFF_NO_PI，无额外前缀，packet[0] 高 4 位为版本号
    if (packet.size() < 20)
        return;
    const quint8 ver = static_cast<quint8>(packet.at(0)) >> 4;
    if (ver != 4)
        return; // 忽略 IPv6/其它

    // IPv4 目的地址在字节 16..19
    const quint8 b0 = static_cast<quint8>(packet.at(16));
    const quint8 b1 = static_cast<quint8>(packet.at(17));
    const quint8 b2 = static_cast<quint8>(packet.at(18));
    const quint8 b3 = static_cast<quint8>(packet.at(19));
    const quint32 dst = (quint32(b0) << 24) | (quint32(b1) << 16) | (quint32(b2) << 8) | b3;

    QString peer;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        peer = m_routes.value(dst);
    }
    if (peer.isEmpty())
        return; // 非虚拟网内目的地，丢弃（不做 NAT）

    // 防火墙：出站过滤
    if (m_fw && !m_fw->allow(uidOf(peer), 2, packet))
        return;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_sent[peer] += static_cast<quint64>(packet.size());
    }
    // 优先 P2P；未建立则回退服务器中继
    if (!m_p2p->sendFrame(peer, packet))
        m_relay->sendFrame(peer, packet);
}

void Router::deliverInbound(const QString& peer, const QByteArray& packet)
{
    // 防火墙：入站过滤
    if (m_fw && !m_fw->allow(uidOf(peer), 1, packet))
        return;

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (!peer.isEmpty())
            m_recv[peer] += static_cast<quint64>(packet.size());
    }
    if (m_writeToTap)
        m_writeToTap(packet);
}
