#include "ui/LivePage.h"

#include "core/ConfigStore.h"
#include "live/LanPreview.h"
#include "live/LiveServer.h"
#include "overlay/OverlayController.h"
#include "record/RecordingController.h"
#include "ui/Widgets.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lightrec {

LivePage::LivePage(RecordingController *controller, OverlayController *overlay, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_overlay(overlay)
    , m_server(new LiveServer(this))
    , m_preview(new LanPreview(overlay, this))
{
    buildUi();

    connect(m_server, &LiveServer::viewerCountChanged, this, [this](int count) {
        m_viewerLabel->setText(QStringLiteral("在线观看：%1 / %2").arg(count).arg(m_server->maxViewers()));
    });
    connect(m_server, &LiveServer::errorOccurred, this, [this](const QString &message) {
        m_lanStatus->setText(message);
    });
    connect(m_preview, &LanPreview::frameReady, m_server, &LiveServer::pushFrame);
    connect(m_preview, &LanPreview::errorOccurred, this, [this](const QString &message) {
        m_lanStatus->setText(QStringLiteral("直播画面异常：%1").arg(message));
    });
    connect(m_controller, &RecordingController::stateChanged, this, &LivePage::handleRecordingState);
    connect(m_controller, &RecordingController::streamStatus, this, [this](const QString &status) {
        if (m_rtmpActive || m_rtmpPending)
            m_rtmpStatus->setText(status);
        if (status.contains(QStringLiteral("中断"))) {
            m_rtmpActive = false;
            m_rtmpButton->setText(QStringLiteral("开始直播"));
            m_rtmpButton->setEnabled(true);
        }
    });

    const StreamConfig stream = ConfigStore::instance().streamDefaults();
    m_rtmpUrl->setText(stream.serverUrl);
    m_rtmpKey->setText(stream.streamKey);
    m_saveLocal->setChecked(stream.saveLocal);
    m_portSpin->setValue(ConfigStore::instance().liveServerPort());
    updateLanUi();
}

LivePage::~LivePage()
{
    stopAll();
}

void LivePage::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(14);

    auto *title = new QLabel(QStringLiteral("直播推流"), this);
    title->setObjectName(QStringLiteral("appTitle"));
    layout->addWidget(title);
    layout->addWidget(ui::mutedLabel(
        QStringLiteral("RTMP 推流同时本地存盘，或启动局域网 Web 直播让同事扫码观看。"
                       "直播画面会同步叠加画板、鼠标特效与摄像头。"), this));

    auto *body = new QHBoxLayout();
    body->setSpacing(16);

    // RTMP ---------------------------------------------------------------
    QFrame *rtmpCard = ui::card(this);
    auto *rtmpLayout = new QVBoxLayout(rtmpCard);
    rtmpLayout->setContentsMargins(18, 16, 18, 16);
    rtmpLayout->setSpacing(10);
    rtmpLayout->addWidget(ui::sectionTitle(QStringLiteral("RTMP 推流"), rtmpCard));

    m_rtmpUrl = new QLineEdit(rtmpCard);
    m_rtmpUrl->setPlaceholderText(QStringLiteral("rtmp://live.example.com/live"));
    rtmpLayout->addWidget(ui::labeledRow(QStringLiteral("推流地址"), m_rtmpUrl));

    m_rtmpKey = new QLineEdit(rtmpCard);
    m_rtmpKey->setPlaceholderText(QStringLiteral("推流密钥"));
    m_rtmpKey->setEchoMode(QLineEdit::Password);
    rtmpLayout->addWidget(ui::labeledRow(QStringLiteral("推流密钥"), m_rtmpKey));

    m_saveLocal = new QCheckBox(QStringLiteral("直播同时保存本地录像（串流存盘）"), rtmpCard);
    m_saveLocal->setChecked(true);
    rtmpLayout->addWidget(m_saveLocal);

    m_rtmpButton = ui::primaryButton(QStringLiteral("开始直播"), rtmpCard);
    m_rtmpStatus = ui::mutedLabel(QStringLiteral("未开始"), rtmpCard);
    rtmpLayout->addWidget(m_rtmpButton, 0, Qt::AlignLeft);
    rtmpLayout->addWidget(m_rtmpStatus);
    rtmpLayout->addWidget(ui::mutedLabel(
        QStringLiteral("支持 H.264/H.265 录制；开启 RTMP 推流时本次录制与推流统一使用 H.264，以兼容 FLV/RTMP。"
                       "直播期间仍可打开画板、鼠标高亮和摄像头画中画。"), rtmpCard));
    rtmpLayout->addStretch(1);
    body->addWidget(rtmpCard, 1);

    // LAN ----------------------------------------------------------------
    QFrame *lanCard = ui::card(this);
    auto *lanLayout = new QVBoxLayout(lanCard);
    lanLayout->setContentsMargins(18, 16, 18, 16);
    lanLayout->setSpacing(10);
    lanLayout->addWidget(ui::sectionTitle(QStringLiteral("局域网直播"), lanCard));

    auto *portRow = new QWidget(lanCard);
    auto *portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    m_portSpin = new QSpinBox(portRow);
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(8088);
    m_lanButton = ui::primaryButton(QStringLiteral("启动直播服务"), portRow);
    portLayout->addWidget(new QLabel(QStringLiteral("端口"), portRow));
    portLayout->addWidget(m_portSpin);
    portLayout->addWidget(m_lanButton);
    portLayout->addStretch(1);
    lanLayout->addWidget(portRow);

    m_lanUrl = new QLabel(QStringLiteral("启动后生成访问地址"), lanCard);
    m_lanUrl->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lanUrl->setStyleSheet(QStringLiteral("color:#2d8cf0; font-size:14px; font-weight:600;"));
    lanLayout->addWidget(m_lanUrl);

    m_qrLabel = new QLabel(lanCard);
    m_qrLabel->setFixedSize(220, 220);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet(QStringLiteral("background:#ffffff; border-radius:10px;"));
    lanLayout->addWidget(m_qrLabel, 0, Qt::AlignHCenter);

    m_viewerLabel = ui::valueLabel(QStringLiteral("在线观看：0 / 16"), lanCard);
    m_viewerLabel->setAlignment(Qt::AlignCenter);
    lanLayout->addWidget(m_viewerLabel);

    m_lanStatus = ui::mutedLabel(
        QStringLiteral("仅在内网提供访问，不上传云端。首次使用请在 Windows 防火墙中允许 LightRec 访问专用网络。"),
        lanCard);
    lanLayout->addWidget(m_lanStatus);
    lanLayout->addStretch(1);
    body->addWidget(lanCard, 1);

    layout->addLayout(body, 1);

    connect(m_rtmpButton, &QPushButton::clicked, this, [this] {
        if (m_rtmpActive)
            stopRtmp();
        else
            startRtmp();
    });
    connect(m_lanButton, &QPushButton::clicked, this, [this] {
        if (m_lanActive)
            stopLan();
        else
            startLan();
    });
}

