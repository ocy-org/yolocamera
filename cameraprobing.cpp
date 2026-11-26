#include "cameraprobing.h"

CameraProbing::CameraProbing(QObject *parent) : QObject(parent)
{
}

QList<CameraCapability> CameraProbing::probeCameraCapabilities(const QString &cameraName)
{
    //qDebug() << "=== 开始探测摄像头能力: " << cameraName << "===";

    QList<CameraCapability> capabilities;

    // 方法1: 使用DirectShow探测
    //qDebug() << "尝试使用DirectShow探测...";
    capabilities = probeWithDirectShow(cameraName);

    if (!capabilities.isEmpty()) {
        //qDebug() << "通过DirectShow探测到" << capabilities.size() << "个能力";
        return capabilities;
    }

    return capabilities;
}

QList<CameraCapability> CameraProbing::probeWithDirectShow(const QString &cameraName)
{
    QList<CameraCapability> capabilities;

    // 使用FFmpeg探测摄像头选项
    QStringList arguments;
    arguments << "-f" << "dshow" << "-list_options" << "true" << "-i" << QString("video=%1").arg(cameraName);

    //qDebug() << "执行FFmpeg命令: ffmpeg" << arguments;

    // 使用静态函数调用
    QString output = executeFFmpegCommand(arguments);

    if (output.isEmpty()) {
        qDebug() << "FFmpeg探测无输出";
        return capabilities;
    }

    //qDebug() << "FFmpeg原始输出:\n" << output;
    //qDebug() << "=== 开始解析FFmpeg输出 ===";

    return parseFFmpegOutput(output, cameraName);
}

