#include "mainwindow.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("实时目标检测"));
    resize(1100, 700);
    buildUI();
    refreshCameraList();
}

MainWindow::~MainWindow()
{
    closeCamera();
}

void MainWindow::buildUI()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto *bar = new QHBoxLayout();
    bar->addWidget(new QLabel(QStringLiteral("摄像头:"), this));
    m_camCombo = new QComboBox(this);
    m_camCombo->setMinimumWidth(280);
    bar->addWidget(m_camCombo);

    bar->addWidget(new QLabel(QStringLiteral("分辨率:"), this));
    m_resCombo = new QComboBox(this);
    m_resCombo->setMinimumWidth(220);
    bar->addWidget(m_resCombo);

    m_toggleBtn = new QPushButton(QStringLiteral("打开摄像头"), this);
    m_toggleBtn->setStyleSheet(
        "QPushButton{background:#4CAF50;color:white;border-radius:4px;padding:6px 20px;}");
    bar->addWidget(m_toggleBtn);
    bar->addStretch();

    m_fpsLabel = new QLabel(QStringLiteral("FPS: 0.0 | 状态: 就绪"), this);
    bar->addWidget(m_fpsLabel);

    root->addLayout(bar);

    m_gl = new VideoGLWidget(this);
    m_gl->setMinimumSize(640, 480);
    m_gl->setStyleSheet("border:1px solid gray;border-radius:4px;background:black;");
    root->addWidget(m_gl, 1);

    connect(m_toggleBtn, &QPushButton::clicked, this, [this]() {
        if (m_opened) closeCamera(); else openCamera();
    });
    connect(m_camCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_opened) refreshResolutionCombo();
    });
}

// ============================================================================
// ★ 摄像头枚举 + FFmpeg能力探测（直接引用你原有的CameraProbing）
// ============================================================================
void MainWindow::refreshCameraList()
{
    closeCamera();
    m_camCombo->blockSignals(true);
    m_camCombo->clear();
    m_cameraCaps.clear();
    m_camCombo->blockSignals(false);

    const auto cams = QCameraInfo::availableCameras();
    for (const QCameraInfo& c : cams) {
        m_camCombo->addItem(c.description(), c.deviceName()); // ★ data=PNP路径
    }
    statusBar()->showMessage(
        QStringLiteral("找到 %1 个摄像头").arg(cams.size()), 3000);

    refreshResolutionCombo();
}



// ============================================================================
// ★ 分辨率列表：调用你原有的 probeCameraCapabilities（FFmpeg -list_options）
// ============================================================================
void MainWindow::refreshResolutionCombo()
{
    m_resCombo->clear();
    QString camName = m_camCombo->currentText();
    if (camName.isEmpty()) return;

    statusBar()->showMessage(QStringLiteral("正在探测 %1 的分辨率...").arg(camName));
    QApplication::processEvents();

    CameraProbing probing;   // 栈对象即可（内部QProcess同步等待）
    QList<CameraCapability> caps = probing.probeCameraCapabilities(camName);
    m_cameraCaps[camName] = caps;

    int preferred = 0;
    for (int i = 0; i < caps.size(); ++i) {
        const CameraCapability& cap = caps[i];
        QString text = QString("%1x%2 - %3-%4fps")
            .arg(cap.resolution.width()).arg(cap.resolution.height())
            .arg(cap.codec).arg(cap.maxFps, 0, 'f', 0);
        m_resCombo->addItem(text);
        m_resCombo->setItemData(m_resCombo->count() - 1,
                                QSize(cap.resolution.width(), cap.resolution.height()));
        if (cap.isPreferred && preferred == 0) preferred = i;
    }
    if (m_resCombo->count() > 0)
        m_resCombo->setCurrentIndex(preferred);
    statusBar()->showMessage(
        QStringLiteral("探测到 %1 个分辨率").arg(caps.size()), 3000);
}

