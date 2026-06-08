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
    path('monitor/', views.monitor_panel, name='monitor_panel'),
]

