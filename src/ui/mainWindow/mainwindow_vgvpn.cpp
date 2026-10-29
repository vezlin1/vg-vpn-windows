// VG VPN Windows client, based on Throne. Modifications licensed under GPL-3.0.
#include "include/ui/mainwindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QUrl>

#include "NkrVersion.h"
#include "include/configs/sub/GroupUpdater.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/ui/mainWindow/MainWindowInternal.h"
#include "include/ui/utils/ProfilesTableModel.h"

namespace {
    void simplifySubscriptionTabs(QTabWidget *tabs) {
        bool haveSubscription = false;
        for (int i = 0; i < tabs->count(); ++i) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(tabs->tabBar()->tabData(i).toInt());
            if (group != nullptr && (!group->url.isEmpty() || !group->Profiles().isEmpty())) haveSubscription = true;
        }
        int visibleTabs = 0;
        for (int i = 0; i < tabs->count(); ++i) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(tabs->tabBar()->tabData(i).toInt());
            const bool visible = !haveSubscription || group == nullptr || !group->url.isEmpty() || !group->Profiles().isEmpty();
            tabs->setTabVisible(i, visible);
            if (visible) ++visibleTabs;
        }
        tabs->tabBar()->setAutoHide(false);
        tabs->tabBar()->setVisible(visibleTabs > 1);
    }

    struct ConnectState {
        QString text;
        bool active = false;
        bool enabled = true;
    };

    class VgConnectDelegate final : public QStyledItemDelegate {
    public:
        using State = std::function<ConnectState(int)>;
        using Activate = std::function<void(int)>;
        VgConnectDelegate(State state, Activate activate, QObject *parent)
            : QStyledItemDelegate(parent), state_(std::move(state)), activate_(std::move(activate)) {}

        static QRect buttonRect(const QRect &cell) {
            return QRect(cell.right() - 174, cell.center().y() - 21, 160, 42);
        }

        void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
            QStyleOptionViewItem background(option);
            initStyleOption(&background, index);
            background.text.clear();
            const auto *widget = option.widget;
            (widget ? widget->style() : QApplication::style())->drawControl(QStyle::CE_ItemViewItem, &background, painter, widget);
            const auto state = state_(index.data(ProfilesTableModel::ProfileIdRole).toInt());
            const QRect rect = buttonRect(option.rect);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            const QColor fill = !state.enabled ? QColor("#292537")
                : state.active ? QColor("#2b233c") : QColor("#9274ff");
            painter->setPen(QPen(state.active ? QColor("#9274ff") : fill, 1));
            painter->setBrush(fill);
            painter->drawRoundedRect(rect, 12, 12);
            if (option.state & QStyle::State_HasFocus) {
                painter->setPen(QPen(QColor("#60e8ff"), 2));
                painter->setBrush(Qt::NoBrush);
                painter->drawRoundedRect(rect.adjusted(-3, -3, 3, 3), 14, 14);
            }
            painter->setPen(!state.enabled ? QColor("#b3a9ce") : state.active ? QColor("#ffffff") : QColor("#100b24"));
            QFont font = option.font;
            font.setWeight(QFont::DemiBold);
            painter->setFont(font);
            painter->drawText(rect, Qt::AlignCenter, state.text);
            painter->restore();
        }

        QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {192, 68}; }

        bool editorEvent(QEvent *event, QAbstractItemModel *, const QStyleOptionViewItem &option,
                         const QModelIndex &index) override {
            bool activate = false;
            if (event->type() == QEvent::MouseButtonRelease) {
                const auto *mouse = static_cast<QMouseEvent *>(event);
                activate = mouse->button() == Qt::LeftButton && buttonRect(option.rect).contains(mouse->position().toPoint());
            } else if (event->type() == QEvent::KeyPress) {
                const int key = static_cast<QKeyEvent *>(event)->key();
                activate = key == Qt::Key_Space;
            }
            const int id = index.data(ProfilesTableModel::ProfileIdRole).toInt();
            if (activate && state_(id).enabled) {
                activate_(id);
                return true;
            }
            return false;
        }

    private:
        State state_;
        Activate activate_;
    };
}

