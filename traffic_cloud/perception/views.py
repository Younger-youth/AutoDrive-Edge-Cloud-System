from django.shortcuts import render
from django.http import JsonResponse, StreamingHttpResponse
from django.views.decorators.csrf import csrf_exempt
import json
import subprocess
import os
import yaml
import time


# 新增：定义一个全局变量，用来在内存中暂存 C++ 传来的最新一帧数据
LAST_PERCEPTION_DATA = {
    "frame_id": 0,
    "objects": []
}

# 新增：用于暂存 C++ 上传的实时画框视频帧字节数据
LATEST_FRAME = None

# 新增：用于暂存前端下发的视频定位帧目标指令，-1 表示无定位请求
SEEK_COMMAND_FRAME = -1

# 新增：用于暂存前端视频流暂停播放控制，True 表示被暂停，False 表示正常播放
IS_PAUSED = False

@csrf_exempt
def upload_frame(request):
    global LATEST_FRAME
    if request.method == 'POST':
        LATEST_FRAME = request.body
        return JsonResponse({"status": "success", "code": 200})
    return JsonResponse({"status": "invalid_method"}, status=405)

def gen_frames():
    global LATEST_FRAME
    while True:
        if LATEST_FRAME:
            yield (b'--frame\r\n'
                   b'Content-Type: image/jpeg\r\n\r\n' + LATEST_FRAME + b'\r\n')
        # 控制流传输频率，大约 30 FPS，缓解 CPU 负载
        time.sleep(0.03)

def video_feed(request):
    return StreamingHttpResponse(gen_frames(), content_type='multipart/x-mixed-replace; boundary=frame')

@csrf_exempt
def receive_data(request):
    global LAST_PERCEPTION_DATA, SEEK_COMMAND_FRAME, IS_PAUSED
    if request.method == 'POST':
        try:
            body_unicode = request.body.decode('utf-8')
            data = json.loads(body_unicode)
            
            # 更新全局变量
            LAST_PERCEPTION_DATA = data
            
            # 提取下发当前挂起的定位指令，并在下发后重置为 -1
            seek_to = SEEK_COMMAND_FRAME
            SEEK_COMMAND_FRAME = -1
            
            return JsonResponse({
                "status": "success", 
                "code": 200,
                "seek_to_frame": seek_to,
                "is_paused": IS_PAUSED
            })
        except Exception as e:
            return JsonResponse({"status": "error", "message": str(e)}, status=400)
            
    return JsonResponse({"status": "invalid_method"}, status=405)

# 新增：接收前端视频跳转指令的接口
@csrf_exempt
def seek_video(request):
    global SEEK_COMMAND_FRAME
    if request.method == 'POST':
        try:
            body_unicode = request.body.decode('utf-8')
            data = json.loads(body_unicode) if body_unicode else {}
            target_frame = int(data.get('target_frame', 0))
            SEEK_COMMAND_FRAME = target_frame
            return JsonResponse({"status": "success", "code": 200, "message": f"定位指令已登记至帧 {target_frame}"})
        except Exception as e:
            return JsonResponse({"status": "error", "message": str(e)}, status=400)
    return JsonResponse({"status": "invalid_method"}, status=405)

# 新增：接收前端暂停/播放指令的接口
@csrf_exempt
def pause_engine(request):
    global IS_PAUSED
    if request.method == 'POST':
        try:
            body_unicode = request.body.decode('utf-8')
            data = json.loads(body_unicode) if body_unicode else {}
            action = data.get('action', 'toggle')
            
            if action == 'pause':
                IS_PAUSED = True
            elif action == 'play':
                IS_PAUSED = False
            else:
                IS_PAUSED = not IS_PAUSED
                
            return JsonResponse({"status": "success", "is_paused": IS_PAUSED})
        except Exception as e:
            return JsonResponse({"status": "error", "message": str(e)}, status=400)
    return JsonResponse({"status": "invalid_method"}, status=405)

