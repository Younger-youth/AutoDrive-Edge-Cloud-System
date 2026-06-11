#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/dnn.hpp>
#include <string>
#include <vector>

class YoloDetector {
public:
    // 启动引擎：加载模型
    bool init(const std::string& model_path, int input_width = 640, int input_height = 640, float conf_threshold = 0.25f, float nms_threshold = 0.45f);
    
    // 核心处理模块：吃进一帧原图，吐出一帧画好框的图
    // cv::Mat detect(cv::Mat& frame);
// ⚡ 核心修改：不再吐出图片，而是吐出这一帧里找到的所有车辆坐标框
    std::vector<cv::Rect> detect(cv::Mat& frame);
    
private:
    // 隐藏在机器内部的 AI 脑子
    cv::dnn::Net net; 

    // 探测参数
    int input_w = 640;
    int input_h = 640;
    float conf_thresh = 0.25f;
    float nms_thresh = 0.45f;

    // 隐藏的预处理流水线（API 偷懒版 Letterbox）
    cv::Mat letterbox(const cv::Mat& source, cv::Size target_size, float& scale, int& pad_left, int& pad_top);
};