void MainWindow::setupVgVpnUi() {
    setObjectName("vgVpnWindow");
    setFont(QFont(QStringLiteral("Segoe UI"), 11));
    setMinimumSize(820, 560);
    if (Configs::dataManager->settingsRepo->mainWindowGeometry.isEmpty()) resize(960, 680);
    const QList<QWidget *> oldToolbar = {ui->toolButton_program, ui->toolButton_preferences,
             ui->toolButton_testing, ui->toolButton_routing, ui->toolButton_tools,
             ui->toolButton_startstop, ui->checkBox_VPN, ui->checkBox_SystemProxy, ui->system_dns,
             ui->data_view, ui->menubar};
    for (QWidget *widget : oldToolbar) widget->hide();

    auto *brand = new QLabel(this);
    brand->setObjectName("vgBrand");
    brand->setText(QStringLiteral("<img src=\":/vgvpn/icon.png\" width=\"48\" height=\"48\">&nbsp; <b>VG VPN</b>"));
    brand->setAccessibleName(QStringLiteral("VG VPN"));
    brand->setTextFormat(Qt::RichText);
    brand->setFixedHeight(68);
    ui->horizontalLayout_2->insertWidget(0, brand);
    ui->horizontalLayout_2->insertStretch(1);
    m_vgAddSubscription = new QPushButton(QStringLiteral("Добавить подписку"), this);
    m_vgAddSubscription->setObjectName("vgAddSubscription");
    m_vgAddSubscription->setFixedSize(208, 46);
    m_vgAddSubscription->setCursor(Qt::PointingHandCursor);
    connect(m_vgAddSubscription, &QPushButton::clicked, this, &MainWindow::addVgVpnSubscription);
    ui->horizontalLayout_2->addWidget(m_vgAddSubscription);
    ui->verticalLayout_3->setContentsMargins(24, 12, 24, 18);
    ui->verticalLayout_3->setSpacing(16);

    m_vgEmptyState = new QLabel(QStringLiteral("Добавьте подписку VG VPN\nПосле загрузки выберите сервер и нажмите «Подключиться»."), this);
    m_vgEmptyState->setObjectName("vgEmptyState");
    m_vgEmptyState->setAlignment(Qt::AlignCenter);
    m_vgEmptyState->setWordWrap(true);
    m_vgEmptyState->setMinimumHeight(160);
    ui->verticalLayout_3->insertWidget(1, m_vgEmptyState);
    ui->verticalLayout_3->setStretch(0, 0);
    ui->verticalLayout_3->setStretch(1, 1);
    ui->verticalLayout_3->setStretch(2, 1);
    ui->verticalLayout_3->setStretch(3, 0);

    ui->splitter->widget(1)->hide();
    ui->profilesTableView->verticalHeader()->hide();
    ui->profilesTableView->verticalHeader()->setDefaultSectionSize(68);
    ui->profilesTableView->setShowGrid(false);
    ui->profilesTableView->setAlternatingRowColors(false);
    ui->profilesTableView->setMouseTracking(true);
    if (auto *filters = ui->tabWidget->cornerWidget(Qt::TopRightCorner)) filters->hide();
    simplifySubscriptionTabs(ui->tabWidget);
    ui->profilesTableView->setItemDelegateForColumn(ProfilesTableModel::ColTraffic, new VgConnectDelegate(
        [this](int id) {
            const bool active = running != nullptr && running->id == id;
            const bool busy = m_profileConnecting || m_profileDisconnecting;
            return ConnectState{busy && id == m_vgRequestedProfileId ?
                (m_profileDisconnecting ? QStringLiteral("Отключаем…") : QStringLiteral("Подключаем…"))
                : active ? QStringLiteral("Отключиться") : QStringLiteral("Подключиться"), active,
                !busy && Configs::dataManager->settingsRepo->core_running};
        }, [this](int id) { activateVgVpnProfile(id); }, this));
    // Double click now follows the same simple connection path as the row button.
    disconnect(ui->profilesTableView, &QTableView::doubleClicked, this, &MainWindow::on_profilesTableView_doubleClicked);
    connect(ui->profilesTableView, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        if (index.isValid()) activateVgVpnProfile(index.data(ProfilesTableModel::ProfileIdRole).toInt());
    });

    ui->label_running->hide();
    ui->label_inbound->hide();
    ui->label_speed->hide();
    m_vgStatus = new QLabel(this);
    m_vgStatus->setObjectName("vgStatus");
    ui->horizontalLayout->insertWidget(0, m_vgStatus, 1);
    auto *advanced = new QToolButton(this);
    advanced->setObjectName("vgAdvanced");
    advanced->setText(QStringLiteral("Дополнительно"));
    advanced->setMinimumHeight(42);
    advanced->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(advanced);
    menu->addAction(QStringLiteral("Обновить подписки сейчас"), this, [] { Subscription::updater()->RefreshAll(); });
    auto *diagnostics = menu->addAction(QStringLiteral("Показать диагностику"));
    diagnostics->setCheckable(true);
    connect(diagnostics, &QAction::toggled, this, [this](bool shown) {
        ui->splitter->widget(1)->setVisible(shown);
        ui->splitter->setVisible(shown || profilesTableModel->rowCount() > 0);
        if (shown) ui->splitter->setSizes({380, 220});
    });
    auto *columns = menu->addAction(QStringLiteral("Технические столбцы"));
    columns->setCheckable(true);
    connect(columns, &QAction::toggled, this, [this](bool shown) {
        m_vgTechnicalColumns = shown;
        refresh_proxy_list_column_size();
    });
    menu->addSeparator();
    menu->addMenu(ui->menu_preferences);
    menu->addMenu(ui->menuTesting);
    menu->addMenu(ui->menuRouting_Menu);
    menu->addMenu(ui->menuTools);
    menu->addMenu(ui->menu_program);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Поддержка VG VPN"), this, [] { QDesktopServices::openUrl(QUrl("https://vgvpn.site/support")); });
    menu->addAction(QStringLiteral("О программе"), this, [this] {
        QMessageBox::about(this, QStringLiteral("VG VPN"), QStringLiteral(
            "<h2>VG VPN %1</h2><p>VPN-клиент для Windows на базе Throne 1.3.2.</p>"
            "<p>HWID включён. Подписки обновляются раз в час.<br>TUN включается при подключении.</p>"
            "<p>Изменения VG VPN: 2026. Лицензия GNU GPL v3.</p>"
            "<p><a href=\"https://github.com/vezlin1/vg-vpn-windows\">Исходный код VG VPN</a><br>"
            "<a href=\"https://github.com/throneproj/Throne\">Проект Throne и его авторы</a></p>").arg(NKR_VERSION));
    });
    menu->addSeparator();
    menu->addAction(ui->menu_exit);
    advanced->setMenu(menu);
    ui->horizontalLayout->addWidget(advanced);
    ui->actionRemember_last_proxy->setVisible(false);
    ui->actionRemember_last_proxy->setEnabled(false);
    ui->menu_start->setText(QStringLiteral("Подключиться / Отключиться"));

    setStyleSheet(QStringLiteral(
        "QMainWindow#vgVpnWindow, #centralwidget { background:#0b0b18; color:#ffffff; }"
        "QLabel#vgBrand, QLabel#vgEmptyState, QLabel#vgStatus { background:transparent; border:0; }"
        "QLabel#vgBrand { font-size:26px; color:#ffffff; }"
        "QLabel#vgEmptyState { color:#c6c1d9; font-size:17px; line-height:1.6; }"
        "QLabel#vgStatus { color:#c6c1d9; padding:8px 0; }"
        "QPushButton#vgAddSubscription { background:#9274ff; color:#100b24; border:1px solid #9274ff; border-radius:14px; font-weight:600; }"
        "QPushButton#vgAddSubscription:hover { background:#a58cff; }"
        "QPushButton#vgAddSubscription:pressed { background:#8061ed; }"
        "QPushButton#vgAddSubscription:disabled { background:#292537; color:#b3a9ce; border-color:#39324c; }"
        "QPushButton#vgAddSubscription:focus, QToolButton#vgAdvanced:focus { border:2px solid #60e8ff; }"
        "QToolButton#vgAdvanced { color:#c6c1d9; background:transparent; border:1px solid #353044; border-radius:12px; padding:0 20px 0 14px; }"
        "QToolButton#vgAdvanced:hover { background:#211c30; color:#ffffff; }"
        "QTableView { background:#11101e; alternate-background-color:#11101e; color:#ffffff; border:1px solid #2a253b; border-radius:14px; selection-background-color:#252037; selection-color:#ffffff; }"
        "QTableView::item { padding:10px 16px; border-bottom:1px solid #211d31; }"
        "QHeaderView::section { background:#171425; color:#c6c1d9; border:0; padding:12px 16px; text-align:left; }"
        "QTabWidget::pane { border:0; background:#0b0b18; }"
        "QTabBar { background:#0b0b18; }"
        "QTabBar::tab { background:transparent; color:#b7afcb; border:0; padding:10px 14px; }"
        "QTabBar::tab:selected { color:#ffffff; border-bottom:2px solid #9274ff; }"
        "QMenu { background:#191526; color:#ffffff; border:1px solid #39324c; padding:6px; }"
        "QMenu::item { padding:9px 28px 9px 16px; }"
        "QMenu::item:selected { background:#34264f; }"
        "QDialog#vgSubscriptionDialog { background:#151121; color:#ffffff; }"
        "QDialog#vgSubscriptionDialog QLabel { color:#ffffff; background:transparent; }"
        "QDialog#vgSubscriptionDialog QLineEdit { background:#211a32; color:#ffffff; border:1px solid #746095; border-radius:8px; min-height:36px; padding:0 10px; }"
        "QDialog#vgSubscriptionDialog QPushButton { min-height:34px; min-width:100px; padding:0 12px; }"
    ));
    refresh_proxy_list_column_size();
    refreshVgVpnUi();
}

