#pragma once
#include <opencv2/opencv.hpp>
#include <opencv2/video/tracking.hpp>

// 这是一个“被追踪对象”的物理模型蓝图
class Track {
public:
    int id;                // 目标的唯一身份证号
    int time_since_update; // 记录有几帧没看到它了（用于判断目标是否消失）
    cv::KalmanFilter kf;   // 核心：专属的卡尔曼滤波器

    // 构造函数：当 YOLO 发现一个新目标时，初始化它的动力学状态
    Track(int _id, const cv::Rect& bbox);

    // 动作 1：根据动力学模型，预测下一帧的位置
    cv::Rect predict();

    // 动作 2：用 YOLO 最新测到的结果，纠正卡尔曼滤波的内部误差
    void update(const cv::Rect& bbox);
};