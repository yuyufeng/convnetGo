#pragma once

#include <QDialog>
#include <QVector>
#include "model/Types.h"

class AppModel;
class QLineEdit;
class QListWidget;

class GroupDialog : public QDialog {
    Q_OBJECT
public:
    explicit GroupDialog(AppModel* model, QWidget* parent = nullptr);

private slots:
    void doCreate();
    void doSearch();
    void onResults(const QVector<GroupInfo>& results);
    void doJoin();

private:
    AppModel* m_model = nullptr;
    QLineEdit* m_createName = nullptr;
    QLineEdit* m_createPass = nullptr;
    QLineEdit* m_search = nullptr;
    QListWidget* m_results = nullptr;
    QVector<GroupInfo> m_searchResults; // 最近一次搜索结果（含 hasPassword）
};
