#include "ui/FloatingToolbar.h"

#include "core/ConfigStore.h"
#include "core/ThemeManager.h"
#include "core/WindowUtils.h"

#include <QApplication>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>

namespace lightrec {

namespace {
QToolButton *makeButton(const QString &text, const QString &tip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}
} // namespace

FloatingToolbar::FloatingToolbar(QWidget *parent)
    : QWidget(parent)
{
    // Use a normal top-level window (not Qt::Tool): tool windows can be hidden
    // automatically when the main window is minimized, which made the recording
    // toolbar disappear during recording.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setObjectName(QStringLiteral("floatingToolbar"));
    setWindowTitle(QStringLiteral("LightRec 录制工具栏"));

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(10, 6, 10, 5);
    m_outerLayout->setSpacing(2);

    auto *buttonRow = new QWidget(this);
    m_buttonLayout = new QHBoxLayout(buttonRow);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(4);
    m_outerLayout->addWidget(buttonRow);

    auto *layout = m_buttonLayout;

    auto addShortcutCell = [this, buttonRow](QToolButton *button) {
        auto *cell = new QWidget(buttonRow);
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(0);
        cellLayout->addWidget(button, 0, Qt::AlignHCenter);
        auto *shortcut = new QLabel(cell);
        shortcut->setObjectName(QStringLiteral("toolbarShortcut"));
        shortcut->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        cellLayout->addWidget(shortcut, 0, Qt::AlignHCenter);
        m_buttonLayout->addWidget(cell);
        m_shortcutLabels.insert(button, shortcut);
    };

    m_dragHandle = new QLabel(QStringLiteral("⣿"), this);
    m_dragHandle->setToolTip(QStringLiteral("拖动工具栏"));
    m_dragHandle->setCursor(Qt::SizeAllCursor);
    m_dragHandle->setStyleSheet(QStringLiteral("color: rgba(255,255,255,140); padding: 0 4px;"));
    m_dragHandle->installEventFilter(this);
    layout->addWidget(m_dragHandle);

    m_dot = new QLabel(QStringLiteral("●"), this);
    m_dot->setStyleSheet(QStringLiteral("color:#e5484d; font-size:13px;"));
    layout->addWidget(m_dot);

    m_elapsedLabel = new QLabel(QStringLiteral("00:00:00"), this);
    m_elapsedLabel->setStyleSheet(QStringLiteral("font-family:'Cascadia Mono','Consolas',monospace;"
                                                 "font-size:14px; font-weight:600; color:#eef4fb;"
                                                 "padding-right:6px;"));
    layout->addWidget(m_elapsedLabel);

    m_startButton = makeButton(QStringLiteral("开始"), QStringLiteral("开始录制"), this);
    m_pauseButton = makeButton(QStringLiteral("暂停"), QStringLiteral("暂停 / 继续"), this);
    m_stopButton = makeButton(QStringLiteral("停止"), QStringLiteral("停止录制并保存"), this);
    m_stopButton->setObjectName(QStringLiteral("stopButton"));
    addShortcutCell(m_startButton);
    addShortcutCell(m_pauseButton);
    addShortcutCell(m_stopButton);

    auto *sep1 = new QLabel(QStringLiteral("│"), this);
    sep1->setStyleSheet(QStringLiteral("color: rgba(255,255,255,40);"));
    layout->addWidget(sep1);

    m_drawButton = makeButton(QStringLiteral("标注"), QStringLiteral("开启 / 关闭实时标注画板（1–7 切换工具，含橡皮）"), this);
    m_drawButton->setCheckable(true);
    m_overlayButton = makeButton(QStringLiteral("叠加"), QStringLiteral("显示 / 隐藏全部叠加元素"), this);
    m_overlayButton->setCheckable(true);
    m_overlayButton->setChecked(true);
    m_cameraButton = makeButton(QStringLiteral("摄像头"), QStringLiteral("开启 / 关闭摄像头画中画"), this);
    m_cameraButton->setCheckable(true);
    m_spotlightButton = makeButton(QStringLiteral("高亮"), QStringLiteral("局部高亮：拖拽选择要突出的区域"), this);
    m_spotlightButton->setCheckable(true);
    m_magnifierButton = makeButton(QStringLiteral("放大"), QStringLiteral("局部放大：拖拽选择要放大的区域"), this);
    m_magnifierButton->setCheckable(true);
    m_regionButton = makeButton(QStringLiteral("选区"), QStringLiteral("调整录制选区位置和大小"), this);
    addShortcutCell(m_drawButton);
    addShortcutCell(m_overlayButton);
    addShortcutCell(m_cameraButton);
    addShortcutCell(m_spotlightButton);
    addShortcutCell(m_magnifierButton);
    addShortcutCell(m_regionButton);
    auto *sep2 = new QLabel(QStringLiteral("│"), this);
    sep2->setStyleSheet(QStringLiteral("color: rgba(255,255,255,40);"));
    layout->addWidget(sep2);

    m_mainButton = makeButton(QStringLiteral("主界面"), QStringLiteral("打开 LightRec 主界面"), this);
    layout->addWidget(m_mainButton);

    connect(m_startButton, &QToolButton::clicked, this, &FloatingToolbar::startRequested);
    connect(m_pauseButton, &QToolButton::clicked, this, &FloatingToolbar::pauseRequested);
    connect(m_stopButton, &QToolButton::clicked, this, &FloatingToolbar::stopRequested);
    connect(m_drawButton, &QToolButton::toggled, this, &FloatingToolbar::drawToggled);
    connect(m_overlayButton, &QToolButton::toggled, this, &FloatingToolbar::overlaysToggled);
    connect(m_cameraButton, &QToolButton::toggled, this, &FloatingToolbar::cameraToggled);
    connect(m_spotlightButton, &QToolButton::toggled, this, &FloatingToolbar::spotlightToggled);
    connect(m_magnifierButton, &QToolButton::toggled, this, &FloatingToolbar::magnifierToggled);

    connect(m_regionButton, &QToolButton::clicked, this, &FloatingToolbar::adjustRegionRequested);
    connect(m_mainButton, &QToolButton::clicked, this, &FloatingToolbar::showMainRequested);

    for (QToolButton *button : findChildren<QToolButton *>())
        button->installEventFilter(this);
    installEventFilter(this);

    m_hintLabel = new QLabel(this);
    m_hintLabel->setWordWrap(true);
    m_hintText = QStringLiteral(
        "标注/高亮/放大：点击开启，再次点击或按 Esc 退出  ·  拖动左侧手柄可移动");
    m_hintLabel->setText(m_hintText);
    m_outerLayout->addWidget(m_hintLabel);

    m_messageTimer = new QTimer(this);
    m_messageTimer->setSingleShot(true);
    m_messageTimer->setInterval(3500);
    connect(m_messageTimer, &QTimer::timeout, this, [this] {
        if (m_hintLabel)
            m_hintLabel->setText(m_hintText);
    });

    setState(RecordState::Idle);
    setScale(150);
    refreshShortcutHints();
}

void FloatingToolbar::setScale(int percent)
{
    m_scale = qBound(100, percent, 250);
    const int buttonHeight = qRound(34.0 * m_scale / 100.0);

    const QList<QToolButton *> buttons = {
        m_startButton, m_pauseButton, m_stopButton, m_drawButton,
        m_overlayButton, m_cameraButton, m_spotlightButton, m_magnifierButton,
        m_regionButton
    };
    for (QToolButton *button : buttons) {
        if (!button)
            continue;
        button->setFixedHeight(buttonHeight);
        button->setMinimumWidth(qRound(46.0 * m_scale / 100.0));
    }
    // The main-window button is the last QToolButton child.
    const auto allButtons = findChildren<QToolButton *>();
    for (QToolButton *button : allButtons) {
        if (buttons.contains(button))
            continue;
        button->setFixedHeight(buttonHeight);
        button->setMinimumWidth(qRound(58.0 * m_scale / 100.0));
    }

    if (m_buttonLayout) {
        const int margin = qRound(8.0 * m_scale / 100.0);
        m_outerLayout->setContentsMargins(margin, qRound(5.0 * m_scale / 100.0),
                                          margin, qRound(4.0 * m_scale / 100.0));
        m_buttonLayout->setSpacing(qRound(4.0 * m_scale / 100.0));
    }
    applyAppearance();
    adjustSize();
}

void FloatingToolbar::setStyle(int style)
{
    m_style = qBound(0, style, 2);
    applyAppearance();
}

void FloatingToolbar::setOpacity(int opacity)
{
    m_opacity = qBound(40, opacity, 100);
    applyAppearance();
}

void FloatingToolbar::applyAppearance()
{
    m_darkAppearance = (m_style == 0) || (m_style == 2 && ThemeManager::instance().isDark());
    const int fontSize = qMax(12, qRound(13.0 * m_scale / 100.0));
    const int padding = qRound(7.0 * m_scale / 100.0);
    const QString textColor = m_darkAppearance ? QStringLiteral("#eef4fb")
                                               : QStringLiteral("#25303f");
    const QString hover = m_darkAppearance ? QStringLiteral("rgba(255,255,255,30)")
                                           : QStringLiteral("rgba(20,40,70,24)");
    const QString disabled = m_darkAppearance ? QStringLiteral("#7f8b9c")
                                              : QStringLiteral("#aab5c3");
    setStyleSheet(QStringLiteral(
        "QToolButton { color:%1; background:transparent; border:0;"
        " border-radius:8px; font-size:%2px; padding:2px %3px; }"
        "QToolButton:hover { background:%4; }"
        "QToolButton:checked { background:#2d8cf0; color:#ffffff; }"
        "QToolButton:disabled { color:%5; }"
        "QToolButton#stopButton { color:#e5484d; font-weight:600; }")
                      .arg(textColor).arg(fontSize).arg(padding).arg(hover, disabled));

    m_dragHandle->setStyleSheet(QStringLiteral("color:%1; font-size:%2px; padding:0 %3px;")
                                    .arg(m_darkAppearance ? QStringLiteral("rgba(255,255,255,170)")
                                                         : QStringLiteral("rgba(30,45,65,150)"))
                                    .arg(qRound(18.0 * m_scale / 100.0))
                                    .arg(qRound(4.0 * m_scale / 100.0)));
    m_dot->setStyleSheet(QStringLiteral("color:#ff4d4f; font-size:%1px;")
                             .arg(qRound(16.0 * m_scale / 100.0)));
    m_elapsedLabel->setStyleSheet(
        QStringLiteral("font-family:'Cascadia Mono','Consolas',monospace;"
                       "font-size:%1px; font-weight:700; color:%2; padding-right:%3px;")
            .arg(qRound(20.0 * m_scale / 100.0))
            .arg(textColor)
            .arg(qRound(8.0 * m_scale / 100.0)));
    if (m_hintLabel) {
        m_hintLabel->setStyleSheet(
            QStringLiteral("color:%1; font-size:%2px; padding:1px %3px 0 %3px;")
                .arg(m_darkAppearance ? QStringLiteral("#9aa8ba") : QStringLiteral("#6b7a8d"))
                .arg(qMax(11, qRound(12.0 * m_scale / 100.0)))
                .arg(qRound(6.0 * m_scale / 100.0)));
    }
    const QString shortcutColor = m_darkAppearance ? QStringLiteral("#8f9daf")
                                                   : QStringLiteral("#7a8798");
    for (QLabel *label : m_shortcutLabels.values()) {
        if (!label)
            continue;
        label->setStyleSheet(
            QStringLiteral("color:%1; font-size:%2px; font-weight:600; padding:0;")
                .arg(shortcutColor)
                .arg(qMax(9, qRound(7.0 * m_scale / 100.0))));
    }
    update();
}

void FloatingToolbar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    // Draw the panel ourselves.  A translucent layered window whose QSS
    // background is not painted has alpha 0 in the empty areas, which makes
    // Windows treat those pixels as click-through.  An explicit opaque panel
    // fixes both visibility and mouse input.
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int alpha = qBound(0, qRound(255.0 * m_opacity / 100.0), 255);
    if (m_darkAppearance) {
        painter.setPen(QPen(QColor(255, 255, 255, 36), 1));
        painter.setBrush(QColor(15, 20, 27, alpha));
    } else {
        painter.setPen(QPen(QColor(40, 60, 90, 40), 1));
        painter.setBrush(QColor(250, 252, 255, alpha));
    }
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}

