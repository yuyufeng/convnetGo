#pragma once

#include <QDialog>
#include <QVector>
#include "net/Firewall.h"

class QTableWidget;
class QComboBox;
class QSpinBox;

// 防火墙规则管理：按顺序匹配的出入站包过滤规则（对端/协议/端口）。
class FirewallDialog : public QDialog {
    Q_OBJECT
public:
    explicit FirewallDialog(Firewall* fw, QWidget* parent = nullptr);

private slots:
    void addRule();
    void removeSelected();
    void moveUp();
    void accept() override;

private:
    void refreshTable();

    Firewall* m_fw = nullptr;
    QVector<FwRule> m_rules;

    QTableWidget* m_table = nullptr;
    QComboBox* m_dir = nullptr;
    QComboBox* m_action = nullptr;
    QSpinBox*  m_peer = nullptr;
    QComboBox* m_proto = nullptr;
    QSpinBox*  m_portLow = nullptr;
    QSpinBox*  m_portHigh = nullptr;
};
