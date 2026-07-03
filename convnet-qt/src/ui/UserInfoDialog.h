#pragma once

// 共享的“用户资料 / 虚拟IP”弹窗，好友列表与群成员列表复用。
// header-only inline，避免改 CMake；无 Q_OBJECT，不需 moc。

#include "model/Types.h"
#include "model/Identity.h"

#include <QWidget>
#include <QMessageBox>
#include <QString>

// 人类可读的字节数
inline QString humanBytes(quint64 n)
{
    if (n < 1024ull)
        return QStringLiteral("%1 B").arg(n);
    if (n < 1024ull * 1024)
        return QStringLiteral("%1 KB").arg(n / 1024.0, 0, 'f', 1);
    if (n < 1024ull * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(n / (1024.0 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(n / (1024.0 * 1024 * 1024), 0, 'f', 2);
}

// sent/recv：与该对端的累计收发字节（数据面）
inline void showUserInfoDialog(QWidget* parent, const FriendInfo& f,
                               quint64 sent = 0, quint64 recv = 0)
{
    // 网卡模式行：对端 TUN/TAP；在线且与本机不一致时红字告警（无法互通）。
    const QString myMode = Identity::instance().nicMode;
    QString modeLine;
    if (f.nicMode.isEmpty()) {
        modeLine = QStringLiteral("未知");
    } else {
        const QString peerLabel = (f.nicMode == QLatin1String("tap"))
                                      ? QStringLiteral("TAP（L2）")
                                      : QStringLiteral("TUN（L3）");
        if (f.online && !myMode.isEmpty() && f.nicMode != myMode) {
            modeLine = QStringLiteral(
                           "<span style='color:#f85149'><b>%1 ⚠ 与你(%2)不一致，虚拟网络无法互通</b></span>")
                           .arg(peerLabel, myMode == QLatin1String("tap") ? QStringLiteral("TAP")
                                                                          : QStringLiteral("TUN"));
        } else {
            modeLine = peerLabel;
        }
    }
    const QString modeRow = QStringLiteral("网卡模式：%1<br>").arg(modeLine); // 预先成行，避免 %10 占位歧义

    const QString html =
        (QStringLiteral("<b>%1</b><br><br>"
                        "用户ID：%2<br>"
                        "虚拟IP：<b>%3</b><br>"
                        "MAC：%4<br>"
                        "状态：%5<br>"
                        "对接方式：%6<br>")
         + modeRow
         + QStringLiteral("已发送：%7<br>"
                          "已接收：%8<br>"
                          "PublicID：<span style='font-size:small'>%9</span>"))
            .arg(f.nick.toHtmlEscaped())
            .arg(f.userId)
            .arg(f.cvnIP.isEmpty() ? QStringLiteral("(未分配)") : f.cvnIP.toHtmlEscaped())
            .arg(f.mac.isEmpty() ? QStringLiteral("-") : f.mac.toHtmlEscaped())
            .arg(f.online ? QStringLiteral("在线") : QStringLiteral("离线"))
            .arg(f.connMethod.isEmpty() ? QStringLiteral("未连接（消息经服务器）") : f.connMethod.toHtmlEscaped())
            .arg(humanBytes(sent))
            .arg(humanBytes(recv))
            .arg(f.publicId.toHtmlEscaped());

    QMessageBox box(parent);
    box.setWindowTitle(QStringLiteral("用户资料"));
    box.setTextFormat(Qt::RichText);
    box.setText(html);
    box.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    box.exec();
}