void FloatingToolbar::setHint(const QString &hint)
{
    if (hint.isEmpty())
        return;
    m_hintText = hint;
    if (m_messageTimer)
        m_messageTimer->stop();
    if (m_hintLabel)
        m_hintLabel->setText(hint);
}

void FloatingToolbar::showMessage(const QString &message)
{
    if (message.isEmpty() || !m_hintLabel)
        return;
    // Keep transient messages inside the excluded toolbar instead of using a
    // separate QToolTip window that would be visible in the recording.
    m_hintLabel->setText(message);
    if (m_messageTimer)
        m_messageTimer->start();
}

void FloatingToolbar::setState(RecordState state)
{
    const bool recording = state == RecordState::Recording || state == RecordState::Paused;
    m_startButton->setEnabled(state == RecordState::Idle);
    m_pauseButton->setEnabled(recording);
    m_stopButton->setEnabled(recording || state == RecordState::Starting);
    m_drawButton->setEnabled(recording);
    m_overlayButton->setEnabled(recording);
    m_cameraButton->setEnabled(recording);
    m_spotlightButton->setEnabled(recording);
    m_magnifierButton->setEnabled(recording);
    m_regionButton->setEnabled(recording);
    m_pauseButton->setText(state == RecordState::Paused ? QStringLiteral("继续")
                                                        : QStringLiteral("暂停"));
    m_pauseButton->setToolTip(state == RecordState::Paused ? QStringLiteral("继续录制")
                                                           : QStringLiteral("暂停录制"));
    m_dot->setStyleSheet(state == RecordState::Paused
                             ? QStringLiteral("color:#f0a020; font-size:13px;")
                             : QStringLiteral("color:#e5484d; font-size:13px;"));
}