// ============================================================================
// 打开/关闭摄像头（cap → det(YOLO) → GL 三线程管线）
// ============================================================================
void MainWindow::openCamera()
{
    if (m_opened) return;
    if (m_camCombo->currentIndex() < 0) {
        QMessageBox::warning(this, QStringLiteral("错误"), QStringLiteral("请先选择摄像头"));
        return;
    }
    closeCamera();

    // ★ 优先用PNP设备路径（与你旧项目逻辑一致），探测失败才退回描述名
    QString devName = m_camCombo->currentData().toString();
    QString dev;
    if (devName.contains("device:pnp:")) {
        devName.replace("device:pnp:", "device_pnp_");   // 旧项目的怪字符处理
        dev = devName;
    } else {
        dev = QString("video=%1").arg(m_camCombo->currentText());
    }

    QSize res = m_resCombo->currentData().toSize();
    if (!res.isValid()) res = QSize(1280, 720);
    QString resolution = QString("%1x%2").arg(res.width()).arg(res.height());

    qDebug() << "打开摄像头:" << dev << resolution;

    // ---------- 采集线程 ----------
    m_capThread = new QThread(this);
    m_capWorker = new CaptureWorker();
    m_capWorker->moveToThread(m_capThread);
    m_gl->setMailbox(m_capWorker->glMailbox());
    CaptureWorker* w = m_capWorker;
    connect(m_capThread, &QThread::started, w, [w, dev, resolution]() {
        w->start(dev, resolution, "mjpeg", 30.0);
    });
    connect(w, &CaptureWorker::errorOccurred, this,
            [this](const QString& e) {
        QMessageBox::warning(this, QStringLiteral("错误"), e);
        closeCamera();
    }, Qt::QueuedConnection);
    connect(w, &CaptureWorker::statsUpdated, this,
            [this](double fps, const QString& codec) {
        m_fpsLabel->setText(QStringLiteral("FPS: %1 | 编码: %2")
                            .arg(fps, 0, 'f', 1).arg(codec));
    }, Qt::QueuedConnection);
    m_capThread->start();

    // ---------- YOLO检测线程 ----------
    QString model   = "D:/python/yolo_env/yolov8n.onnx";
    if (QFileInfo::exists(model)) {
        m_detThread = new QThread(this);
        m_detWorker = new DetectWorker();
        m_detWorker->moveToThread(m_detThread);
        m_detWorker->setMailbox(m_capWorker->detMailbox());
        m_detWorker->setModel(model);
        m_detWorker->setConf(0.45f);
        m_detWorker->setIntervalMs(150);
        DetectWorker* dw = m_detWorker;
        connect(m_detThread, &QThread::started, dw, &DetectWorker::startLoop);
        connect(dw, &DetectWorker::detReady, this,
                [this](const QVector<DetItem>& items) {
            qDebug() << "[Main] 收到检测结果:" << items.size();
            m_gl->setDetOverlay(items);
        }, Qt::QueuedConnection);

        connect(dw, &DetectWorker::detStats, this,
                [this](double fps, int n) {
            statusBar()->showMessage(QStringLiteral("检测: %1 fps | 目标: %2")
                                     .arg(fps, 0, 'f', 1).arg(n), 3000);
        }, Qt::QueuedConnection);
        connect(dw, &DetectWorker::detError, this,
                [this](const QString& e) {
            statusBar()->showMessage(QStringLiteral("检测错误: %1").arg(e), 5000);
        }, Qt::QueuedConnection);
        m_detThread->start();
    } else {
        statusBar()->showMessage(QStringLiteral("未找到yolov8n.onnx，目标检测未启用"), 5000);
    }

    m_opened = true;
    m_toggleBtn->setText(QStringLiteral("关闭摄像头"));
    m_toggleBtn->setStyleSheet(
        "QPushButton{background:#f44336;color:white;border-radius:4px;padding:6px 20px;}");
    m_camCombo->setEnabled(false);
    m_resCombo->setEnabled(false);
}

void MainWindow::closeCamera()
{
    m_gl->setMailbox(nullptr);

    if (m_detWorker)
        QMetaObject::invokeMethod(m_detWorker, "stopLoop", Qt::QueuedConnection);
    if (m_detThread) { m_detThread->quit(); m_detThread->wait(3000); }
    delete m_detWorker; m_detWorker = nullptr;
    delete m_detThread; m_detThread = nullptr;
    m_gl->setDetOverlay(QVector<DetItem>());

    if (m_capWorker) m_capWorker->stop();
    if (m_capThread) { m_capThread->quit(); m_capThread->wait(3000); }
    delete m_capWorker; m_capWorker = nullptr;
    delete m_capThread; m_capThread = nullptr;

    m_opened = false;
    m_toggleBtn->setText(QStringLiteral("打开摄像头"));
    m_toggleBtn->setStyleSheet(
        "QPushButton{background:#4CAF50;color:white;border-radius:4px;padding:6px 20px;}");
    m_camCombo->setEnabled(true);
    m_resCombo->setEnabled(true);
    m_gl->clearFrame();
    m_fpsLabel->setText(QStringLiteral("FPS: 0.0 | 状态: 已关闭"));
}
