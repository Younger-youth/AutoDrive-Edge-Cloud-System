#include "YoloDetector.h"
#include <iostream>

using namespace cv;
using namespace std;

// 1. 启动引擎：加载 ONNX 模型
bool YoloDetector::init(const string& model_path, int input_width, int input_height, float conf_threshold, float nms_threshold) {
    conf_thresh = conf_threshold;
    nms_thresh = nms_threshold;
    
    // 🎯 核心防护：由于 ONNX 模型是静态导出的，输入尺寸必须与模型内部 DFL Reshape 层完全匹配，否则 OpenCV 会发生 Assertion 崩溃。
    if (model_path.find("yolov8n") != string::npos) {
        input_w = 320;
        input_h = 320;
    } else if (model_path.find("yolov8s") != string::npos || 
               model_path.find("yolov8m") != string::npos || 
               model_path.find("yolov8x") != string::npos) {
        input_w = 640;
        input_h = 640;
    } else {
        input_w = input_width;
        input_h = input_height;
    }
    
    try {
        cout << "[INFO] 正在尝试加载模型: " << model_path << endl;
        cout << "[INFO] 📐 自动适配输入尺寸: " << input_w << "x" << input_h << endl;
        net = dnn::readNetFromONNX(model_path);
        net.setPreferableBackend(dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(dnn::DNN_TARGET_CPU); // 未来有显卡可改为 DNN_TARGET_CUDA
        cout << "[INFO] 🧠 YOLO 引擎点火成功！" << endl;
        return true;
    } catch (const cv::Exception& e) {
        cout << "\n=============================================" << endl;
        cout << "[FATAL ERROR] ❌ OpenCV 核心崩溃！" << endl;
        cout << "👉 真实死因: " << e.what() << endl;
        cout << "=============================================\n" << endl;
        return false;
    } catch (const std::exception& e) {
        cout << "[ERROR] ❌ C++ 标准异常: " << e.what() << endl;
        return false;
    }
}

// 2. 图像预处理：等比例缩放并填充灰边 (Letterbox)
Mat YoloDetector::letterbox(const Mat& source, Size target_size, float& scale, int& pad_left, int& pad_top) {
    Mat result;
    float scale_x = (float)target_size.width / source.cols;
    float scale_y = (float)target_size.height / source.rows;
    scale = min(scale_x, scale_y);

    int new_unpad_w = int(round(source.cols * scale));
    int new_unpad_h = int(round(source.rows * scale));

    pad_left = (target_size.width - new_unpad_w) / 2;
    pad_top = (target_size.height - new_unpad_h) / 2;

    resize(source, result, Size(new_unpad_w, new_unpad_h));
    copyMakeBorder(result, result, pad_top, target_size.height - new_unpad_h - pad_top,
                   pad_left, target_size.width - new_unpad_w - pad_left, BORDER_CONSTANT, Scalar(114, 114, 114));
    return result;
}

// 3. 核心推理与后处理
vector<Rect> YoloDetector::detect(Mat& frame)  {
    float scale;
    int pad_left, pad_top;
    
    // ⚡ 动态尺寸进行 Letterbox 和 Blob 转换
    Mat letterbox_img = letterbox(frame, Size(input_w, input_h), scale, pad_left, pad_top);

    Mat blob = dnn::blobFromImage(letterbox_img, 1.0 / 255.0, Size(input_w, input_h), Scalar(), true, false);
    net.setInput(blob);

    vector<Mat> net_outputs;
    net.forward(net_outputs, net.getUnconnectedOutLayersNames());

    // ⚡ 工业级动态解析：彻底告别越界闪退
    int out_rows = net_outputs[0].size[1]; // 通常是 84 (4 个框坐标 + 80 个类别概率)
    int out_cols = net_outputs[0].size[2]; // 动态框数量 (比如 8400 或 2100)

    Mat output_matrix(out_rows, out_cols, CV_32F, net_outputs[0].data);
    Mat transposed_matrix;
    transpose(output_matrix, transposed_matrix);
    float* p = (float*)transposed_matrix.data;

    vector<Rect> boxes;
    vector<float> confidences;
    vector<int> class_ids;

    for (int i = 0; i < out_cols; i++) {
        float max_class_score = 0;
        int best_class_id = -1;
        
        for (int c = 4; c < out_rows; c++) {
            if (p[c] > max_class_score) {
                max_class_score = p[c];
                best_class_id = c - 4;
            }
        }

        if (max_class_score > conf_thresh) { // 置信度阈值
            float cx = p[0], cy = p[1], w = p[2], h = p[3];
            int left = int((cx - 0.5 * w - pad_left) / scale);
            int top = int((cy - 0.5 * h - pad_top) / scale);
            int width = int(w / scale);
            int height = int(h / scale);

            boxes.push_back(Rect(left, top, width, height));
            confidences.push_back(max_class_score);
            class_ids.push_back(best_class_id);
        }
        p += out_rows; // 指针安全跨越
    }

    // ⚡ NMS 非极大值抑制：把重叠的废框全部过滤掉
    vector<int> indices;
    dnn::NMSBoxes(boxes, confidences, conf_thresh, nms_thresh, indices);
    vector<Rect> final_detections; // 准备一个空车厢，用来装纯净的坐标数据

    for (int idx : indices) {
        int cid = class_ids[idx];
        
        // 工业级过滤：COCO 数据集里 2是汽车(car), 5是公交(bus), 7是卡车(truck)
        // 我们只把车辆的坐标传给下游追踪器，如果是人、猫、狗、红绿灯，直接无视！
        if (cid == 2 || cid == 5 || cid == 7) { 
            final_detections.push_back(boxes[idx]); // 把合格 of 车辆坐标装进车厢
        }
    }

    return final_detections; // 把装满坐标的车厢发往主函数，不再返回 Mat 图片了！
} // 这里是你 detect 函数原本的结束大括号
