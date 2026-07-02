#pragma once

#include <QWidget>
#include "model/Types.h"

class AppModel;
class QTextBrowser;
class QTextEdit;
class QListWidget;
class QTimer;

// 一个会话窗口（单聊或群聊）。自行订阅 AppModel::chatMessageReceived 并过滤本会话消息。
class ChatWindow : public QWidget {
    Q_OBJECT
public:
    ChatWindow(AppModel* model, const QString& scope, quint64 targetId,
               const QString& title, QWidget* parent = nullptr);

signals:
    void activated(const QString& scope, quint64 targetId);

protected:
    void changeEvent(QEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override; // 输入框回车发送

private slots:
    void onChatMessage(const ChatMessage& m);
    void onSend();
    void refreshMembers();                       // 群成员列表刷新
    void onMemberContextMenu(const QPoint& pos); // 群成员右键：查看资料

private:
    void appendMessage(const ChatMessage& m);
    bool belongsHere(const ChatMessage& m) const;
    void buildEmojiButton(class QHBoxLayout* row);

    AppModel* m_model = nullptr;
    QString m_scope;
    quint64 m_targetId = 0;
    QTextBrowser* m_history = nullptr;
    QTextEdit* m_input = nullptr;
    QListWidget* m_memberList = nullptr;          // 仅群聊
    QTimer* m_memberTimer = nullptr;              // 合并成员列表刷新（在线状态抖动时）
};
