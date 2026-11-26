#ifndef DETOVERLAY_H
#define DETOVERLAY_H

#include <QRectF>
#include <QString>
#include <QImage>
#include <QVector>

// 单个目标：框（视频像素坐标）+ 可选掩膜
struct DetItem {
    QRectF  box;
    float   conf = 0;
    int     cls  = -1;
    QString label;
    QImage  maskRGBA;   // Format_RGBA8888，无掩膜时为空
};

// 一次检测结果
struct DetOverlay {
    quint64 seq = 0;
    QVector<DetItem> items;
    bool empty() const { return items.isEmpty(); }
};

Q_DECLARE_METATYPE(DetItem)
Q_DECLARE_METATYPE(QVector<DetItem>)

#endif // DETOVERLAY_H
