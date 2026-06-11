"""
URL configuration for traffic_cloud project.

The `urlpatterns` list routes URLs to views. For more information please see:
    https://docs.djangoproject.com/en/6.0/topics/http/urls/
Examples:
Function views
    1. Add an import:  from my_app import views
    2. Add a URL to urlpatterns:  path('', views.home, name='home')
Class-based views
    1. Add an import:  from other_app.views import Home
    2. Add a URL to urlpatterns:  path('', Home.as_view(), name='home')
Including another URLconf
    1. Import the include() function: from django.urls import include, path
    2. Add a URL to urlpatterns:  path('blog/', include('blog.urls'))
"""
from django.contrib import admin
from django.urls import path
from perception import views

urlpatterns = [
    path('admin/', admin.site.urls),
    # 1. C++ 往这里扔数据
    path('api/perception/upload/', views.receive_data, name='receive_data'),
    # 2. 前端往这里要数据
    path('api/perception/latest/', views.get_latest_data, name='get_latest_data'),
    # 3. 浏览器访问的可视化大屏主页
    path('api/control/start/', views.start_engine, name='start_engine'),
    
    # 新增：C++ 上传图像帧接口与前端视频流分发接口
    path('api/perception/upload_frame/', views.upload_frame, name='upload_frame'),
    path('api/perception/video_feed/', views.video_feed, name='video_feed'),
    
    # 新增：前端下发跳转进度指令接口
    path('api/perception/seek/', views.seek_video, name='seek_video'),
    
    # 新增：暂停与停止感知引擎接口
    path('api/control/pause/', views.pause_engine, name='pause_engine'),
    path('api/control/stop/', views.stop_engine, name='stop_engine'),
    
    path('monitor/', views.monitor_panel, name='monitor_panel'),
]

