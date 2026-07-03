#pragma once

// TapManager —— 虚拟网卡收发的线程封装。
//   读：独立 std::thread 阻塞读 IP 包 -> outbound 回调（数据面，不经 Qt）。
//   写：write() 线程安全（P2P 线程 / GUI 线程都会调用）。

#include <QObject>
#include <QString>
#include <QByteArray>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#include "net/ITapDevice.h" // NicMode

class ITapDevice;

class TapManager : public QObject {
    Q_OBJECT
public:
    explicit TapManager(QObject* parent = nullptr);
    ~TapManager() override;

    // 启动网卡：ip=本机虚拟IP，prefixLen=8，mode=TUN/TAP；outbound=读到的包/帧回调（读线程调用）。
    bool start(const QString& ip, int prefixLen, NicMode mode,
               std::function<void(const QByteArray&)> outbound, QString& err);
    void stop();
    bool isRunning() const { return m_running.load(); }
    bool isLayer2() const { return m_layer2; } // TAP=true

    void write(const QByteArray& packet); // 入站写网卡，线程安全
    QString ifName() const { return m_ifName; }

signals:
    void started(const QString& ifName);
    void error(const QString& message);

private:
    void readLoop();

    ITapDevice* m_dev = nullptr;
    bool m_layer2 = false;
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::mutex m_writeMtx;
    std::function<void(const QByteArray&)> m_outbound;
    QString m_ifName;
};
