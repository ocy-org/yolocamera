QT       += core gui multimedia multimediawidgets printsupport concurrent opengl

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
TARGET = gpycamera
win32 {
    RC_ICONS = gpy_default.ico
}
# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0
DEFINES += WIN32_LEAN_AND_MEAN NOMINMAX
QMAKE_CXXFLAGS += -include windows.h

SOURCES += \
    cameraprobing.cpp \
    cameraworker.cpp \
    captureworker.cpp \
    detectworker.cpp \
    frameutils.cpp \
    main.cpp \
    mainwindow.cpp \
    videoglwidget.cpp \
    yolodetector.cpp

HEADERS += \
    cameraprobing.h \
    cameraworker.h \
    captureworker.h \
    detectworker.h \
    detoverlay.h \
    framepacket.h \
    frameutils.h \
    mainwindow.h \
    metatypes.h \
    videoglwidget.h \
    yolodetector.h

FORMS +=    mainwindow.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

win32 {
    # 指定 ZXing 头文件路径
    INCLUDEPATH += "D:/software/otherdll/zxing-cpp-master/core/install/include"
    # 指定 ZXing 库文件路径
    LIBS += -L"D:/software/otherdll/zxing-cpp-master/core/install/lib" -lZXing
}

INSTALL_DIR = $$OUT_PWD/install
win32 {
    #创建 install 目录
    QMAKE_POST_BUILD += $$QMAKE_MKDIR $$shell_path($$INSTALL_DIR) $$escape_expand(\\n\\t)
}
include(D:\software\opencv\4.12.0opencv.pri)
include(D:\software\ffmpeg-8.0\v8ffmpeg.pri)

RESOURCES += \
    res.qrc


win32 {
    # Release 模式
    EXE_FILE = $$OUT_PWD/release/$${TARGET}.exe
    #复制 .exe 文件
    #QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$EXE_FILE) $$shell_path($$INSTALL_DIR) $$escape_expand(\\n\\t)
}
