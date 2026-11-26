#include "detectworker.h"
#include "frameutils.h"
#include <QDebug>
#include <QDateTime>

DetectWorker::DetectWorker(QObject* parent) : QObject(parent) {}

void DetectWorker::setModel(const QString& path)
{
    m_modelPath = path;
}

void DetectWorker::startLoop()
{
    // ★ YoloDetector必须在本线程内创建（cv::dnn::Net非线程安全，实例线程私有）
    if (!m_yolo) {
        try {
            m_yolo = std::make_unique<YoloDetector>(m_modelPath.toStdString());
            qDebug() << "[Detect] YOLO模型加载完成:" << m_modelPath;
        } catch (const std::exception& e) {
            emit detError(QStringLiteral("模型加载失败: %1").arg(e.what()));
            return;
        }
    }
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &DetectWorker::onTick);
    }
    m_timer->start(m_intervalMs);
}

void DetectWorker::onTick()
{
    if (!m_mail || !m_yolo) return;

    FramePacket pkt;
    if (!m_mail->fetch(pkt)) return;

    cv::Mat bgr = framePacketToBgr(pkt);
    if (bgr.empty()) return;

    // ★ 推理+NMS全在YoloDetector::detect里，这里只拿结果
    const std::vector<DetResult> dets = m_yolo->detect(bgr, m_conf);

    QVector<DetItem> items;
    items.reserve((int)dets.size());
    for (const auto& d : dets) {
        DetItem it;
        it.box  = QRectF(d.box.x, d.box.y, d.box.width, d.box.height);
        it.conf = d.conf;
        it.cls  = d.classId;
        items.append(it);
    }

    qDebug() << "[Detect] 原始候选→保留:" << dets.size() << "->" << items.size();
    emit detReady(items);

    // 统计
    ++m_statCount;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastStat >= 1000) {
        emit detStats(m_statCount * 1000.0 / (now - m_lastStat), items.size());
        m_lastStat = now; m_statCount = 0;
    }
}
