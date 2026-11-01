#include <nova/core.hpp>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdlib>

static QString qtext(const std::string& value) { return QString::fromStdString(nova::visible_text(value)); }
class Window : public QMainWindow {
public:
    explicit Window(bool demo) : service_(nova::frontend_options()) {
        setWindowTitle("discord-nova " NOVA_VERSION);
        resize(1160, 760); setMinimumSize(760, 520);
        auto* root = new QWidget; auto* layout = new QVBoxLayout(root);
        layout->setContentsMargins(20, 16, 20, 16); layout->setSpacing(12);
        auto* top = new QHBoxLayout;
        auto* brand = new QLabel("NOVA"); brand->setObjectName("brand");
        auto* subtitle = new QLabel("A native home for your conversations");
        top->addWidget(brand); top->addWidget(subtitle); top->addStretch();
        account_ = new QLabel("Offline"); top->addWidget(account_); layout->addLayout(top);
        auto* auth = new QHBoxLayout;
        token_ = new QLineEdit; token_->setEchoMode(QLineEdit::Password);
        token_->setPlaceholderText("Account token - kept in memory only");
        if (const char* token = std::getenv("DISCORD_TOKEN")) token_->setText(QString::fromUtf8(token));
        connect_ = new QPushButton("Connect"); demo_ = new QPushButton("Try Demo");
        disconnect_ = new QPushButton("Disconnect"); refresh_ = new QPushButton("Refresh");
        auth->addWidget(token_, 1); auth->addWidget(connect_); auth->addWidget(demo_);
        auth->addWidget(disconnect_); auth->addWidget(refresh_); layout->addLayout(auth);
        auto* identity = new QHBoxLayout;
        identity->addWidget(new QLabel("Connection profile"));
        profile_ = new QComboBox;
        profile_->addItem("Default TLS", QString());
        const auto defaults = nova::frontend_options();
        if (discord::supports_browser_impersonation()) {
            profile_->addItem("Chrome 152 (TLS + HTTP/2)", "chrome152");
        }
        if (!defaults.transport.browser_profile.empty() && profile_->findData(QString::fromStdString(defaults.transport.browser_profile)) < 0)
            profile_->addItem(qtext(defaults.transport.browser_profile), QString::fromStdString(defaults.transport.browser_profile));
        profile_->setCurrentIndex(std::max(0, profile_->findData(QString::fromStdString(defaults.transport.browser_profile))));
        identity->addWidget(profile_);
        auto* identity_note = new QLabel(discord::supports_browser_impersonation() ? "Certificate verification enabled" : "Browser profiles need NOVA_BROWSER_IDENTITY=ON");
        identity->addWidget(identity_note); identity->addStretch(); layout->addLayout(identity);
        auto* split = new QSplitter;
        guilds_ = new QListWidget; guilds_->setMinimumWidth(160);
        channels_ = new QListWidget; channels_->setMinimumWidth(160);
        auto* conversation = new QWidget; auto* chat = new QVBoxLayout(conversation); chat->setContentsMargins(8, 0, 0, 0);
        heading_ = new QLabel("Welcome to Nova"); heading_->setObjectName("heading");
        messages_ = new QTextBrowser; messages_->setOpenLinks(false); messages_->setReadOnly(true);
        messages_->setPlainText("Choose Try Demo to explore, or connect to your account.\n\n"
                               "v0.1 supports text conversations. Voice, video and plugins are planned.");
        auto* compose = new QHBoxLayout;
        draft_ = new QLineEdit; draft_->setPlaceholderText("Write a message... (Enter to send)");
        send_ = new QPushButton("Send"); compose->addWidget(draft_, 1); compose->addWidget(send_);
        chat->addWidget(heading_); chat->addWidget(messages_, 1); chat->addLayout(compose);
        split->addWidget(guilds_); split->addWidget(channels_); split->addWidget(conversation);
        split->setStretchFactor(2, 1); split->setSizes({200, 220, 700}); layout->addWidget(split, 1);
        status_ = new QLabel; status_->setWordWrap(true); layout->addWidget(status_);
        setCentralWidget(root);
        setStyleSheet(R"(
            QWidget { background: #151923; color: #e8edf7; font-size: 14px; }
            QLabel#brand { color: #91abff; font-size: 28px; font-weight: 800; letter-spacing: 4px; }
            QLabel#heading { font-size: 20px; font-weight: 700; padding: 10px; }
            QListWidget, QTextBrowser { background: #1c2230; border: 1px solid #30394d; border-radius: 8px; padding: 10px; }
            QListWidget::item { padding: 10px 5px; }
            QListWidget::item:selected { background: #34466e; border-radius: 5px; }
            QLineEdit { background: #242c3d; border: 1px solid #3d4962; border-radius: 6px; padding: 10px; }
            QPushButton { background: #354972; border: 1px solid #506a9e; border-radius: 6px; padding: 10px 16px; }
            QPushButton:hover { background: #456196; }
            QPushButton:disabled { color: #7b8497; background: #242b39; border-color: #30394d; }
            QSplitter::handle { background: #151923; width: 8px; }
        )");
        QObject::connect(connect_, &QPushButton::clicked, this, [this] {
            if (service_.connect(token_->text().toStdString(), profile_->currentData().toString().toStdString())) token_->clear();
        });
        QObject::connect(token_, &QLineEdit::returnPressed, connect_, &QPushButton::click);
        QObject::connect(demo_, &QPushButton::clicked, this, [this] { service_.demo(); token_->clear(); });
        QObject::connect(disconnect_, &QPushButton::clicked, this, [this] { service_.disconnect(); });
        QObject::connect(refresh_, &QPushButton::clicked, this, [this] { service_.refresh(); });
        QObject::connect(guilds_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            service_.select_guild(item->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(guilds_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            service_.select_guild(item->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(channels_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
            service_.select_channel(item->data(Qt::UserRole).toString().toStdString());
        });
        QObject::connect(channels_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
            service_.select_channel(item->data(Qt::UserRole).toString().toStdString());
        });
        auto send = [this] {
            if (service_.send(draft_->text().toStdString())) pending_draft_ = draft_->text();
        };
        QObject::connect(send_, &QPushButton::clicked, this, send);
        QObject::connect(draft_, &QLineEdit::returnPressed, this, send);
        auto* timer = new QTimer(this);
        QObject::connect(timer, &QTimer::timeout, this, [this] { update(); }); timer->start(80);
        if (demo) service_.demo();
        update();
    }
    bool demo_ready() const { const auto s = service_.snapshot(); return s.demo && !s.busy && s.messages.size() >= 3; }
private:
    static void entries(QListWidget* list, const std::vector<nova::Entry>& items, const std::string& selected) {
        bool same = list->count() == static_cast<int>(items.size());
        for (int i = 0; same && i < list->count(); ++i) {
            const auto& entry = items[static_cast<std::size_t>(i)];
            same = list->item(i)->data(Qt::UserRole).toString() == QString::fromStdString(entry.id) && list->item(i)->text() == qtext(entry.name);
        }
        if (!same) {
            list->clear();
            for (const auto& entry : items) {
                auto* item = new QListWidgetItem(qtext(entry.name), list);
                item->setData(Qt::UserRole, QString::fromStdString(entry.id));
            }
        }
        for (int i = 0; i < list->count(); ++i) if (list->item(i)->data(Qt::UserRole).toString() == QString::fromStdString(selected)) list->setCurrentRow(i);
    }
    void update() {
        const auto s = service_.snapshot();
        if (revision_ == s.revision) return;
        revision_ = s.revision;
        const bool idle = !s.busy;
        connect_->setEnabled(idle); demo_->setEnabled(idle); token_->setEnabled(idle);
        profile_->setEnabled(idle && !s.connected);
        disconnect_->setEnabled(s.connected && idle); refresh_->setEnabled(s.connected && idle);
        guilds_->setEnabled(s.connected && idle); channels_->setEnabled(s.connected && idle);
        draft_->setEnabled(s.connected && !s.channel.empty() && idle); send_->setEnabled(draft_->isEnabled());
        account_->setText(s.connected ? qtext(s.user) + (s.demo ? "  |  DEMO" : s.live ? "  |  LIVE" : "  |  REST") : "Offline");
        status_->setTextFormat(Qt::PlainText);
        status_->setText(qtext(s.error.empty() ? s.status : s.error) + (s.busy ? "  [Working...]" : ""));
        entries(guilds_, s.guilds, s.guild); entries(channels_, s.channels, s.channel);
        QString heading = "Select a conversation";
        for (const auto& c : s.channels) if (c.id == s.channel) heading = qtext(c.name);
        heading_->setTextFormat(Qt::PlainText); heading_->setText(heading);
        account_->setTextFormat(Qt::PlainText);
        QString body;
        for (const auto& m : s.messages) body += qtext(nova::message_text(m)) + "\n\n";
        if (!s.connected) body = "Connect to your account or choose Try Demo.\n\nTokens and messages are never saved to disk.";
        if (messages_->toPlainText() != body) {
            auto* bar = messages_->verticalScrollBar();
            const bool at_bottom = bar->value() >= bar->maximum() - 8;
            const int old = bar->value(); messages_->setPlainText(body);
            bar->setValue(at_bottom ? bar->maximum() : old);
        }
        if (idle && !pending_draft_.isNull()) {
            if (s.error.empty() && draft_->text() == pending_draft_) draft_->clear();
            pending_draft_ = QString();
        }
    }
    nova::Service service_;
    std::uint64_t revision_ = static_cast<std::uint64_t>(-1);
    QString pending_draft_;
    QLabel *account_, *heading_, *status_;
    QLineEdit *token_, *draft_;
    QComboBox* profile_;
    QPushButton *connect_, *demo_, *disconnect_, *refresh_, *send_;
    QListWidget *guilds_, *channels_;
    QTextBrowser* messages_;
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("discord-nova"); QApplication::setApplicationVersion(NOVA_VERSION);
    const auto args = app.arguments();
    if (args.contains("--version")) { qInfo("discord-nova " NOVA_VERSION); return 0; }
    const bool smoke = args.contains("--smoke-test");
    Window window(args.contains("--demo") || smoke); window.show();
    if (smoke) {
        auto* poll = new QTimer(&app);
        QObject::connect(poll, &QTimer::timeout, &app, [&] { if (window.demo_ready()) app.exit(0); }); poll->start(40);
        QTimer::singleShot(5000, &app, [&] { app.exit(1); });
    }
    return app.exec();
}
