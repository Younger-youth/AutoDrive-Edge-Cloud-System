#include <iostream>
#include <opencv2/opencv.hpp>
#include <opencv2/core/utils/logger.hpp> 
#include <yaml-cpp/yaml.h>
#include <thread>
#include <chrono>
#include <mutex>

#include "YoloDetector.h" 
#include "MultiTracker.h" 
#include "DistanceEstimator.h"
#include "httplib.h"
#include "json.hpp"
#include "PubSub.h"

using namespace std;
using namespace cv;
using json = nlohmann::json;

// Global helper to format string
string format_string(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int size = vsnprintf(nullptr, 0, fmt, args) + 1;
    va_end(args);
    
    vector<char> buf(size);
    va_start(args, fmt);
    vsnprintf(buf.data(), size, fmt, args);
    va_end(args);
    
    return string(buf.data(), size - 1);
}

int main(int argc, char** argv) {
    SetConsoleOutputCP(CP_UTF8);
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

    string node_type = "";
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--node" && i + 1 < argc) {
            node_type = argv[i + 1];
        }
    }

    if (node_type == "") {
        cout << "Usage: main_video.exe --node [broker|publisher|detector|cloud] [config_path]" << endl;
        return 0;
    }

    // ----------------------------------------------------
    // 1. Broker Node Mode
    // ----------------------------------------------------
    if (node_type == "broker") {
        cout << "[NODE] Starting Message Broker..." << endl;
        PubSubBroker broker(9000);
        broker.start();
        return 0;
    }

    // ----------------------------------------------------
    // 2. Video Publisher Node Mode
    // ----------------------------------------------------
    if (node_type == "publisher") {
        cout << "[NODE] Starting Video Publisher..." << endl;

        string config_path = "D:/AutoDrive_System/AutoDrive_Framework/config.yaml";
        for (int i = 1; i < argc; ++i) {
            if (string(argv[i]) != "--node" && string(argv[i - 1]) != "--node") {
                config_path = argv[i];
            }
        }

        YAML::Node config;
        try {
            config = YAML::LoadFile(config_path);
        } catch (const YAML::Exception& e) {
            cout << "[ERROR] Cannot load YAML config: " << config_path << endl;
            return -1;
        }

        string video_path = config["system"]["video_source"].as<string>();
        VideoCapture cap(video_path);
        if (!cap.isOpened()) {
            cout << "[ERROR] Cannot open video: " << video_path << endl;
            return -1;
        }

        int total_frames = (int)cap.get(CAP_PROP_FRAME_COUNT);

        PubSubClient client("127.0.0.1", 9000);
        if (!client.connect_to_broker()) {
            return -1;
        }

        std::mutex state_mutex;
        bool is_paused = false;
        int seek_to_frame = -1;

        client.subscribe("/control/command", [&](const json& payload) {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (payload.contains("is_paused")) {
                is_paused = payload["is_paused"].get<bool>();
            }
            if (payload.contains("seek_to_frame")) {
                seek_to_frame = payload["seek_to_frame"].get<int>();
            }
        });

        int frame_count = 0;
        Mat frame;

        while (true) {
            bool current_paused = false;
            int current_seek = -1;
            {
                std::lock_guard<std::mutex> lock(state_mutex);
                current_paused = is_paused;
                current_seek = seek_to_frame;
                seek_to_frame = -1; 
            }

            if (current_seek >= 0) {
                cap.set(CAP_PROP_POS_FRAMES, current_seek);
                frame_count = current_seek;
            }

            if (current_paused) {
                json heart_beat;
                heart_beat["frame_id"] = frame_count;
                heart_beat["total_frames"] = total_frames;
                heart_beat["is_paused"] = true;
                heart_beat["image"] = "";
                client.publish("/camera/image_raw", heart_beat);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            cap >> frame;
            if (frame.empty()) {
                cap.set(CAP_PROP_POS_FRAMES, 0);
                frame_count = 0;
                continue;
            }

            vector<uchar> buf;
            imencode(".jpg", frame, buf);
            string b64_img = base64_encode(buf.data(), static_cast<unsigned int>(buf.size()));

            json img_msg;
            img_msg["frame_id"] = frame_count++;
            img_msg["total_frames"] = total_frames;
            img_msg["is_paused"] = false;
            img_msg["image"] = b64_img;

            client.publish("/camera/image_raw", img_msg);

            std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }

        return 0;
    }

    // ----------------------------------------------------
    // 3. YOLO Detection Node Mode
    // ----------------------------------------------------
    if (node_type == "detector") {
        cout << "[NODE] Starting YOLO Perception Detector..." << endl;

        string config_path = "D:/AutoDrive_System/AutoDrive_Framework/config.yaml";
        for (int i = 1; i < argc; ++i) {
            if (string(argv[i]) != "--node" && string(argv[i - 1]) != "--node") {
                config_path = argv[i];
            }
        }

        YAML::Node config;
        try {
            config = YAML::LoadFile(config_path);
        } catch (const YAML::Exception& e) {
            cout << "[ERROR] Cannot load YAML config: " << config_path << endl;
            return -1;
        }

        string model_path = config["perception"]["model_path"].as<string>();
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
        if (!detector.init(model_path, input_w, input_h, conf_threshold, nms_threshold)) {
            cout << "[ERROR] YOLO init failed" << endl;
            return -1;
        }

        MultiTracker tracker;
        DistanceEstimator distance_estimator;

        PubSubClient client("127.0.0.1", 9000);
        if (!client.connect_to_broker()) {
            return -1;
        }

        TickMeter tm;

        client.subscribe("/camera/image_raw", [&](const json& payload) {
            int frame_id = payload["frame_id"].get<int>();
            int total_frames = payload["total_frames"].get<int>();
            bool is_paused = payload["is_paused"].get<bool>();

            if (is_paused) {
                json out_msg;
                out_msg["frame_id"] = frame_id;
                out_msg["total_frames"] = total_frames;
                out_msg["is_paused"] = true;
                out_msg["objects"] = json::array();
                out_msg["rendered_image"] = "";
                client.publish("/perception/detections", out_msg);
                return;
            }

            string b64_img = payload["image"].get<string>();
            string decoded = base64_decode(b64_img);
            vector<uchar> buf(decoded.begin(), decoded.end());
            Mat frame = imdecode(buf, IMREAD_COLOR);

            if (frame.empty()) return;

            tm.start();
            vector<Rect> detections = detector.detect(frame);
            vector<pair<int, Rect>> tracked_objects;
            tracker.update(detections, tracked_objects);

            json objects_json = json::array();

            for (const auto& obj : tracked_objects) {
                int id = obj.first;
                Rect box = obj.second;
                float dist_m = distance_estimator.calculateDistance(box);

                if (dist_m > 0) {
                    json item;
                    item["target_id"] = id;
                    item["distance"] = dist_m;
                    objects_json.push_back(item);
                }

                rectangle(frame, box, Scalar(255, 144, 30), 2);
                string label = "ID: " + to_string(id);
                if (dist_m > 0) {
                    label += " D: " + format_string("%.1f", dist_m) + "m";
                }
                int baseLine;
                Size labelSize = getTextSize(label, FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseLine);
                rectangle(frame, Point(box.x, box.y - labelSize.height - 5), 
                          Point(box.x + labelSize.width, box.y), Scalar(255, 144, 30), FILLED);
                putText(frame, label, Point(box.x, box.y - 5), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0, 0, 0), 2);
            }

            tm.stop();
            string fps_text = "FPS: " + format_string("%.1f", tm.getFPS());
            putText(frame, fps_text, Point(20, 40), FONT_HERSHEY_SIMPLEX, 1.0, Scalar(0, 0, 255), 2);
            tm.reset();

            vector<uchar> out_buf;
            imencode(".jpg", frame, out_buf);
            string out_b64 = base64_encode(out_buf.data(), static_cast<unsigned int>(out_buf.size()));

            json out_msg;
            out_msg["frame_id"] = frame_id;
            out_msg["total_frames"] = total_frames;
            out_msg["is_paused"] = false;
            out_msg["objects"] = objects_json;
            out_msg["rendered_image"] = out_b64;

            client.publish("/perception/detections", out_msg);
        });

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return 0;
    }

    // ----------------------------------------------------
    // 4. Cloud Connector Node Mode
    // ----------------------------------------------------
    if (node_type == "cloud") {
        cout << "[NODE] Starting Cloud Connector..." << endl;

        PubSubClient client("127.0.0.1", 9000);
        if (!client.connect_to_broker()) {
            return -1;
        }

        httplib::Client cli("http://127.0.0.1:8000");
        cli.set_connection_timeout(0, 100000);

        client.subscribe("/perception/detections", [&](const json& payload) {
            int frame_id = payload["frame_id"].get<int>();
            int total_frames = payload["total_frames"].get<int>();
            bool is_paused = payload["is_paused"].get<bool>();

            json djangopayload;
            djangopayload["frame_id"] = frame_id;
            djangopayload["total_frames"] = total_frames;
            
            if (is_paused) {
                djangopayload["objects"] = json::array();
            } else {
                djangopayload["objects"] = payload["objects"];
                
                string out_b64 = payload["rendered_image"].get<string>();
                string decoded = base64_decode(out_b64);
                
                cli.Post("/api/perception/upload_frame/", decoded, "image/jpeg");
            }

            string json_str = djangopayload.dump();
            if (auto res = cli.Post("/api/perception/upload/", json_str, "application/json")) {
                if (res->status == 200) {
                    try {
                        auto resp_json = json::parse(res->body);
                        json cmd;
                        bool has_cmd = false;

                        if (resp_json.contains("is_paused")) {
                            cmd["is_paused"] = resp_json["is_paused"].get<bool>();
                            has_cmd = true;
                        }
                        if (resp_json.contains("seek_to_frame")) {
                            int seek_to = resp_json["seek_to_frame"].get<int>();
                            if (seek_to >= 0) {
                                cout << "[INFO] 🎛️ Cloud got seek instruction to: " << seek_to << endl;
                                cmd["seek_to_frame"] = seek_to;
                                has_cmd = true;
                            }
                        }

                        if (has_cmd) {
                            client.publish("/control/command", cmd);
                        }
                    } catch (const exception& e) {
                        // ignore parsing exception
                    }
                }
            }
        });

        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        return 0;
    }

    return 0;
}