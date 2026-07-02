#pragma once

// Router —— 数据面路由（纯类，非 QObject，用互斥锁保证线程安全）。
//   出站：TUN 读线程 -> routeOutbound：按目的 IP 找对端，优先 P2P，失败走中继。
//   入站：P2P(libdatachannel线程)/中继(GUI线程) -> deliverInbound -> 写回 TUN。
// L3 路由，按目的 IP 精确投递；无 ARP/广播（满足“默认不转发广播”）。

#include <QString>
#include <QByteArray>
#include <QHash>
#include <functional>
#include <mutex>

class P2PManager;
class RelayTransport;
class Firewall;

class Router {
public:
    Router(P2PManager* p2p, RelayTransport* relay);

    void setFirewall(Firewall* fw) { m_fw = fw; }
    void setRoute(quint32 cvnIp, const QString& peerPublicId);
    void removeRoute(quint32 cvnIp);
    void clearRoutes();
    void setWriteToTap(std::function<void(const QByteArray&)> writer) { m_writeToTap = std::move(writer); }

    void routeOutbound(const QByteArray& packet);              // TUN 读到的本机出站包
    void deliverInbound(const QString& peer, const QByteArray& packet); // 对端来包 -> 写 TUN

    // 按对端累计收发字节
    void traffic(const QString& peer, quint64& sent, quint64& recv) const;

    static quint32 parseIpv4(const QString& dotted); // "10.110.0.12" -> 0x0A6E000C

private:
    P2PManager* m_p2p = nullptr;
    RelayTransport* m_relay = nullptr;
    Firewall* m_fw = nullptr;
    mutable std::mutex m_mtx;
    QHash<quint32, QString> m_routes; // cvnIP -> peerPublicId
    QHash<QString, quint64> m_sent;   // peerPublicId -> 已发字节
    QHash<QString, quint64> m_recv;   // peerPublicId -> 已收字节
    std::function<void(const QByteArray&)> m_writeToTap;
};
