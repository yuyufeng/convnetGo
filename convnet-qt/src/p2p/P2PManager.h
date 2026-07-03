#pragma once

// P2PManager —— 管理与所有网络对端的 PeerLink。
// 负责：按 publicId 去重建链、glare 避免（publicId 较小者主叫）、
// 信令收发（C_CONNTOWS_PEERCALL / _RESP）、把每个对端的对接方式暴露给 UI。

#include <QObject>
#include <QHash>
#include <QString>
#include <QByteArray>
#include <QJsonArray>
#include <functional>
#include <mutex>

class SignalingClient;
class PeerLink;

class P2PManager : public QObject {
    Q_OBJECT
public:
    explicit P2PManager(SignalingClient* sig, QObject* parent = nullptr);

    void setSelfPublicId(const QString& pid) { m_selfPublicId = pid; }
    void reset();                                    // 登出/断开清理
    void ensureLink(const QString& peerPublicId);    // 主动建链（仅主叫方发起）
    void handlePeerCall(const QJsonArray& msg);      // C_CONNTOWS_PEERCALL_RESP
    void handlePeerOffline(const QJsonArray& msg);   // C_CONNTOWS_PEERCALL_RESP_NOTONLINE

    QString methodText(const QString& peerPublicId) const; // 中文对接方式
    int     rttMs(const QString& peerPublicId) const;        // 往返时延(ms)，-1=未知
    bool    isP2PDirect(const QString& peerPublicId) const;  // 是否 P2P 直连（否则视为经服务器中转）

    // 数据面：出站经 P2P 发送（未建立则返回 false，交由中继）；入站帧统一回调
    bool sendFrame(const QString& peerPublicId, const QByteArray& data);
    void setInboundSink(std::function<void(const QString&, const QByteArray&)> sink) { m_inboundSink = std::move(sink); }

signals:
    void peerMethodChanged(const QString& peerPublicId);

private slots:
    void onLinkSendOfferAnswer(const QString& peerPublicId, const QString& sdp, const QString& step);
    void onLinkMethodChanged(const QString& peerPublicId);

private:
    PeerLink* createLink(const QString& peerPublicId);

    SignalingClient* m_sig = nullptr;
    QString m_selfPublicId;
    QHash<QString, PeerLink*> m_links; // 受 m_linksMtx 保护（TUN 线程读、GUI 线程写）
    mutable std::mutex m_linksMtx;
    std::function<void(const QString&, const QByteArray&)> m_inboundSink;
};