void FloatingToolbar::setElapsed(qint64 elapsedMs)
{
    m_elapsedLabel->setText(formatDuration(elapsedMs));
}

void FloatingToolbar::setCaptureInVideo(bool include)
{
    m_captureInVideo = include;
    if (!isVisible())
        return;
    if (include)
        WindowUtils::allowCapture(winId());
    else
        WindowUtils::excludeFromCapture(winId());
}

void FloatingToolbar::setDrawEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_drawButton);
    m_drawButton->setChecked(enabled);
}

void FloatingToolbar::setOverlaysVisible(bool visible)
{
    const QSignalBlocker blocker(m_overlayButton);
    m_overlayButton->setChecked(visible);
}

void FloatingToolbar::setCameraEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_cameraButton);
    m_cameraButton->setChecked(enabled);
}

void FloatingToolbar::setSpotlightEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_spotlightButton);
    m_spotlightButton->setChecked(enabled);
}

void FloatingToolbar::setMagnifierEnabled(bool enabled)
{
    const QSignalBlocker blocker(m_magnifierButton);
    m_magnifierButton->setChecked(enabled);
}

void FloatingToolbar::setRegionMode(bool regionMode)
{
    if (QWidget *cell = m_regionButton->parentWidget()) {
        if (cell != this)
            cell->setVisible(regionMode);
        else
            m_regionButton->setVisible(regionMode);
    } else {
        m_regionButton->setVisible(regionMode);
    }
}

