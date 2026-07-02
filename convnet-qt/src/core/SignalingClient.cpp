#include "core/SignalingClient.h"
#include "core/FrameCodec.h"

#include <QTcpSocket>

SignalingClient::SignalingClient(QObject* parent)
    : QObject(parent)
{
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &SignalingClient::onConnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &SignalingClient::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &SignalingClient::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &SignalingClient::onErrorOccurred);
}

void SignalingClient::connectToServer(const QString& host, quint16 port)
{
    m_buffer.clear();
    m_socket->abort();
    m_socket->connectToHost(host, port);
}

void SignalingClient::disconnectFromServer()
{
    m_socket->disconnectFromHost();
}

bool SignalingClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

bool SignalingClient::send(int cmd, const QJsonArray& message)
{
    if (!isConnected())
        return false;
    const QByteArray frame = FrameCodec::encode(cmd, message);
    return m_socket->write(frame) == frame.size();
}

void SignalingClient::onConnected()
{
    emit connected();
}

void SignalingClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    const QVector<Frame> frames = FrameCodec::drain(m_buffer);
    for (const Frame& f : frames)
        emit frameReceived(f.cmd, f.message);
}

void SignalingClient::onDisconnected()
{
    emit disconnected();
}

void SignalingClient::onErrorOccurred()
{
    QString msg;
    switch (m_socket->error()) {
    case QAbstractSocket::ConnectionRefusedError:
        msg = QStringLiteral("连不上服务器：连接被拒绝（服务未启动或端口不对？）");
        break;
    case QAbstractSocket::HostNotFoundError:
        msg = QStringLiteral("连不上服务器：找不到主机地址");
        break;
    case QAbstractSocket::SocketTimeoutError:
        msg = QStringLiteral("连不上服务器：连接超时");
        break;
    case QAbstractSocket::NetworkError:
        msg = QStringLiteral("连不上服务器：网络不可达");
        break;
    case QAbstractSocket::RemoteHostClosedError:
        msg = QStringLiteral("服务器关闭了连接");
        break;
    default:
        msg = QStringLiteral("连不上服务器：%1").arg(m_socket->errorString());
        break;
    }
    emit socketError(msg);
}