void MainWindow::refreshVgVpnUi() {
    if (m_vgStatus == nullptr) return;
    QString status;
    if (m_profileDisconnecting) status = QStringLiteral("Отключаем VPN…");
    else if (m_profileConnecting) status = QStringLiteral("Подключаем VPN…");
    else if (running != nullptr) status = QStringLiteral("Подключено · %1").arg(running->outbound->name);
    else if (!Configs::dataManager->settingsRepo->core_running) status = QStringLiteral("Запускаем VG VPN…");
    else status = QStringLiteral("Не подключено · выберите сервер");
    m_vgStatus->setText(status);
    m_vgStatus->setAccessibleName(status);
    setWindowTitle(running != nullptr ? QStringLiteral("VG VPN · Подключено") : QStringLiteral("VG VPN"));
    const bool empty = profilesTableModel == nullptr || profilesTableModel->rowCount() == 0;
    m_vgEmptyState->setVisible(empty);
    ui->splitter->widget(0)->setVisible(!empty);
    ui->splitter->setVisible(!empty || !ui->splitter->widget(1)->isHidden());
    ui->profilesTableView->viewport()->update();
}

void MainWindow::activateVgVpnProfile(int id) {
    if (m_profileConnecting || m_profileDisconnecting || !Configs::dataManager->settingsRepo->core_running) return;
    if (running != nullptr && running->id == id) profile_stop(false, false, true);
    else profile_start(id);
}

