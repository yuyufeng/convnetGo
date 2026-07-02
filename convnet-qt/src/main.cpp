#include <QApplication>
#include <QFont>
#include <QFontDatabase>

#include <cstdlib>
#include <cstdio>

#include "core/SignalingClient.h"
#include "model/AppModel.h"
#include "model/Identity.h"
#include "model/Types.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

static QString firstAvailable(const QStringList& candidates, const QStringList& installed)
{
    for (const QString& f : candidates)
        if (installed.contains(f))
            return f;
    return QString();
}

// 选择带中文字形的主字体，并显式指定 emoji 字体，避免在缺省字体无 CJK/表情
// 覆盖的环境（如未装字体的 WSL）里中文或表情显示为方框（tofu）。
// 重要：Linux/WSL 上表情方框的根因是「没装 emoji 字体」——代码无法凭空造字形，
// 仍需先安装：sudo apt install fonts-noto-color-emoji && fc-cache -f
// （Windows 自带 Segoe UI Emoji，无需安装。）
static void applyAppFonts()
{
    const QStringList installed = QFontDatabase::families();

    static const QStringList preferredCjk = {
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans SC"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Zen Hei"),
        QStringLiteral("WenQuanYi Micro Hei"),
        QStringLiteral("PingFang SC"),
        QStringLiteral("SimHei"),
        QStringLiteral("SimSun"),
    };
    static const QStringList preferredEmoji = {
        QStringLiteral("Noto Color Emoji"),
        QStringLiteral("Segoe UI Emoji"),
        QStringLiteral("Apple Color Emoji"),
        QStringLiteral("Twemoji Mozilla"),
        QStringLiteral("Noto Emoji"),
        QStringLiteral("Symbola"),
    };

    const QString cjk = firstAvailable(preferredCjk, installed);
    const QString emoji = firstAvailable(preferredEmoji, installed);

    // Qt 6.9+：表情段由专门的 emoji 字体路径解析。这才是真正决定 emoji 用哪套
    // 字体的开关（而非下面的 families 列表——那对 emoji 基本是冗余的）。
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    if (!emoji.isEmpty())
        QFontDatabase::setApplicationEmojiFontFamilies({emoji});
#endif

    QStringList fams;
    if (!cjk.isEmpty())
        fams << cjk;    // 主字体：中文/拉丁
    if (!emoji.isEmpty())
        fams << emoji;  // 兜底（利于 CJK 覆盖的确定性，对 emoji 无害）
    if (fams.isEmpty())
        return;

    QFont f = QApplication::font();
    f.setFamilies(fams);
    QApplication::setFont(f);
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("convnet"));
    app.setApplicationName(QStringLiteral("convnet-qt"));

    applyAppFonts();
    app.setStyleSheet(darkThemeQss()); // 全局深色主题（Radmin 风格）

    // 自定义类型注册（同线程直连不强制，但注册后更稳妥）
    qRegisterMetaType<FriendInfo>("FriendInfo");
    qRegisterMetaType<GroupInfo>("GroupInfo");
    qRegisterMetaType<ChatMessage>("ChatMessage");

    Identity::instance().load();

    SignalingClient sig;
    AppModel model(&sig);
    MainWindow win(&model);
    win.show();

    const int rc = app.exec();
    // 立即结束进程，跳过较慢的对象析构（虚拟网卡线程、libdatachannel PeerConnection 等），
    // 避免关闭时界面卡顿。配置(QSettings)每次修改已即时落盘，无数据丢失。
    std::fflush(nullptr);
    std::_Exit(rc);
    return rc; // 不可达
}
