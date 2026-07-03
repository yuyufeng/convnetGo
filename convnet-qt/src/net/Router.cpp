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

void Router::addPeer(const QString& peerPublicId)
{
    if (peerPublicId.isEmpty())
        return;
    std::lock_guard<std::mutex> lk(m_mtx);
    m_peers.insert(peerPublicId);
}

void Router::removePeer(const QString& peerPublicId)
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_peers.remove(peerPublicId);
    // 清掉指向该对端的 MAC 学习项
    for (auto it = m_macToPeer.begin(); it != m_macToPeer.end();) {
        if (it.value() == peerPublicId)
            it = m_macToPeer.erase(it);
        else
            ++it;
    }
}

void Router::clearRoutes()
{
    std::lock_guard<std::mutex> lk(m_mtx);
    m_routes.clear();
    m_peers.clear();
    m_macToPeer.clear();
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

void Router::sendTo(const QString& peer, const QByteArray& packet, int fwDir)
{
    // 出站/入站防火墙：L3 按包内容(端口/协议)，L2 仅按对端“拉黑”
    if (m_fw) {
        if (m_layer2) {
            if (m_fw->peerBlocked(uidOf(peer), fwDir))
                return;
        } else if (!m_fw->allow(uidOf(peer), fwDir, packet)) {
            return;
        }
    }
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        m_sent[peer] += static_cast<quint64>(packet.size());
    }
    // 优先 P2P；未建立则回退服务器中继
    if (!m_p2p->sendFrame(peer, packet))
        m_relay->sendFrame(peer, packet);
}

void Router::fanoutL2(const QByteArray& frame)
{
    QList<QString> peers;
    {
        std::lock_guard<std::mutex> lk(m_mtx);
        peers = m_peers.values();
    }
    // 本机产生的广播/组播/未知单播 -> 发给所有对端各一份（不经其它对端转发）
    for (const QString& peer : peers)
        sendTo(peer, frame, 2);
}

void Router::routeOutbound(const QByteArray& packet)
{
    if (m_layer2) {
        // L2：以太网帧，前 14 字节以太头（dstMAC[0..5], srcMAC[6..11], ethertype[12..13]）
        if (packet.size() < 14)
            return;
        const unsigned char* p = reinterpret_cast<const unsigned char*>(packet.constData());
        const bool bcast = p[0] == 0xff && p[1] == 0xff && p[2] == 0xff
                        && p[3] == 0xff && p[4] == 0xff && p[5] == 0xff;
        const bool group = (p[0] & 0x01) != 0; // 组播/广播位（含广播）
        if (bcast || group) {
            fanoutL2(packet); // 广播/组播 -> 所有对端（游戏发现等）
            return;
        }
        // 单播：查 MAC 学习表
        const QByteArray dstMac(reinterpret_cast<const char*>(p), 6);
        QString peer;
        {
            std::lock_guard<std::mutex> lk(m_mtx);
            peer = m_macToPeer.value(dstMac);
        }
        if (peer.isEmpty()) {
            fanoutL2(packet); // 未知单播 -> 泛洪（学习式交换），对端网卡按 dstMAC 自行取舍
            return;
        }
        sendTo(peer, packet, 2);
        return;
    }

    // ---- L3(TUN)：按目的 IP 路由 ----
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
    sendTo(peer, packet, 2);
}

void Router::deliverInbound(const QString& peer, const QByteArray& packet)
{
    if (m_layer2) {
        // 学习 srcMAC(帧[6..11]) -> peer，供后续单播直达
        if (packet.size() >= 12 && !peer.isEmpty()) {
            const QByteArray srcMac(packet.constData() + 6, 6);
            std::lock_guard<std::mutex> lk(m_mtx);
            m_macToPeer.insert(srcMac, peer);
        }
        // 入站防火墙：L2 仅按对端拉黑
        if (m_fw && m_fw->peerBlocked(uidOf(peer), 1))
            return;
    } else {
        // L3 入站防火墙：按包内容
        if (m_fw && !m_fw->allow(uidOf(peer), 1, packet))
            return;
    }

    {
        std::lock_guard<std::mutex> lk(m_mtx);
        if (!peer.isEmpty())
            m_recv[peer] += static_cast<quint64>(packet.size());
    }
    if (m_writeToTap)
        m_writeToTap(packet); // 只写本地网卡；L2 不再向其它对端转发（不做广播转发）
}
