"""
URL configuration for mcpproject project.
"""
from django.contrib import admin
from django.urls import path, include

urlpatterns = [
    path('admin/', admin.site.urls),
    path('mcp_server/', include('mcp_server.urls')),
]
