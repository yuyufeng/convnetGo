#pragma once

#include <QDialog>
#include <QVector>
#include "model/Types.h"

class AppModel;
class QLineEdit;
class QListWidget;

class AddFriendDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddFriendDialog(AppModel* model, QWidget* parent = nullptr);

private slots:
    void doSearch();
    void onResults(const QVector<FriendInfo>& results);
    void doRequest();

private:
    AppModel* m_model = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_results = nullptr;
    QLineEdit* m_greeting = nullptr;
};
