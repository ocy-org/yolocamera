#include "yolodetector.h"
#include <algorithm>
#include <QDebug>

YoloDetector::YoloDetector(const std::string& modelPath)
{
    // 从 ONNX 文件加载模型（解析计算图、权重，构建 OpenCV Net 对象）
    net = cv::dnn::readNetFromONNX(modelPath);
    // 指定推理后端为 OpenCV 内置实现
    net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    // 指定推理目标设备为 CPU
    net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
}
YoloDetector::~YoloDetector()
{

}

// ==================== 核心方法：目标检测 ====================
// 完整流程：预处理(letterbox) -> 归一化转blob -> 前向推理 -> reshape -> 解码 -> NMS
std::vector<DetResult> YoloDetector::detect(const cv::Mat& frame, float confThresh)
{
    std::vector<DetResult> results;// 存放最终检测结果
    if (frame.empty()) return results;

    // ==================== 第一阶段：预处理 - Letterbox 缩放与填充 ====================
    // Letterbox 的目的：保持目标宽高比不变形地缩放到模型输入尺寸（640x640）
    // 取宽/高中缩放比例较小的一个，确保图像能完整放入画布
    // 计算缩放比例：inputSize / 原图尺寸，取较小值保证不裁剪
    float scale = std::min((float)inputSize.width / frame.cols,
                           (float)inputSize.height / frame.rows);
    int newW = cvRound(frame.cols * scale);
    int newH = cvRound(frame.rows * scale);
    // 计算左右/上下边距（居中放置时两侧各填充的像素数）
    int padW = (inputSize.width - newW) / 2;
    int padH = (inputSize.height - newH) / 2;

    cv::Mat resized, padded;
    // 执行缩放：将原图按比例 resize 到 newW x newH
    cv::resize(frame, resized, cv::Size(newW, newH));
    padded = cv::Mat(inputSize, CV_8UC3, cv::Scalar(114, 114, 114));
    // 将缩放后的图像粘贴到画布中央区域
    resized.copyTo(padded(cv::Rect(padW, padH, newW, newH)));

    // ==================== 第二阶段：构建 Blob 并推理 ====================
    // blobFromImage 一次性完成三项操作：
    //   1) scalefactor = 1/255.0 -> 像素值归一化到 [0, 1]
    //   2) swapRB = true -> BGR 通道互换为 RGB（与模型训练时一致）
    //   3) crop = false + 已 resize -> 输出 HWC -> CHW 维度重排
    // 最终输出形状：[1, 3, 640, 640] 的 4 维 float 张量
    cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0 / 255.0, inputSize,
                                          cv::Scalar(), true, false, CV_32F);
    // 将 blob 设为网络输入张量（内存零拷贝）
    net.setInput(blob);
    // 前向推理：执行整个 ONNX 计算图，得到输出张量
    cv::Mat out = net.forward();   // [1, 84, 8400]
    qDebug() << "det out dims:" << out.dims << out.size[1] << out.size[2];

    // ==================== 第三阶段：输出张量标准化 ====================
    // 不同版本的 ONNX 模型可能输出 [1, 84, 8400] 或 [1, 8400, 84]
    // 这里统一转换为二维矩阵 [84, 8400]，便于按行索引读取属性
    cv::Mat pred;
    if (out.dims == 3) {
        int d1 = out.size[1], d2 = out.size[2];
        if (d1 == 8400 && d2 != 8400) {
            cv::Mat t = out.reshape(1, {d1, d2});
            cv::transpose(t, pred);          // [84, 8400]
        } else {
            pred = out.reshape(1, {d1, d2}); // [84, 8400]
        }
    } else {
        pred = out.reshape(1, {out.size[0], out.size[1]});
    }

    int rows = pred.rows;                    // 84 = 4 + 80
    int numC = rows - 4;                     // 80 个类别
    int numP = pred.cols;                    // 8400
    // 确保矩阵在内存中连续（reshape/transpose 后可能需要 clone）
    // 如果不连续，ptr<float>(0) 跨行访问会读到错误数据
    CV_Assert(pred.isContinuous());
    const float* base = pred.ptr<float>(0);
    const int step = numP;                   //跨行读取，和分割版同样的关键点

    // ==================== 第四阶段：解码候选框 ====================
    // 临时结构体：存储通过置信度过滤后的候选框
    struct Cand { cv::Rect box; float conf; int cls; };
    std::vector<Cand> cands;
    // 遍历全部 8400 个预测框
    for (int i = 0; i < numP; ++i) {
        // 从矩阵第 [0~3] 行读取当前框的 cx, cy, w, h（中心点坐标 + 宽高，单位：letterbox 图像素）
        float cx = base[0 * step + i];  // 第0行第i列 -> 中心x
        float cy = base[1 * step + i];  // 第1行第i列 -> 中心y
        float w  = base[2 * step + i];  // 第2行第i列 -> 宽度
        float h  = base[3 * step + i];  // 第3行第i列 -> 高度
        // 扫描 80 个类别得分，找最大值对应的类别 ID
        int bestCls = -1;
        float bestRaw = -1e30f;
        for (int c = 0; c < numC; ++c) {
            float s = base[(4 + c) * step + i];
            if (s > bestRaw) { bestRaw = s; bestCls = c; }
        }
        // 置信度处理
        float bestScore = (bestRaw >= 0.f && bestRaw <= 1.f)
                          ? bestRaw
                          : 1.f / (1.f + std::exp(-bestRaw));
        if (bestScore < confThresh) continue;
        // 坐标还原：从 letterbox 空间映射回原始图像空间
        float x0 = (cx - w * 0.5f - padW) / scale;
        float y0 = (cy - h * 0.5f - padH) / scale;
        // 构建矩形框并裁剪到图像边界内
        cv::Rect box(cvRound(x0), cvRound(y0), cvRound(w / scale), cvRound(h / scale));
        box &= cv::Rect(0, 0, frame.cols, frame.rows);
        if (box.area() <= 0) continue;

        cands.push_back({box, bestScore, bestCls});
    }

    // ==================== 第五阶段：NMS（非极大值抑制） ====================
    // 目的：去除同一目标的重复检测框，只保留置信度最高的那个
    // 方法：IoU（交并比）> 阈值 的框被抑制

    // 按置信度降序排序（高分框优先保留）
    std::sort(cands.begin(), cands.end(),
              [](const Cand& a, const Cand& b) { return a.conf > b.conf; });
    std::vector<bool> removed(cands.size(), false);
    // 与所有后续框比较，IoU 超过阈值则标记为"移除"
    for (size_t i = 0; i < cands.size(); ++i) {
        if (removed[i]) continue;
        results.push_back(DetResult{cands[i].box, cands[i].conf, cands[i].cls});
        for (size_t j = i + 1; j < cands.size(); ++j) {
            if (removed[j]) continue;
            float inter = (float)(cands[i].box & cands[j].box).area();
            float uni = cands[i].box.area() + cands[j].box.area() - inter;
            if (uni > 0 && inter / uni > 0.45f) removed[j] = true;
        }
    }
    return results;
}
