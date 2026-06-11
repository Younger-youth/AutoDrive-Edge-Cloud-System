from django.shortcuts import render
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
import json
import subprocess
import os
import yaml


# 新增：定义一个全局变量，用来在内存中暂存 C++ 传来的最新一帧数据
LAST_PERCEPTION_DATA = {
    "frame_id": 0,
    "objects": []
}

@csrf_exempt
def receive_data(request):
    global LAST_PERCEPTION_DATA
    if request.method == 'POST':
        try:
            body_unicode = request.body.decode('utf-8')
            data = json.loads(body_unicode)
            
            # 更新全局变量
            LAST_PERCEPTION_DATA = data
            
            return JsonResponse({"status": "success", "code": 200})
        except Exception as e:
            return JsonResponse({"status": "error", "message": str(e)}, status=400)
            
    return JsonResponse({"status": "invalid_method"}, status=405)

# 新增：给前端页面提供最新数据的接口
def get_latest_data(request):
    global LAST_PERCEPTION_DATA
    # 无论是现在读内存，还是以后读数据库，前端看到的都是这个 JSON
    return JsonResponse(LAST_PERCEPTION_DATA)

# 新增：渲染前端可视化大屏主页的视图
def monitor_panel(request):
    return render(request, 'monitor.html')

@csrf_exempt
def start_engine(request):
    if request.method == 'POST':
        try:
            # 1. 拦截模型名字
            body_unicode = request.body.decode('utf-8')
            post_data = json.loads(body_unicode) if body_unicode else {}
            selected_model = post_data.get('model_name', 'yolov8n.onnx') 
            
            # 2. 覆写 YAML 配置
            yaml_path = r"D:\AutoDrive_System\AutoDrive_Framework\config.yaml"
            with open(yaml_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            config['perception']['model_path'] = f"D:/AutoDrive_System/AutoDrive_Framework/models/{selected_model}"
            with open(yaml_path, 'w', encoding='utf-8') as f:
                yaml.dump(config, f, allow_unicode=True, sort_keys=False)

            # 3. 准备路径 (请确保这是你刚才试过绝对正确的路径)
            exe_path = r"D:\AutoDrive_System\build\Debug\main_video.exe" # 根据你的实际情况
            work_dir = r"D:\AutoDrive_System\AutoDrive_Framework"

            # 🎯 新增核心逻辑：点火前，先在 Windows 系统层强制清理同名的僵尸/旧进程
            # 这相当于实现了“覆盖式”热切换
            os.system("taskkill /f /im main_video.exe >nul 2>&1")

            # 4. 重新点火！
            subprocess.Popen([exe_path], cwd=work_dir)
            return JsonResponse({"status": "success", "message": f"已成功切换为 {selected_model}"})
            
        except Exception as e:
            return JsonResponse({"status": "error", "message": f"启动失败: {str(e)}"})
            
    return JsonResponse({"status": "invalid_method"}, status=405)