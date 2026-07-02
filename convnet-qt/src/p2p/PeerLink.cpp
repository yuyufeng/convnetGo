#include "p2p/PeerLink.h"

#include <QTimer>

#include <rtc/rtc.hpp>

namespace {
constexpr int kP2pTimeoutMs = 12000; // 超时未连通则判为需走中继
}

PeerLink::PeerLink(const QString& peerPublicId, QObject* parent)
    : QObject(parent), m_peerPublicId(peerPublicId)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kP2pTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (m_method != Method::P2P)
            setMethod(Method::Relay); // ICE 未在时限内连通 -> 回退服务器中继
    });
}

PeerLink::~PeerLink()
{
    // 先摘除回调，避免析构期间 libdatachannel 线程回调命中已销毁对象
    if (m_dc) {
        m_dc->onOpen(nullptr);
        m_dc->onClosed(nullptr);
        m_dc->onMessage(nullptr);
    }
    if (m_pc) {
        m_pc->onStateChange(nullptr);
        m_pc->onGatheringStateChange(nullptr);
        m_pc->onDataChannel(nullptr);
        m_pc->close();
    }
}

void PeerLink::setupPc()
{
    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");
    m_pc = std::make_shared<rtc::PeerConnection>(config);

    m_pc->onStateChange([this](rtc::PeerConnection::State state) {
        using S = rtc::PeerConnection::State;
        if (state == S::Connected)
            setMethod(Method::P2P);
        else if (state == S::Failed)
            setMethod(Method::Relay); // 直连失败 -> 回退中继
    });

    // 非 trickle：等 ICE 收集完成，取完整本地 SDP（含所有候选）一次性发出
    m_pc->onGatheringStateChange([this](rtc::PeerConnection::GatheringState state) {
        if (state != rtc::PeerConnection::GatheringState::Complete)
            return;
        auto desc = m_pc->localDescription();
        if (!desc)
            return;
        const QString sdp = QString::fromStdString(std::string(*desc));
        const QString wireStep = m_caller ? QStringLiteral("1") : QStringLiteral("2");
        emit sendOfferAnswer(m_peerPublicId, sdp, wireStep); // 排队到 GUI 线程实际发送
    });

    // 被叫端由对端 offer 触发生成的 DataChannel
    m_pc->onDataChannel([this](std::shared_ptr<rtc::DataChannel> dc) {
        m_dc = std::move(dc);
        bindDc();
    });
}

void PeerLink::bindDc()
{
    if (!m_dc)
        return;
    m_dc->onOpen([this]() { setMethod(Method::P2P); });
    m_dc->onMessage([this](rtc::message_variant msg) {
        if (std::holds_alternative<rtc::binary>(msg)) {
            const rtc::binary& bin = std::get<rtc::binary>(msg);
            QByteArray data(reinterpret_cast<const char*>(bin.data()),
                            static_cast<int>(bin.size()));
            if (m_frameSink)
                m_frameSink(m_peerPublicId, data); // 数据面：直接写入路由，不经 Qt 事件循环
        }
    });
}

void PeerLink::startAsCaller()
{
    m_caller = true;
    setupPc();
    m_dc = m_pc->createDataChannel("data"); // 触发自动协商生成 offer
    bindDc();
    m_timeout->start();
}

void PeerLink::onRemoteOffer(const QString& sdp)
{
    m_caller = false;
    setupPc();
    m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), "offer")); // 自动生成 answer
    m_timeout->start();
}

void PeerLink::onRemoteAnswer(const QString& sdp)
{
    if (!m_pc)
        return;
    m_pc->setRemoteDescription(rtc::Description(sdp.toStdString(), "answer"));
}

int PeerLink::rttMs() const
{
    if (!m_pc)
        return -1;
    auto r = m_pc->rtt(); // libdatachannel SCTP 往返时延
    return r ? static_cast<int>(r->count()) : -1;
}

bool PeerLink::sendFrame(const QByteArray& data)
{
    if (!m_dc || !m_dc->isOpen())
        return false;
    m_dc->send(reinterpret_cast<const std::byte*>(data.constData()),
               static_cast<size_t>(data.size()));
    return true;
}

void PeerLink::setMethod(Method m)
{
    if (m == m_method)
        return;
    m_method = m;
    emit methodChanged(m_peerPublicId);
}
