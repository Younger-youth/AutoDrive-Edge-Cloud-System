#pragma once
#include "Tracker.h"
#include <vector>

// 多目标追踪调度中心
class MultiTracker {
public:
    MultiTracker();

    // 核心接口：吃进 YOLO 的无脑检测框，吐出带有 ID 的追踪记录
    void update(const std::vector<cv::Rect>& detections, std::vector<std::pair<int, cv::Rect>>& output_tracks);

private:
    int next_id;                  // 记录下一个要发放的车辆 ID
    std::vector<Track> tracks;    // 登记在册的所有活跃车辆

    // 基础算理：计算两个框的重合度 (Intersection over Union)
    float calculateIoU(const cv::Rect& box1, const cv::Rect& box2);
};