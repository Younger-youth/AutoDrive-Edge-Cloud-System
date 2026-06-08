#include "MultiTracker.h"
#include <algorithm>
#include <iostream>

MultiTracker::MultiTracker() {
    next_id = 1; // ID 从 1 开始发放
}

float MultiTracker::calculateIoU(const cv::Rect& box1, const cv::Rect& box2) {
    int x1 = std::max(box1.x, box2.x);
    int y1 = std::max(box1.y, box2.y);
    int x2 = std::min(box1.x + box1.width, box2.x + box2.width);
    int y2 = std::min(box1.y + box1.height, box2.y + box2.height);

    int intersection_area = std::max(0, x2 - x1) * std::max(0, y2 - y1);
    int union_area = box1.area() + box2.area() - intersection_area;

    if (union_area == 0) return 0;
    return (float)intersection_area / union_area;
}

void MultiTracker::update(const std::vector<cv::Rect>& detections, std::vector<std::pair<int, cv::Rect>>& output_tracks) {
    output_tracks.clear();

    // 1. 让所有登记在册的车辆，根据卡尔曼滤波预测它们现在应该在哪
    std::vector<cv::Rect> predictions;
    for (auto& track : tracks) {
        predictions.push_back(track.predict());
    }

    // 2. 匹配逻辑 (贪心 IoU 匹配)
    std::vector<bool> matched_detections(detections.size(), false);
    std::vector<bool> matched_tracks(tracks.size(), false);

    // 对于每一个 YOLO 检测到的框，去寻找跟它最重合的预测框
    for (size_t d = 0; d < detections.size(); d++) {
        int best_track_idx = -1;
        float best_iou = 0.3f; // IoU 阈值：重合度必须大于 30% 才算是同一辆车

        for (size_t t = 0; t < tracks.size(); t++) {
            if (matched_tracks[t]) continue; // 这辆车已经匹配过了，跳过

            float iou = calculateIoU(detections[d], predictions[t]);
            if (iou > best_iou) {
                best_iou = iou;
                best_track_idx = t;
            }
        }

        // 如果找到了匹配的预测框
        if (best_track_idx != -1) {
            tracks[best_track_idx].update(detections[d]); // 用真实位置纠正卡尔曼滤波
            matched_detections[d] = true;
            matched_tracks[best_track_idx] = true;
            
            output_tracks.push_back({tracks[best_track_idx].id, detections[d]});
        }
    }

    // 3. 处理还没匹配上的 YOLO 检测框（说明是新闯入画面的车）
    for (size_t d = 0; d < detections.size(); d++) {
        if (!matched_detections[d]) {
            Track new_track(next_id++, detections[d]);
            tracks.push_back(new_track);
            output_tracks.push_back({new_track.id, detections[d]});
        }
    }

    // 4. 清理已经消失的车辆（如果连续 15 帧都没匹配上，说明车开走了）
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
        [](const Track& t) { return t.time_since_update > 15; }), tracks.end());
}