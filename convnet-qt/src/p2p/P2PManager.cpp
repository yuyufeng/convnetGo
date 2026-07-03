#include "p2p/P2PManager.h"
#include "p2p/PeerLink.h"
#include "core/Protocol.h"
#include "core/SignalingClient.h"

#include <QJsonObject>

using namespace proto;

P2PManager::P2PManager(SignalingClient* sig, QObject* parent)
    : QObject(parent), m_sig(sig)
{
}

void P2PManager::reset()
{
    std::lock_guard<std::mutex> lk(m_linksMtx);
    for (PeerLink* link : m_links)
        link->deleteLater();
    m_links.clear();
}

PeerLink* P2PManager::createLink(const QString& peerPublicId)
{
    auto* link = new PeerLink(peerPublicId, this);
    connect(link, &PeerLink::sendOfferAnswer, this, &P2PManager::onLinkSendOfferAnswer);
    connect(link, &PeerLink::methodChanged, this, &P2PManager::onLinkMethodChanged);
    if (m_inboundSink)
        link->setFrameSink(m_inboundSink); // 数据面直连回调
    {
        std::lock_guard<std::mutex> lk(m_linksMtx);
        m_links.insert(peerPublicId, link);
    }
    return link;
}

bool P2PManager::sendFrame(const QString& peerPublicId, const QByteArray& data)
{
    PeerLink* link;
    {
        std::lock_guard<std::mutex> lk(m_linksMtx); // TUN 线程读，与 GUI 线程写互斥
        link = m_links.value(peerPublicId, nullptr);
    }
    // deleteLater 语义保证对象不会在本次调用中被立即释放
    return link && link->sendFrame(data);
}

void P2PManager::ensureLink(const QString& peerPublicId)
{
    if (peerPublicId.isEmpty() || peerPublicId == m_selfPublicId)
        return;
    if (m_links.contains(peerPublicId))
        return;
    // glare 避免：仅 publicId 较小的一方主动发起 offer，另一方等待来电
    if (m_selfPublicId < peerPublicId) {
        PeerLink* link = createLink(peerPublicId);
        link->startAsCaller();
        emit peerMethodChanged(peerPublicId); // 进入“连接中”
    }
    // 否则等待对端的 offer（handlePeerCall step "1"）
}

void P2PManager::handlePeerCall(const QJsonArray& msg)
{
    const QJsonObject caller = msg.at(0).toObject();
    const QString peer = caller.value("PublicID").toString();
    const QString sdp = msg.at(1).toArray().at(0).toString();
    const QString step = msg.at(2).toString();
    if (peer.isEmpty())
        return;

    if (step == QLatin1String("1")) {
        // 收到 offer。若本方是主叫（publicId 较小）则为 glare，忽略之。
        if (m_selfPublicId < peer)
            return;
        PeerLink* link = m_links.value(peer, nullptr);
        if (!link)
            link = createLink(peer);
        link->onRemoteOffer(sdp);
        emit peerMethodChanged(peer);
    } else if (step == QLatin1String("2")) {
        // 收到 answer（本方为主叫）
        if (PeerLink* link = m_links.value(peer, nullptr))
            link->onRemoteAnswer(sdp);
    }
}

void P2PManager::handlePeerOffline(const QJsonArray& msg)
{
    const QString peer = msg.at(0).toString();
    PeerLink* link = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_linksMtx);
        link = m_links.take(peer);
    }
    if (link) {
        link->deleteLater();
        emit peerMethodChanged(peer); // 变回“未连接”
    }
}

void P2PManager::onLinkSendOfferAnswer(const QString& peerPublicId, const QString& sdp, const QString& step)
{
    // GUI 线程执行（PeerLink 信号排队投递到此），socket 写入安全
    m_sig->send(C_CONNTOWS_PEERCALL, {peerPublicId, sdp, step, QString()});
}

void P2PManager::onLinkMethodChanged(const QString& peerPublicId)
{
    emit peerMethodChanged(peerPublicId);
}

int P2PManager::rttMs(const QString& peerPublicId) const
{
    std::lock_guard<std::mutex> lk(m_linksMtx);
    PeerLink* link = m_links.value(peerPublicId, nullptr);
    return link ? link->rttMs() : -1;
}

bool P2PManager::isP2PDirect(const QString& peerPublicId) const
{
    std::lock_guard<std::mutex> lk(m_linksMtx);
    PeerLink* link = m_links.value(peerPublicId, nullptr);
    return link && link->method() == PeerLink::Method::P2P;
}

QString P2PManager::methodText(const QString& peerPublicId) const
{
    PeerLink* link = m_links.value(peerPublicId, nullptr);
    if (!link)
        return QStringLiteral("未连接");
    switch (link->method()) {
    case PeerLink::Method::P2P:
        return QStringLiteral("P2P 直连");
    case PeerLink::Method::Relay:
        return QStringLiteral("服务器中继");
    case PeerLink::Method::Failed:
        return QStringLiteral("连接失败");
    case PeerLink::Method::Connecting:
    default:
        return QStringLiteral("连接中");
    }
}
