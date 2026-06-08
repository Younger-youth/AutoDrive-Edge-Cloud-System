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
            # 1. 拦截前端传过来的 JSON 数据，提取模型名字
            body_unicode = request.body.decode('utf-8')
            post_data = json.loads(body_unicode) if body_unicode else {}
            # 如果没传，默认用 yolov8n.onnx
            selected_model = post_data.get('model_name', 'yolov8n.onnx') 
            
            # 2. 动态修改底层 C++ 的 config.yaml 图纸
            yaml_path = r"D:\AutoDrive_System\AutoDrive_Framework\config.yaml"
            
            # 读取原有配置
            with open(yaml_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            
            # 拼接新的模型绝对路径并覆写
            new_model_path = f"D:/AutoDrive_System/AutoDrive_Framework/models/{selected_model}"
            config['perception']['model_path'] = new_model_path
            
            # 将新配置写回文件
            with open(yaml_path, 'w', encoding='utf-8') as f:
                yaml.dump(config, f, allow_unicode=True, sort_keys=False)

            # 3. 准备启动 C++ 程序 (路径保持你刚才核对过正确的即可)
            exe_path = r"D:\AutoDrive_System\build\Debug\main_video.exe" 
            work_dir = r"D:\AutoDrive_System\AutoDrive_Framework"

            # 4. 点火！
            subprocess.Popen([exe_path], cwd=work_dir)
            return JsonResponse({"status": "success", "message": f"成功挂载 {selected_model} 并点火！"})
            
        except Exception as e:
            return JsonResponse({"status": "error", "message": f"启动失败: {str(e)}"})
            
    return JsonResponse({"status": "invalid_method"}, status=405)