# 新增：接收前端停止指令的接口
@csrf_exempt
def stop_engine(request):
    global LATEST_FRAME, SEEK_COMMAND_FRAME, IS_PAUSED, LAST_PERCEPTION_DATA
    if request.method == 'POST':
        try:
            # 强行杀掉后台主算法进程
            os.system("taskkill /f /im main_video.exe >nul 2>&1")
            
            # 清理重置所有后端的状态参数，防止下一次启动时数据错乱
            LATEST_FRAME = None
            SEEK_COMMAND_FRAME = -1
            IS_PAUSED = False
            LAST_PERCEPTION_DATA = {
                "frame_id": 0,
                "total_frames": 0,
                "objects": []
            }
            return JsonResponse({"status": "success", "message": "已成功强制终止感知节点进程并重置状态缓存！"})
        except Exception as e:
            return JsonResponse({"status": "error", "message": str(e)}, status=400)
    return JsonResponse({"status": "invalid_method"}, status=405)

# 新增：给前端页面提供最新数据的接口
def get_latest_data(request):
    global LAST_PERCEPTION_DATA, IS_PAUSED
    # 无论是现在读内存，还是以后读数据库，前端看到的都是这个 JSON，同时附带当前的暂停状态
    response_data = dict(LAST_PERCEPTION_DATA)
    response_data['is_paused'] = IS_PAUSED
    return JsonResponse(response_data)

# 新增：渲染前端可视化大屏主页的视图
def monitor_panel(request):
    return render(request, 'monitor.html')

@csrf_exempt
def start_engine(request):
    if request.method == 'POST':
        try:
            # 1. 拦截参数与上传的视频文件
            content_type = request.content_type or ''
            if content_type.startswith('multipart/form-data'):
                selected_model = request.POST.get('model_name', 'yolov8n.onnx')
                video_file = request.FILES.get('video_file')
            else:
                body_unicode = request.body.decode('utf-8')
                post_data = json.loads(body_unicode) if body_unicode else {}
                selected_model = post_data.get('model_name', 'yolov8n.onnx')
                video_file = None
            
            # 2. 保存自定义视频文件，如果未上传则恢复为默认测试视频
            if video_file:
                upload_dir = r"D:\AutoDrive_System\AutoDrive_Framework\data\uploads"
                os.makedirs(upload_dir, exist_ok=True)
                save_path = os.path.join(upload_dir, "user_uploaded.mp4")
                with open(save_path, 'wb+') as destination:
                    for chunk in video_file.chunks():
                        destination.write(chunk)
                video_path = save_path.replace("\\", "/")
            else:
                video_path = "D:/AutoDrive_System/AutoDrive_Framework/data/test_video.mp4"
            
            # 3. 覆写 YAML 配置
            yaml_path = r"D:\AutoDrive_System\AutoDrive_Framework\config.yaml"
            with open(yaml_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            
            # 写入模型路径和视频源
            config['perception']['model_path'] = f"D:/AutoDrive_System/AutoDrive_Framework/models/{selected_model}"
            config['system']['video_source'] = video_path
            
            # 根据模型静态维度自动更新 YAML 中的尺寸配置
            if 'yolov8n' in selected_model:
                config['perception']['input_size'] = [320, 320]
            else:
                config['perception']['input_size'] = [640, 640]
                
            with open(yaml_path, 'w', encoding='utf-8') as f:
                yaml.dump(config, f, allow_unicode=True, sort_keys=False)

            # 🎯 清理上一轮残留的视频帧与定位指令
            global LATEST_FRAME, SEEK_COMMAND_FRAME
            LATEST_FRAME = None
            SEEK_COMMAND_FRAME = -1

            # 4. 准备路径 (请确保这是你刚才试过绝对正确的路径)
            exe_path = r"D:\AutoDrive_System\build\Debug\main_video.exe" # 根据你的实际情况
            work_dir = r"D:\AutoDrive_System\AutoDrive_Framework"

            # 🎯 新增核心逻辑：点火前，先在 Windows 系统层强制清理同名的僵尸/旧进程
            # 这相当于实现了“覆盖式”热切换
            os.system("taskkill /f /im main_video.exe >nul 2>&1")

            # 5. 重新点火！
            subprocess.Popen([exe_path], cwd=work_dir)
            return JsonResponse({"status": "success", "message": f"已成功切换为 {selected_model} 并载入视频！"})
            
        except Exception as e:
            return JsonResponse({"status": "error", "message": f"启动失败: {str(e)}"})
            
    return JsonResponse({"status": "invalid_method"}, status=405)