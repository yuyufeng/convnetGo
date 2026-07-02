#include "model/Identity.h"

#include <QSettings>
#include <QRandomGenerator>

Identity& Identity::instance()
{
    static Identity inst;
    return inst;
}

void Identity::ensureDefaults()
{
    if (mac.isEmpty()) {
        // 生成一个本地管理的伪 MAC：02:xx:xx:xx:xx:xx（每台机器一份）
        quint32 r1 = QRandomGenerator::global()->generate();
        quint32 r2 = QRandomGenerator::global()->generate();
        mac = QString("02:%1:%2:%3:%4:%5")
                  .arg((r1 >> 0) & 0xFF, 2, 16, QChar('0'))
                  .arg((r1 >> 8) & 0xFF, 2, 16, QChar('0'))
                  .arg((r1 >> 16) & 0xFF, 2, 16, QChar('0'))
                  .arg((r2 >> 0) & 0xFF, 2, 16, QChar('0'))
                  .arg((r2 >> 8) & 0xFF, 2, 16, QChar('0'));
    }

    if (serverHost.isEmpty())
        serverHost = QStringLiteral("127.0.0.1");
    if (serverPort == 0)
        serverPort = 13903;
}

void Identity::load()
{
    QSettings s("convnet", "convnet-qt");
    account = s.value("account").toString();
    rememberPassword = s.value("rememberPassword", false).toBool();
    password = rememberPassword ? s.value("password").toString() : QString();
    nick = s.value("nick").toString();
    mac = s.value("mac").toString();
    serverHost = s.value("serverHost", "127.0.0.1").toString();
    serverPort = static_cast<quint16>(s.value("serverPort", 13903).toUInt());
    ensureDefaults();
    save(); // 回写首次生成的默认值（如伪 MAC）
}

void Identity::save()
{
    QSettings s("convnet", "convnet-qt");
    s.setValue("account", account);
    s.setValue("rememberPassword", rememberPassword);
    if (rememberPassword)
        s.setValue("password", password);
    else
        s.remove("password");
    s.setValue("nick", nick);
    s.setValue("mac", mac);
    s.setValue("serverHost", serverHost);
    s.setValue("serverPort", serverPort);
}

quint64 Identity::userId() const
{
    const int idx = publicId.lastIndexOf(':');
    if (idx < 0)
        return 0;
    return publicId.mid(idx + 1).toULongLong();
}
