from django.shortcuts import render
from django.http import JsonResponse
from django.views.decorators.csrf import csrf_exempt
import json
import subprocess
import os

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
    # 你的 C++ exe 的绝对路径 (请确认你电脑上的实际路径是否与此一致)
    # 因为刚才移动了文件夹，路径应该是 D:\AutoDrive_System\AutoDrive_Framework\...
    exe_path = r"D:\AutoDrive_System\build\Debug\main_video.exe" 
    
    # C++ 程序的工作目录（让它能正确找到 config.yaml 和模型）
    work_dir = r"D:\AutoDrive_System\AutoDrive_Framework"

    try:
        # Popen 是非阻塞的，它会把 C++ 扔到后台运行，Django 继续处理网页请求
        subprocess.Popen([exe_path], cwd=work_dir)
        return JsonResponse({"status": "success", "message": "底盘/感知引擎已成功点火！"})
    except Exception as e:
        return JsonResponse({"status": "error", "message": str(e)})