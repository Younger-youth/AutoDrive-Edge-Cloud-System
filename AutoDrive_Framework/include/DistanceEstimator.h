#pragma once
#include <opencv2/opencv.hpp>

// 单目测距引擎：负责将像素坐标降维打击，转化为物理世界的距离（米）
class DistanceEstimator {
public:
    DistanceEstimator();

    // 核心计算接口：输入追踪框，输出这辆车距离我们的纵向距离 (米)
    float calculateDistance(const cv::Rect& bbox);

private:
    // 相机的内外参 (在真实车企中，这些参数是通过标定板测出来的，目前我们用典型假设值)
    float camera_height;       // 摄像头离地高度 (m)
    float camera_focal_length; // 焦距 (像素单位)
    float image_center_y;      // 画面垂直中心点的 y 坐标 (像素)
    float camera_pitch_angle;  // 摄像头俯仰角 (弧度，低头为正)
};