QList<CameraCapability> CameraProbing::parseFFmpegOutput(const QString &output, const QString &cameraName)
{
    QList<CameraCapability> capabilities;

    QTextStream stream(const_cast<QString*>(&output), QIODevice::ReadOnly);
    QString line;

    //qDebug() << "=== 开始解析FFmpeg输出，查找所有格式 ===";

    // 多种可能的正则表达式模式
    QRegularExpression pattern1(R"(pixel_format=(\w+)\s+min s=(\d+)x(\d+)\s+fps=(\d+))");
    QRegularExpression pattern2(R"(vcodec=(\w+)\s+min s=(\d+)x(\d+)\s+fps=(\d+))");
    QRegularExpression pattern3(R"(min s=(\d+)x(\d+)\s+fps=(\d+)\s+pixel_format=(\w+))");
    QRegularExpression pattern4(R"(min s=(\d+)x(\d+)\s+fps=(\d+)\s+vcodec=(\w+))");

    // 专门匹配MJPG格式的模式
    QRegularExpression mjpegPattern(R"((mjpeg|mjpg|MJPEG|MJPG).*?min s=(\d+)x(\d+).*?fps=(\d+))",
                                   QRegularExpression::CaseInsensitiveOption);

    QSet<QString> uniqueConfigs;
    int lineNumber = 0;
    int configCount = 0;

    while (stream.readLineInto(&line)) {
        lineNumber++;
        line = line.trimmed();

        //qDebug() << "检查行" << lineNumber << ":" << line;

        CameraCapability cap;
        bool matched = false;

        // 尝试匹配MJPG格式（优先）
        QRegularExpressionMatch mjpegMatch = mjpegPattern.match(line);
        if (mjpegMatch.hasMatch()) {
            QString codec = mjpegMatch.captured(1).toLower();
            int width = mjpegMatch.captured(2).toInt();
            int height = mjpegMatch.captured(3).toInt();
            double fps = mjpegMatch.captured(4).toDouble();

            cap.codec = "mjpeg";
            cap.pixelFormat = "mjpeg";
            cap.resolution = QSize(width, height);
            cap.maxFps = fps;
            cap.isPreferred = true; // MJPG格式优先

            matched = true;
           // qDebug() << "匹配到MJPG格式:" << width << "x" << height << "fps:" << fps;
        }

        // 如果没有匹配到MJPG，尝试其他格式
        if (!matched) {
            QRegularExpressionMatch match;

            // 按优先级尝试不同的模式
            if (pattern1.match(line).hasMatch()) match = pattern1.match(line);
            else if (pattern2.match(line).hasMatch()) match = pattern2.match(line);
            else if (pattern3.match(line).hasMatch()) match = pattern3.match(line);
            else if (pattern4.match(line).hasMatch()) match = pattern4.match(line);

            if (match.hasMatch()) {
                QString formatOrCodec = match.captured(1).toLower();
                int width, height;
                double fps;

                // 根据模式调整捕获组
                if (match.regularExpression() == pattern1 || match.regularExpression() == pattern2) {
                    width = match.captured(2).toInt();
                    height = match.captured(3).toInt();
                    fps = match.captured(4).toDouble();
                } else {
                    width = match.captured(1).toInt();
                    height = match.captured(2).toInt();
                    fps = match.captured(3).toDouble();
                    formatOrCodec = match.captured(4).toLower();
                }

                // 确定是编码格式还是像素格式
                if (formatOrCodec == "mjpeg" || formatOrCodec == "mjpg") {
                    cap.codec = "mjpeg";
                    cap.pixelFormat = "mjpeg";
                    cap.isPreferred = true;
                } else {
                    cap.codec = formatOrCodec;
                    cap.pixelFormat = formatOrCodec;
                    cap.isPreferred = false;
                }

                cap.resolution = QSize(width, height);
                cap.maxFps = fps;
                matched = true;

                //qDebug() << "匹配到格式:" << formatOrCodec << width << "x" << height << "fps:" << fps;
            }
        }

        // 如果匹配成功，添加到列表
        if (matched) {
            QString configKey = QString("%1x%2-%3-%4").arg(cap.resolution.width())
                                                    .arg(cap.resolution.height())
                                                    .arg(cap.codec)
                                                    .arg(cap.maxFps);

            if (!uniqueConfigs.contains(configKey)) {
                uniqueConfigs.insert(configKey);
                capabilities.append(cap);
                configCount++;

//                qDebug() << "添加配置" << configCount << ":"
//                         << cap.resolution.width() << "x" << cap.resolution.height()
//                         << "-" << cap.codec << "-" << cap.maxFps << "fps"
//                         << "- 首选:" << cap.isPreferred;
            }
        }
    }

    //qDebug() << "=== 解析完成，共找到" << capabilities.size() << "个配置 ===";

    // 如果没有找到任何配置，使用默认值
    if (capabilities.isEmpty()) {
        qDebug() << "未找到任何配置，使用默认值";
        capabilities << CameraCapability(QSize(640, 480), "yuyv422", 30.0, "yuyv422", false)
                    << CameraCapability(QSize(1280, 720), "yuyv422", 15.0, "yuyv422", false)
                    << CameraCapability(QSize(1920, 1080), "mjpeg", 10.0, "mjpeg", true);
    }

    // 按分辨率排序（从大到小），MJPG格式优先
    std::sort(capabilities.begin(), capabilities.end(),
        [](const CameraCapability& a, const CameraCapability& b) {
            // 优先选择MJPG格式
            if (a.isPreferred && !b.isPreferred) return true;
            if (!a.isPreferred && b.isPreferred) return false;

            // 相同格式下按分辨率排序
            int areaA = a.resolution.width() * a.resolution.height();
            int areaB = b.resolution.width() * b.resolution.height();
            return areaA > areaB;
        });

    // 打印最终结果
    //qDebug() << "=== 最终配置列表 ===";
//    for (int i = 0; i < capabilities.size(); ++i) {
//        const CameraCapability& cap = capabilities[i];
//        qDebug() << "配置" << (i+1) << ":" << cap.resolution.width() << "x" << cap.resolution.height()
//                 << "-" << cap.codec << "-" << cap.maxFps << "fps"
//                 << (cap.isPreferred ? "(首选)" : "");
//    }

    return capabilities;
}


QString CameraProbing::executeFFmpegCommand(const QStringList &arguments)
{
    return executeProcess("ffmpeg", arguments, 15000); // 增加超时时间
}

QString CameraProbing::executeProcess(const QString &program, const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels); // 合并标准输出和错误输出

    //qDebug() << "执行命令:" << program << arguments;

    process.start(program, arguments);

    if (!process.waitForStarted(3000)) {
        qDebug() << "无法启动" << program << "进程";
        return QString();
    }

    if (!process.waitForFinished(timeoutMs)) {
        qDebug() << program << "进程超时";
        process.kill();
        return QString();
    }

    QString output = QString::fromLocal8Bit(process.readAll());
    //qDebug() << "进程退出码:" << process.exitCode();
    //qDebug() << "进程输出长度:" << output.length();

    return output;
}

QList<CameraInfo> CameraProbing::getCameraList()
{
    QList<CameraInfo> cameraList;
    cameraList.clear();
    // 获取所有可用的摄像头
    QList<QCameraInfo> cameras = QCameraInfo::availableCameras();
    for (const QCameraInfo &cameraInfo : cameras) {
        CameraInfo st;
        st.deviceName = cameraInfo.deviceName();
        st.description = cameraInfo.description();
        st.formats = probeCameraCapabilities(st.description);
        cameraList.append(st);
    }
    return cameraList;
}
