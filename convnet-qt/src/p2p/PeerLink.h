#pragma once

// PeerLink —— 与单个对端的 WebRTC 连接（libdatachannel）。
// 复用现有信令协议 C_CONNTOWS_PEERCALL：非 trickle、完整 SDP、DataChannel 标签 "data"，
// 与遗留 Go(pion) 互通。libdatachannel 回调在其自有线程触发，本类只发 Qt 信号
// （排队投递到 GUI 线程），不直接碰 socket/UI。

#include <QObject>
#include <QString>
#include <QByteArray>
#include <memory>
#include <functional>

namespace rtc {
class PeerConnection;
class DataChannel;
}

class QTimer;

class PeerLink : public QObject {
    Q_OBJECT
public:
    enum class Method { Connecting, P2P, Relay, Failed };

    PeerLink(const QString& peerPublicId, QObject* parent = nullptr);
    ~PeerLink() override;

    void startAsCaller();                     // 主叫：建 DataChannel -> 自动生成 offer
    void onRemoteOffer(const QString& sdp);   // 被叫：收到 offer -> 自动生成 answer
    void onRemoteAnswer(const QString& sdp);  // 主叫：收到 answer

    Method  method() const { return m_method; }
    QString peerPublicId() const { return m_peerPublicId; }
    int     rttMs() const;                      // 往返时延(ms)；未知/未连通返回 -1
    bool    sendFrame(const QByteArray& data); // 经 DataChannel 发送；未打开返回 false

    // 入站帧回调（数据面，直接调用，不经 Qt 事件循环）。在 libdatachannel 线程触发。
    // 回调带上对端 publicId，便于按对端计流量。
    void setFrameSink(std::function<void(const QString&, const QByteArray&)> sink) { m_frameSink = std::move(sink); }

signals:
    // 本地 SDP 收集完成，需经信令发给对端（wireStep: "1"=offer, "2"=answer）
    void sendOfferAnswer(const QString& peerPublicId, const QString& sdp, const QString& wireStep);
    void methodChanged(const QString& peerPublicId);

private:
    void setupPc();
    void bindDc();
    void setMethod(Method m);

    QString m_peerPublicId;
    bool    m_caller = false;
    Method  m_method = Method::Connecting;

    std::shared_ptr<rtc::PeerConnection> m_pc;
    std::shared_ptr<rtc::DataChannel> m_dc;
    QTimer* m_timeout = nullptr; // 若超时未连通，判定回退中继
    std::function<void(const QString&, const QByteArray&)> m_frameSink;
};
