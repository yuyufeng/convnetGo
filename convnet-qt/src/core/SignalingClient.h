#pragma once

// SignalingClient —— 与 Go 信令服务器（默认 :13903）的 TCP 连接。
// QTcpSocket 异步事件驱动，运行在 GUI 线程即可（控制面无阻塞 I/O）。
// M3 的 P2P/TAP 阻塞循环届时另起 worker 线程。

#include <QObject>
#include <QByteArray>
#include <QJsonArray>
#include <QString>

class QTcpSocket;

class SignalingClient : public QObject {
    Q_OBJECT
public:
    explicit SignalingClient(QObject* parent = nullptr);

    void connectToServer(const QString& host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;

    // 发送任意 opcode。未连接时静默丢弃（返回 false）。
    bool send(int cmd, const QJsonArray& message);

signals:
    void connected();
    void disconnected();
    void socketError(const QString& message);
    void frameReceived(int cmd, const QJsonArray& message);

private slots:
    void onConnected();
    void onReadyRead();
    void onDisconnected();
    void onErrorOccurred();

private:
    QTcpSocket* m_socket = nullptr;
    QByteArray m_buffer;
};
