#include "Tracker.h"

Track::Track(int _id, const cv::Rect& bbox) {
    id = _id;
    time_since_update = 0;

    // 1. 初始化卡尔曼滤波器 
    // 状态维度 8: [cx, cy, w, h, v_cx, v_cy, v_w, v_h] (中心点坐标，宽高，以及它们的速度)
    // 测量维度 4: [cx, cy, w, h] (YOLO 只能测出位置和大小，测不出速度)
    kf = cv::KalmanFilter(8, 4, 0);

    // 2. 状态转移矩阵 A (State Transition Matrix)
    // 这就是一个极其经典的离散时间匀速运动学方程：x(t) = x(t-1) + v * dt (假设 dt = 1)
    kf.transitionMatrix = (cv::Mat_<float>(8, 8) << 
        1, 0, 0, 0, 1, 0, 0, 0,  // cx_new = cx + v_cx
        0, 1, 0, 0, 0, 1, 0, 0,  // cy_new = cy + v_cy
        0, 0, 1, 0, 0, 0, 1, 0,  // w_new = w + v_w
        0, 0, 0, 1, 0, 0, 0, 1,  // h_new = h + v_h
        0, 0, 0, 0, 1, 0, 0, 0,  // v_cx 保持不变
        0, 0, 0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 0, 0, 1);

    // 3. 测量矩阵 H (Measurement Matrix)
    // 告诉滤波器，YOLO 传进来的 4 个值分别对应状态变量里的前 4 个
    cv::setIdentity(kf.measurementMatrix);

    // 4. 噪声协方差矩阵 (经验值，用来控制系统对 YOLO 的信任度 vs 对物理预测的信任度)
    cv::setIdentity(kf.processNoiseCov, cv::Scalar::all(1e-2));
    cv::setIdentity(kf.measurementNoiseCov, cv::Scalar::all(1e-1));
    cv::setIdentity(kf.errorCovPost, cv::Scalar::all(1));

    // 5. 初始化第一帧的状态量
    float cx = bbox.x + bbox.width / 2.0f;
    float cy = bbox.y + bbox.height / 2.0f;
    kf.statePost = (cv::Mat_<float>(8, 1) << cx, cy, bbox.width, bbox.height, 0, 0, 0, 0);
}

// ================== 预测动作 ==================
cv::Rect Track::predict() {
    // 矩阵乘法：X_new = A * X_old
    cv::Mat prediction = kf.predict();
    
    // 把预测出的中心点和宽高，重新转换回 OpenCV 认识的 Rect 框
    float cx = prediction.at<float>(0);
    float cy = prediction.at<float>(1);
    float w  = prediction.at<float>(2);
    float h  = prediction.at<float>(3);
    
    time_since_update++; // 预测了一次，说明距离上次 YOLO 更新又远了一帧
    return cv::Rect(cx - w / 2, cy - h / 2, w, h);
}

// ================== 更新动作 ==================
void Track::update(const cv::Rect& bbox) {
    time_since_update = 0; // 成功匹配到了 YOLO 的真实框，计时器清零！

    // 将 YOLO 的 Rect 提取出中心点和宽高
    float cx = bbox.x + bbox.width / 2.0f;
    float cy = bbox.y + bbox.height / 2.0f;
    cv::Mat measurement = (cv::Mat_<float>(4, 1) << cx, cy, bbox.width, bbox.height);
    
    // 卡尔曼增益计算与状态更新
    kf.correct(measurement);
}