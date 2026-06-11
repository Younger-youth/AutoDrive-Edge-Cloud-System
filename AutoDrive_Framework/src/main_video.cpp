#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp> 
#include <yaml-cpp/yaml.h>
#include <thread>
#include <chrono>

// 引入自动驾驶核心模块
#include "YoloDetector.h" 
#include "MultiTracker.h" 
#include "DistanceEstimator.h"

// 新增：引入网络与 JSON 库
#define WIN32_LEAN_AND_MEAN // 避免 Windows 头文件冲突
#include "httplib.h"
#include "json.hpp"

using namespace std;
using namespace cv;
using json = nlohmann::json; // 定义 JSON 命名空间别名

int main(int argc, char** argv) {

    SetConsoleOutputCP(CP_UTF8);
    
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);
    cout << "[INFO] 自动驾驶感知与追踪系统启动中..." << endl;

    string config_path = "D:/AutoDrive_System/AutoDrive_Framework/config.yaml";
    if (argc >= 2) config_path = argv[1]; 

    YAML::Node config;
    try {
        config = YAML::LoadFile(config_path);
    } catch (const YAML::Exception& e) {
        cout << "[ERROR] 无法读取 YAML！" << endl;
        return -1;
    }

    string model_path = config["perception"]["model_path"].as<string>();
    string video_path = config["system"]["video_source"].as<string>();

    bool show_gui = false; // 默认不弹出本地 GUI 窗口
    if (config["system"]["show_gui"]) {
        show_gui = config["system"]["show_gui"].as<bool>();
    }

    int input_w = 640;
    int input_h = 640;
    float conf_threshold = 0.25f;
    float nms_threshold = 0.45f;

    if (config["perception"]["input_size"] && config["perception"]["input_size"].IsSequence() && config["perception"]["input_size"].size() == 2) {
        input_w = config["perception"]["input_size"][0].as<int>();
        input_h = config["perception"]["input_size"][1].as<int>();
    }
    if (config["perception"]["conf_threshold"]) {
        conf_threshold = config["perception"]["conf_threshold"].as<float>();
    }
    if (config["perception"]["nms_threshold"]) {
        nms_threshold = config["perception"]["nms_threshold"].as<float>();
    }

    YoloDetector detector;
    if (!detector.init(model_path, input_w, input_h, conf_threshold, nms_threshold)) return -1;

    MultiTracker tracker; 
    DistanceEstimator distance_estimator;
    
    // 新增：实例化 HTTP 客户端，指向本地的 Django 默认端口 8000
    httplib::Client cli("http://127.0.0.1:8000");
    // 设置连接超时时间为 100 毫秒，防止由于 Django 未启动导致 C++ 视频流卡顿
    cli.set_connection_timeout(0, 100000); 

    VideoCapture cap(video_path); 
    if (!cap.isOpened()) return -1;

    // 获取视频的总帧数，用于同步前端可拖拽进度条
    int total_frames = (int)cap.get(CAP_PROP_FRAME_COUNT);

    Mat frame;
    if (show_gui) {
        namedWindow("Autonomous Perception", WINDOW_NORMAL);
        resizeWindow("Autonomous Perception", 1280, 720);
    }
    TickMeter tm;

    int frame_count = 0; // 新增：记录当前帧编号
    bool is_paused = false; // 初始不暂停

    while (true) {
        // 🎯 暂停机制：如果检测到暂停指令，进入低占用的心跳轮询状态，直到被 unpause
        if (is_paused) {
            json payload;
            payload["frame_id"] = frame_count;
            payload["total_frames"] = total_frames;
            payload["objects"] = json::array();
            string json_str = payload.dump();
            
            if (auto res = cli.Post("/api/perception/upload/", json_str, "application/json")) {
                if (res->status == 200) {
                    try {
                        auto resp_json = json::parse(res->body);
                        if (resp_json.contains("is_paused")) {
                            is_paused = resp_json["is_paused"].get<bool>();
                        }
                        if (resp_json.contains("seek_to_frame")) {
                            int seek_to = resp_json["seek_to_frame"].get<int>();
                            if (seek_to >= 0) {
                                cap.set(CAP_PROP_POS_FRAMES, seek_to);
                                frame_count = seek_to;
                            }
                        }
                    } catch (const exception& e) {}
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        cap >> frame; 
        if (frame.empty()) break;

        tm.start();
        vector<Rect> detections = detector.detect(frame);
        vector<pair<int, Rect>> tracked_objects;
        tracker.update(detections, tracked_objects);
        
        // 新增：初始化当前帧的 JSON 数据包
        json payload;
        payload["frame_id"] = frame_count++;
        payload["objects"] = json::array();

        for (const auto& obj : tracked_objects) {
            int id = obj.first;
            Rect box = obj.second;
            float dist_m = distance_estimator.calculateDistance(box);
            
            // 新增：如果测距有效，将数据写入 JSON
            if (dist_m > 0) {
                json item;
                item["target_id"] = id;
                item["distance"] = dist_m;
                payload["objects"].push_back(item);
            }

            // 原有渲染逻辑保持不变
            rectangle(frame, box, Scalar(255, 144, 30), 2);
            string label = "ID: " + to_string(id);
            if (dist_m > 0) {
                label += " D: " + format("%.1f", dist_m) + "m";
            }
            int baseLine;
            Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseLine);
            rectangle(frame, Point(box.x, box.y - labelSize.height - 5), 
                      Point(box.x + labelSize.width, box.y), Scalar(255, 144, 30), FILLED);
            putText(frame, label, Point(box.x, box.y - 5), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 0), 2);
        }

        // 🎯 核心变更：每帧都向 Django 发送帧状态（包含当前帧号和总帧数），用作心跳同步，并读取下发的 seek 定位信号
        payload["total_frames"] = total_frames;
        string json_str = payload.dump();
        
        if (auto res = cli.Post("/api/perception/upload/", json_str, "application/json")) {
            if (res->status == 200) {
                try {
                    auto resp_json = json::parse(res->body);
                    if (resp_json.contains("is_paused")) {
                        is_paused = resp_json["is_paused"].get<bool>();
                    }
                    if (resp_json.contains("seek_to_frame")) {
                        int seek_to = resp_json["seek_to_frame"].get<int>();
                        if (seek_to >= 0) {
                            cout << "[INFO] 🎛️ 收到网页定位指令，正在跳转到帧: " << seek_to << endl;
                            cap.set(CAP_PROP_POS_FRAMES, seek_to);
                            frame_count = seek_to; // 同步本地帧号计数器
                        }
                    }
                } catch (const exception& e) {
                    // 解析异常无需崩溃，直接跳过
                }
            }
        }

        tm.stop();
        string fps_text = "FPS: " + format("%.1f", tm.getFPS());
        putText(frame, fps_text, Point(20, 40), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 0, 255), 2);
        tm.reset();

        // 新增：将带识别框的实时图像帧压缩并上传至 Django 视频流缓存接口
        vector<uchar> buf;
        imencode(".jpg", frame, buf);
        string image_data(buf.begin(), buf.end());
        cli.Post("/api/perception/upload_frame/", image_data, "image/jpeg");

        if (show_gui) {
            imshow("Autonomous Perception", frame);
        }
        if (waitKey(1) == 27) break;
    }
    cap.release();
    if (show_gui) {
        destroyAllWindows();
    }
    return 0;
}