#include "p2p/RelayTransport.h"
#include "core/Protocol.h"
#include "core/SignalingClient.h"

#include <QMetaObject>

RelayTransport::RelayTransport(SignalingClient* sig, QObject* parent)
    : QObject(parent), m_sig(sig)
{
}

void RelayTransport::sendFrame(const QString& peerPublicId, const QByteArray& frame)
{
    const QString blob = QString::fromLatin1(frame.toBase64());
    // marshal 到本对象所属线程（GUI），保证 QTcpSocket 写入在其线程内进行
    QMetaObject::invokeMethod(
        this,
        [this, peerPublicId, blob]() {
            m_sig->send(proto::RELAY_DATA, {peerPublicId, blob});
        },
        Qt::QueuedConnection);
}
