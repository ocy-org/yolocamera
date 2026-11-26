#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QThread>
#include <QStatusBar>
#include <QFileInfo>
#include <QCoreApplication>
#include <QApplication>
#include <opencv2/opencv.hpp>
#include "videoglwidget.h"
#include "captureworker.h"
#include "detectworker.h"
#include "cameraprobing.h"
#include "metatypes.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void buildUI();
    void openCamera();
    void closeCamera();
    void refreshCameraList();
    void refreshResolutionCombo();

    // UI
    QComboBox      *m_camCombo = nullptr;
    QComboBox      *m_resCombo = nullptr;
    QPushButton    *m_toggleBtn = nullptr;
    QLabel         *m_fpsLabel = nullptr;
    VideoGLWidget  *m_gl = nullptr;

    // 摄像头信息缓存：description → 能力列表
    QMap<QString, QList<CameraCapability>> m_cameraCaps;
    QList<QString> m_camDescriptions;

    // 采集/检测线程
    QThread       *m_capThread = nullptr;
    CaptureWorker *m_capWorker = nullptr;
    QThread       *m_detThread = nullptr;
    DetectWorker  *m_detWorker = nullptr;

    bool m_opened = false;
};
#endif
