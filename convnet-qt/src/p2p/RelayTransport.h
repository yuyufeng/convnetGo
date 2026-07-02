#pragma once

// RelayTransport —— P2P 失败时的数据面回退：把帧经信令服务器盲转发（RELAY_DATA）。
// sendFrame 可能从 TUN 读线程调用，内部 marshal 到 GUI 线程再写 socket。

#include <QObject>
#include <QString>
#include <QByteArray>

class SignalingClient;

class RelayTransport : public QObject {
    Q_OBJECT
public:
    explicit RelayTransport(SignalingClient* sig, QObject* parent = nullptr);

    // 出站：帧 -> base64 -> RELAY_DATA[targetPublicID, blob]。线程安全。
    void sendFrame(const QString& peerPublicId, const QByteArray& frame);

private:
    SignalingClient* m_sig = nullptr;
};
