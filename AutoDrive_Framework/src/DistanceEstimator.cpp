#include "DistanceEstimator.h"
#include <cmath>

DistanceEstimator::DistanceEstimator() {
    // 假设这是一个装在重型车辆或者乘用车内后视镜位置的行车记录仪
    camera_height = 1.5f;          // 离地 1.5 米高
    camera_focal_length = 800.0f;  // 这是一个典型的 720p 相机焦距估算值
    image_center_y = 360.0f;       // 720p 高度的一半 (720 / 2)
    camera_pitch_angle = 0.0f;     // 假设摄像头完全水平，没有低头
}

float DistanceEstimator::calculateDistance(const cv::Rect& bbox) {
    // 1. 寻找物理世界的“接地点”：也就是车轮与地面的接触点
    // 我们用 YOLO 框的底部中心来近似代表轮胎接触地面的位置
    float bottom_y = bbox.y + bbox.height;

    // 如果目标在画面的上半部分（比如天上的鸟、或者极远处的噪点），
    // 它的底部已经超过了地平线，此时无法进行平地假设测距，返回 -1 表示无效。
    if (bottom_y <= image_center_y) {
        return -1.0f; 
    }

    // 2. 核心三角测量公式
    // dy 是车轮接地点距离画面中心线的像素差
    float dy = bottom_y - image_center_y;
    
    // 计算这条光线与水平面的夹角
    float angle = camera_pitch_angle + atan(dy / camera_focal_length);
    
    if (angle <= 0) return -1.0f; // 防止异常角度

    // 3. 几何映射：距离 = 高度 / tan(角度)
    float distance = camera_height / tan(angle);
    
    return distance;
}