void FloatingToolbar::showToolbar()
{
    adjustSize();
    QScreen *primary = QGuiApplication::primaryScreen();
    if (primary && width() > primary->availableGeometry().width() - 8) {
        const int target = qMax(100, m_scale * (primary->availableGeometry().width() - 8) / width());
        if (target < m_scale)
            setScale(target);
        adjustSize();
    }
    const QString saved = ConfigStore::instance().value(QStringLiteral("ui/toolbarPos")).toString();
    QPoint position;
    if (!saved.isEmpty()) {
        const QStringList parts = saved.split(QLatin1Char(','));
        if (parts.size() == 2)
            position = QPoint(parts.at(0).toInt(), parts.at(1).toInt());
    }
    if (saved.isEmpty()) {
        QScreen *screen = QGuiApplication::primaryScreen();
        if (screen) {
            const QRect geometry = screen->availableGeometry();
            position = QPoint(geometry.center().x() - width() / 2, geometry.top() + 16);
        }
    }
    // Keep the (possibly enlarged) toolbar inside the visible desktop.
    QScreen *screen = QGuiApplication::screenAt(position);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        position.setX(qBound(available.left() + 4,
                             position.x(), available.right() - width() - 4));
        position.setY(qBound(available.top() + 4,
                             position.y(), available.bottom() - height() - 4));
    }
    move(position);
    show();
    raise();
    setCaptureInVideo(ConfigStore::instance().captureFloatingToolbar());
    WindowUtils::setTopmost(winId(), true);
    emit geometryChanged(geometry());
}