void MainWindow::addVgVpnSubscription() {
    QInputDialog dialog(this);
    dialog.setObjectName("vgSubscriptionDialog");
    dialog.setWindowTitle(QStringLiteral("Добавить подписку"));
    dialog.setLabelText(QStringLiteral("Вставьте ссылку на подписку из личного кабинета VG VPN."));
    dialog.setOkButtonText(QStringLiteral("Добавить"));
    dialog.setCancelButtonText(QStringLiteral("Отмена"));
    dialog.resize(560, dialog.sizeHint().height());
    const QString clipboard = QApplication::clipboard()->text().trimmed();
    if (clipboard.startsWith("https://") || clipboard.startsWith("http://")) dialog.setTextValue(clipboard);
    if (dialog.exec() != QDialog::Accepted) return;
    const QUrl url(dialog.textValue().trimmed(), QUrl::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        (url.scheme() != "https" && url.scheme() != "http")) {
        QMessageBox::warning(this, QStringLiteral("Проверьте ссылку"),
            QStringLiteral("Нужна полная ссылка на подписку, начинающаяся с https://."));
        return;
    }
    m_vgAddSubscription->setEnabled(false);
    m_vgAddSubscription->setText(QStringLiteral("Загружаем…"));
    const QPointer<MainWindow> self(this);
    const QString normalized = url.toString(QUrl::RemoveFragment);
    auto finished = [self, normalized] {
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, normalized] {
            if (!self) return;
            self->m_vgAddSubscription->setEnabled(true);
            self->m_vgAddSubscription->setText(QStringLiteral("Добавить подписку"));
            self->refresh_groups();
            for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
                const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
                if (group != nullptr && QUrl(group->url).toString(QUrl::RemoveFragment) == normalized) {
                    if (group->name == QUrl(normalized).host()) {
                        group->name = group->sub_info.title.isEmpty() ? QStringLiteral("VG VPN") : group->sub_info.title;
                        Configs::dataManager->groupsRepo->Save(group);
                        self->refresh_groups();
                    }
                    self->ui->tabWidget->setCurrentIndex(groupId2TabIndex(gid));
                    self->show_group(gid);
                    simplifySubscriptionTabs(self->ui->tabWidget);
                    if (group->Profiles().isEmpty()) {
                        QMessageBox::warning(self, QStringLiteral("Подписка не загружена"), QStringLiteral(
                            "Не удалось получить список серверов. Проверьте ссылку и подключение к интернету.\n\n"
                            "Повторите добавление той же ссылки или обновите подписки в меню «Дополнительно»."));
                    }
                    break;
                }
            }
            self->refresh_proxy_list_column_size();
            self->refreshVgVpnUi();
        }, Qt::QueuedConnection);
    };
    for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
        const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
        if (group != nullptr && QUrl(group->url).toString(QUrl::RemoveFragment) == normalized) {
            show_group(gid);
            Subscription::updater()->RefreshGroup(gid, finished);
            return;
        }
    }
    Subscription::updater()->SubscribeUrl(normalized, finished);
}