void LivePage::setOptionsProvider(std::function<RecordOptions()> provider)
{
    m_optionsProvider = std::move(provider);
}

void LivePage::startRtmp()
{
    if (m_controller->isRecording()) {
        ui::info(this, QStringLiteral("RTMP 推流"),
                 QStringLiteral("当前已有录制/直播任务在进行，请先停止后再开始新的直播。"));
        return;
    }
    const QString url = m_rtmpUrl->text().trimmed();
    if (url.isEmpty()) {
        ui::warn(this, QStringLiteral("推流地址"), QStringLiteral("请填写 RTMP 推流地址。"));
        return;
    }

    StreamConfig config;
    config.enabled = true;
    config.serverUrl = url;
    config.streamKey = m_rtmpKey->text().trimmed();
    config.saveLocal = m_saveLocal->isChecked();
    ConfigStore::instance().setStreamDefaults(config);

    m_rtmpButton->setEnabled(false);
    m_rtmpButton->setText(QStringLiteral("连接中…"));
    m_rtmpPending = true;
    m_rtmpStatus->setText(QStringLiteral("正在连接 RTMP 服务器…"));
    emit startStreamRequested(config);
}

void LivePage::stopRtmp()
{
    if (m_rtmpActive) {
        m_rtmpButton->setEnabled(false);
        m_rtmpButton->setText(QStringLiteral("停止中…"));
        m_rtmpStatus->setText(QStringLiteral("正在停止直播…"));
    } else {
        // A countdown / connection attempt is still pending.
        m_rtmpPending = false;
        resetRtmpStart();
    }
    emit stopStreamRequested();
}

void LivePage::resetRtmpStart()
{
    if (m_rtmpActive)
        return;
    m_rtmpPending = false;
    m_rtmpButton->setText(QStringLiteral("开始直播"));
    m_rtmpButton->setEnabled(true);
    m_rtmpStatus->setText(QStringLiteral("未开始"));
}

void LivePage::reloadSettings()
{
    m_portSpin->setValue(ConfigStore::instance().liveServerPort());
}

void LivePage::startLan()
{
    if (m_lanActive)
        return;
    QString error;
    if (!m_server->start(static_cast<quint16>(m_portSpin->value()), &error)) {
        ui::warn(this, QStringLiteral("局域网直播"), error);
        return;
    }
    ConfigStore::instance().setLiveServerPort(m_portSpin->value());
    m_lanActive = true;
    m_lanUrl->setText(m_server->url());
    updateQr();
    updateLanUi();

    if (m_controller->isRecording()) {
        connectRecordingFrames(true);
    } else {
        startPreview();
    }
}

