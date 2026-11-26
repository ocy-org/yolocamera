#ifndef DETECTWORKER_H
#define DETECTWORKER_H

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QRectF>
#include "detoverlay.h"
#include "framepacket.h"
#include "yolodetector.h"

//struct DetItem {
//    QRectF box;      // 视频像素坐标
//    float conf = 0;
//    int cls = -1;
//};
//Q_DECLARE_METATYPE(DetItem)

class DetectWorker : public QObject
{
    Q_OBJECT
public:
    explicit DetectWorker(QObject* parent = nullptr);
    void setMailbox(FrameMailbox* m) { m_mail = m; }
    void setIntervalMs(int ms) { m_intervalMs = ms; }
    void setModel(const QString& path);
    void setConf(float c) { m_conf = c; }

public slots:
    void startLoop();                 // 在本线程内创建QTimer
    void stopLoop() { if (m_timer) m_timer->stop(); }

signals:
    void detReady(const QVector<DetItem>& items);   // 视频像素坐标
    void detStats(double fps, int n);
    void detError(const QString& msg);

private slots:
    void onTick();

private:
    FrameMailbox*  m_mail = nullptr;
    QTimer*        m_timer = nullptr;
    int            m_intervalMs = 150;   // ≈7fps 节流
    std::unique_ptr<YoloDetector> m_yolo; // 线程私有实例
    QString        m_modelPath;
    float          m_conf = 0.45f;
    qint64         m_lastStat = 0;
    int            m_statCount = 0;
};
#endif
