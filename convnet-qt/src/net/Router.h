#pragma once

// Router —— 数据面路由（纯类，非 QObject，用互斥锁保证线程安全）。
//   出站：TAP/TUN 读线程 -> routeOutbound；入站：P2P/中继 -> deliverInbound -> 写回网卡。
// 两种模式：
//   L3(TUN)：按目的 IP 精确投递；无 ARP/广播。
//   L2(TAP)：以太网学习式交换——单播按学习到的 MAC 找对端；广播/组播/未知单播 fanout
//            给所有对端一次；收到的帧只写本地网卡、不再向其它对端转发（不做广播转发）。

#include <QString>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <functional>
#include <mutex>

class P2PManager;
class RelayTransport;
class Firewall;

class Router {
public:
    Router(P2PManager* p2p, RelayTransport* relay);

    void setFirewall(Firewall* fw) { m_fw = fw; }
    void setLayer2(bool on) { m_layer2 = on; } // true=TAP(L2 交换)，false=TUN(L3 路由)
    void setRoute(quint32 cvnIp, const QString& peerPublicId); // L3：cvnIP -> 对端
    void removeRoute(quint32 cvnIp);
    void addPeer(const QString& peerPublicId);                 // L2：登记虚拟网内对端（fanout 用）
    void removePeer(const QString& peerPublicId);
    void clearRoutes();
    void setWriteToTap(std::function<void(const QByteArray&)> writer) { m_writeToTap = std::move(writer); }

    void routeOutbound(const QByteArray& packet);              // TUN 读到的本机出站包
    void deliverInbound(const QString& peer, const QByteArray& packet); // 对端来包 -> 写 TUN

    // 按对端累计收发字节
    void traffic(const QString& peer, quint64& sent, quint64& recv) const;

    static quint32 parseIpv4(const QString& dotted); // "10.110.0.12" -> 0x0A6E000C

private:
    void sendTo(const QString& peer, const QByteArray& packet, int fwDir); // 单个对端（含出站防火墙+计数）
    void fanoutL2(const QByteArray& frame);                                // 广播/组播/未知单播 -> 所有对端

    P2PManager* m_p2p = nullptr;
    RelayTransport* m_relay = nullptr;
    Firewall* m_fw = nullptr;
    bool m_layer2 = false;
    mutable std::mutex m_mtx;
    QHash<quint32, QString> m_routes;       // L3：cvnIP -> peerPublicId
    QSet<QString> m_peers;                  // L2：虚拟网内所有对端（fanout 用）
    QHash<QByteArray, QString> m_macToPeer; // L2：学习到的 dstMAC(6字节) -> peerPublicId
    QHash<QString, quint64> m_sent;   // peerPublicId -> 已发字节
    QHash<QString, quint64> m_recv;   // peerPublicId -> 已收字节
    std::function<void(const QByteArray&)> m_writeToTap;
};