void FloatingToolbar::refreshShortcutHints()
{
    const auto withShortcut = [](const QString &text, const QString &sequence) {
        return sequence.isEmpty() ? text : QStringLiteral("%1（%2）").arg(text, sequence);
    };
    const auto setLabel = [this](QToolButton *button, const QString &sequence) {
        QLabel *label = m_shortcutLabels.value(button);
        if (!label)
            return;
        label->setText(sequence);
        label->setVisible(!sequence.isEmpty());
    };
    const ConfigStore &store = ConfigStore::instance();
    setLabel(m_startButton, store.hotkeyStartStop());
    setLabel(m_stopButton, store.hotkeyStartStop());
    setLabel(m_pauseButton, store.hotkeyPause());
    setLabel(m_drawButton, store.hotkeyDraw());
    setLabel(m_overlayButton, store.hotkeyHideOverlay());
    setLabel(m_cameraButton, store.hotkeyCamera());
    setLabel(m_spotlightButton, store.hotkeySpotlight());
    setLabel(m_magnifierButton, store.hotkeyMagnifier());
    setLabel(m_regionButton, store.hotkeyRegion());
    if (m_startButton)
        m_startButton->setToolTip(withShortcut(QStringLiteral("开始录制"), store.hotkeyStartStop()));
    if (m_stopButton)
        m_stopButton->setToolTip(withShortcut(QStringLiteral("停止录制并保存"), store.hotkeyStartStop()));
    if (m_pauseButton)
        m_pauseButton->setToolTip(withShortcut(QStringLiteral("暂停 / 继续"), store.hotkeyPause()));
    if (m_drawButton) {
        m_drawButton->setToolTip(withShortcut(
            QStringLiteral("开启 / 关闭实时标注画板（1–7 切换工具，含橡皮）"), store.hotkeyDraw()));
    }
    if (m_overlayButton)
        m_overlayButton->setToolTip(withShortcut(QStringLiteral("显示 / 隐藏全部叠加元素"),
                                                 store.hotkeyHideOverlay()));
    if (m_cameraButton)
        m_cameraButton->setToolTip(withShortcut(QStringLiteral("开启 / 关闭摄像头画中画"),
                                                store.hotkeyCamera()));
    if (m_spotlightButton)
        m_spotlightButton->setToolTip(withShortcut(QStringLiteral("局部高亮：拖拽选择要突出的区域"),
                                                   store.hotkeySpotlight()));
    if (m_magnifierButton)
        m_magnifierButton->setToolTip(withShortcut(QStringLiteral("局部放大：拖拽选择要放大的区域"),
                                                   store.hotkeyMagnifier()));
    if (m_regionButton)
        m_regionButton->setToolTip(withShortcut(QStringLiteral("调整录制选区位置和大小"),
                                                store.hotkeyRegion()));
    if (isVisible()) {
        adjustSize();
        emit geometryChanged(geometry());
    }
}

void FloatingToolbar::hideToolbar()
{
    hide();
}

void FloatingToolbar::moveEvent(QMoveEvent *event)
{
    QWidget::moveEvent(event);
    emit geometryChanged(geometry());
}

bool FloatingToolbar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dragHandle) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_dragging = true;
                m_dragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            move(mouse->globalPosition().toPoint() - m_dragOffset);
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (m_dragging) {
                m_dragging = false;
                ConfigStore::instance().setValue(QStringLiteral("ui/toolbarPos"),
                                                 QStringLiteral("%1,%2").arg(pos().x()).arg(pos().y()));
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace lightrec
