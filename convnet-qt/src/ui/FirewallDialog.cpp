#include "ui/FirewallDialog.h"
#include "model/Identity.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QDialogButtonBox>

namespace {
QString dirText(int d) { return d == 1 ? QStringLiteral("入站") : d == 2 ? QStringLiteral("出站") : QStringLiteral("双向"); }
QString actText(int a) { return a == 0 ? QStringLiteral("允许") : QStringLiteral("拒绝"); }
QString protoText(int p)
{
    switch (p) {
    case 1: return QStringLiteral("ICMP");
    case 6: return QStringLiteral("TCP");
    case 17: return QStringLiteral("UDP");
    default: return QStringLiteral("任意");
    }
}
QString portText(const FwRule& r)
{
    if (r.portHigh <= 0)
        return QStringLiteral("任意");
    if (r.portLow == r.portHigh)
        return QString::number(r.portLow);
    return QStringLiteral("%1-%2").arg(r.portLow).arg(r.portHigh);
}
} // namespace

FirewallDialog::FirewallDialog(Firewall* fw, QWidget* parent)
    : QDialog(parent), m_fw(fw)
{
    setWindowTitle(QStringLiteral("防火墙规则"));
    resize(620, 480);
    if (m_fw)
        m_rules = m_fw->rules();

    // 规则表
    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {QStringLiteral("启用"), QStringLiteral("方向"), QStringLiteral("动作"),
         QStringLiteral("对端ID"), QStringLiteral("协议"), QStringLiteral("目的端口")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);

    // 添加规则表单
    auto* addBox = new QGroupBox(QStringLiteral("添加规则（自上而下匹配，首条命中生效；无命中默认放行）"), this);
    m_dir = new QComboBox(addBox);
    m_dir->addItems({QStringLiteral("双向"), QStringLiteral("入站"), QStringLiteral("出站")});
    m_action = new QComboBox(addBox);
    m_action->addItems({QStringLiteral("允许"), QStringLiteral("拒绝")});
    m_action->setCurrentIndex(1);
    m_peer = new QSpinBox(addBox);
    m_peer->setRange(0, 1000000000);
    m_peer->setSpecialValueText(QStringLiteral("任意")); // 0 显示为“任意”
    m_proto = new QComboBox(addBox);
    m_proto->addItem(QStringLiteral("任意"), 0);
    m_proto->addItem(QStringLiteral("TCP"), 6);
    m_proto->addItem(QStringLiteral("UDP"), 17);
    m_proto->addItem(QStringLiteral("ICMP"), 1);
    m_portLow = new QSpinBox(addBox);
    m_portLow->setRange(0, 65535);
    m_portHigh = new QSpinBox(addBox);
    m_portHigh->setRange(0, 65535);
    auto* addBtn = new QPushButton(QStringLiteral("添加"), addBox);

    auto* form = new QFormLayout(addBox);
    form->addRow(QStringLiteral("方向"), m_dir);
    form->addRow(QStringLiteral("动作"), m_action);
    form->addRow(QStringLiteral("对端用户ID (0=任意)"), m_peer);
    form->addRow(QStringLiteral("协议"), m_proto);
    auto* portRow = new QHBoxLayout;
    portRow->addWidget(m_portLow);
    portRow->addWidget(new QLabel(QStringLiteral("-"), addBox));
    portRow->addWidget(m_portHigh);
    portRow->addWidget(new QLabel(QStringLiteral("(端口留 0-0 表示不限；仅对 TCP/UDP 生效)"), addBox));
    form->addRow(QStringLiteral("目的端口"), portRow);
    form->addRow(QString(), addBtn);

    auto* rowBtns = new QHBoxLayout;
    auto* upBtn = new QPushButton(QStringLiteral("上移"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除选中"), this);
    rowBtns->addWidget(upBtn);
    rowBtns->addWidget(delBtn);
    rowBtns->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("保存并应用"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));

    // 网卡模式对防火墙生效范围的提示（模式不影响“规则的存储”，只影响“哪些规则生效”）
    const bool tapMode = (Identity::instance().nicMode == QLatin1String("tap"));
    auto* modeNote = new QLabel(this);
    modeNote->setWordWrap(true);
    modeNote->setTextFormat(Qt::RichText);
    if (tapMode) {
        modeNote->setText(QStringLiteral(
            "<b>当前网卡：TAP（L2）</b> —— <span style='color:#f0b429'>端口/协议规则不生效</span>"
            "（对 IPX 等非 IP 帧无意义），仅「动作=拒绝 + 指定对端ID」的“拉黑对端”规则有效。"));
        modeNote->setStyleSheet(QStringLiteral(
            "background:#332701;border:1px solid #7a5b00;border-radius:5px;padding:6px;"));
    } else {
        modeNote->setText(QStringLiteral(
            "当前网卡：TUN（L3） —— 端口/协议/对端 规则均生效。"
            "<span style='color:#8b929b'>（切到 TAP 后端口/协议规则将不生效，规则本身不会丢失。）</span>"));
        modeNote->setStyleSheet(QStringLiteral("color:#a9b0b8;"));
    }

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(modeNote);
    layout->addWidget(m_table, 1);
    layout->addLayout(rowBtns);
    layout->addWidget(addBox);
    layout->addWidget(buttons);

    connect(addBtn, &QPushButton::clicked, this, &FirewallDialog::addRule);
    connect(delBtn, &QPushButton::clicked, this, &FirewallDialog::removeSelected);
    connect(upBtn, &QPushButton::clicked, this, &FirewallDialog::moveUp);
    connect(buttons, &QDialogButtonBox::accepted, this, &FirewallDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refreshTable();
}

void FirewallDialog::refreshTable()
{
    m_table->setRowCount(m_rules.size());
    for (int i = 0; i < m_rules.size(); ++i) {
        const FwRule& r = m_rules[i];
        const QString cells[6] = {
            r.enabled ? QStringLiteral("是") : QStringLiteral("否"),
            dirText(r.direction),
            actText(r.action),
            r.peerUserId == 0 ? QStringLiteral("任意") : QString::number(r.peerUserId),
            protoText(r.protocol),
            portText(r)};
        for (int c = 0; c < 6; ++c)
            m_table->setItem(i, c, new QTableWidgetItem(cells[c]));
    }
}

void FirewallDialog::addRule()
{
    FwRule r;
    r.enabled = true;
    r.direction = m_dir->currentIndex();
    r.action = m_action->currentIndex();
    r.peerUserId = static_cast<quint64>(m_peer->value());
    r.protocol = m_proto->currentData().toInt();
    int lo = m_portLow->value();
    int hi = m_portHigh->value();
    if (hi > 0 && lo > hi) {
        const int t = lo;
        lo = hi;
        hi = t;
    }
    r.portLow = lo;
    r.portHigh = hi;
    m_rules.append(r);
    refreshTable();
}

void FirewallDialog::removeSelected()
{
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_rules.size()) {
        m_rules.remove(row);
        refreshTable();
    }
}

void FirewallDialog::moveUp()
{
    const int row = m_table->currentRow();
    if (row > 0 && row < m_rules.size()) {
        m_rules.swapItemsAt(row, row - 1);
        refreshTable();
        m_table->selectRow(row - 1);
    }
}

void FirewallDialog::accept()
{
    if (m_fw)
        m_fw->setRules(m_rules); // 立即生效（Firewall 被 Router 共享）
    QDialog::accept();
}