void LivePage::stopLan()
{
    if (!m_lanActive)
        return;
    stopPreview();
    connectRecordingFrames(false);
    m_server->stop();
    m_lanActive = false;
    m_lanUrl->setText(QStringLiteral("启动后生成访问地址"));
    m_qrLabel->clear();
    m_viewerLabel->setText(QStringLiteral("在线观看：0 / 16"));
    m_lanStatus->setText(QStringLiteral("仅在内网提供访问，不上传云端。首次使用请在 Windows 防火墙中允许 LightRec 访问专用网络。"));
    updateLanUi();
}

void LivePage::updateLanUi()
{
    m_lanButton->setText(m_lanActive ? QStringLiteral("停止直播服务")
                                     : QStringLiteral("启动直播服务"));
    m_portSpin->setEnabled(!m_lanActive);
    m_qrLabel->setVisible(m_lanActive);
}

void LivePage::updateQr()
{
    if (!m_lanActive) {
        m_qrLabel->clear();
        return;
    }
    const QImage image = m_server->qrCode(200);
    if (!image.isNull())
        m_qrLabel->setPixmap(QPixmap::fromImage(image));
}

void LivePage::startPreview()
{
    if (m_previewRunning || !m_lanActive || m_controller->isRecording())
        return;

    RecordOptions options;
    if (m_optionsProvider)
        options = m_optionsProvider();
    else {
        options.video = ConfigStore::instance().videoDefaults();
        options.overlay = ConfigStore::instance().overlayDefaults();
    }

    QString error;
    if (!m_preview->start(options.video, options.overlay, &error)) {
        m_lanStatus->setText(QStringLiteral("预览画面启动失败：%1").arg(error));
        return;
    }
    m_previewRunning = true;
    m_lanStatus->setText(QStringLiteral("正在直播屏幕画面，可打开画板实时标注。"));
}

void LivePage::stopPreview()
{
    if (!m_previewRunning)
        return;
    m_preview->stop();
    m_previewRunning = false;
}

void LivePage::connectRecordingFrames(bool connectFrames)
{
    if (connectFrames) {
        if (m_frameConnection)
            return;
        m_frameConnection = connect(m_controller, &RecordingController::frameReady,
                                    m_server, &LiveServer::pushFrame);
        m_lanStatus->setText(QStringLiteral("正在使用录制画面进行局域网直播。"));
    } else {
        if (m_frameConnection) {
            disconnect(m_frameConnection);
            m_frameConnection = QMetaObject::Connection();
        }
    }
}

void LivePage::handleRecordingState(RecordState state)
{
    if (state == RecordState::Recording) {
        if (m_rtmpPending || m_rtmpActive) {
            if (m_controller->streamActive()) {
                m_rtmpPending = false;
                m_rtmpActive = true;
                m_rtmpButton->setText(QStringLiteral("停止直播"));
                m_rtmpButton->setEnabled(true);
                m_rtmpStatus->setText(QStringLiteral("直播中：已连接 RTMP 服务器"));
            } else {
                m_rtmpPending = false;
                m_rtmpActive = false;
                m_rtmpButton->setText(QStringLiteral("开始直播"));
                m_rtmpButton->setEnabled(true);
                m_rtmpStatus->setText(QStringLiteral("RTMP 连接失败，本地录像继续"));
            }
        }
    } else if (state == RecordState::Idle) {
        m_rtmpPending = false;
        if (m_rtmpActive) {
            m_rtmpActive = false;
            m_rtmpButton->setText(QStringLiteral("开始直播"));
            m_rtmpButton->setEnabled(true);
            m_rtmpStatus->setText(QStringLiteral("直播已结束"));
        } else if (!m_controller->isRecording()) {
            m_rtmpButton->setText(QStringLiteral("开始直播"));
            m_rtmpButton->setEnabled(true);
            if (m_rtmpStatus->text().contains(QStringLiteral("正在连接")))
                m_rtmpStatus->setText(QStringLiteral("未开始"));
        }
    }

    if (!m_lanActive)
        return;

    if (state == RecordState::Recording || state == RecordState::Starting) {
        stopPreview();
        connectRecordingFrames(true);
    } else if (state == RecordState::Idle) {
        connectRecordingFrames(false);
        if (!m_previewRunning)
            startPreview();
    }
}

void LivePage::setRecordingState(RecordState state)
{
    handleRecordingState(state);
}

void LivePage::prepareForRecording()
{
    if (!m_lanActive)
        return;
    // The preview owns the overlay session; stop it before the recording
    // session starts, otherwise stopping it later would tear down the live
    // annotation overlay.
    stopPreview();
    connectRecordingFrames(true);
}

void LivePage::stopAll()
{
    stopLan();
    if (m_rtmpActive) {
        m_rtmpActive = false;
        emit stopStreamRequested();
    }
}

} // namespace lightrec
