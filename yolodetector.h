#ifndef YOLODETECTOR_H
#define YOLODETECTOR_H

#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <vector>
#include <string>

struct DetResult {
    cv::Rect box;
    float conf;
    int classId;
    cv::Mat mask;
};

class YoloDetector
{
public:
    explicit YoloDetector(const std::string& modelPath);
    ~YoloDetector();
    std::vector<DetResult> detect(const cv::Mat& frame,
                                  float confThresh = 0.45f);
private:
    cv::dnn::Net net;
    const cv::Size inputSize{640, 640};
};

#endif
