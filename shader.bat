@echo off
setlocal
set VULKAN_VERSION=1.4.313.0
set VULKAN_Path=C:\VulkanSDK\%VULKAN_VERSION%\Bin\glslangValidator.exe
echo path "%VULKAN_Path%"

%VULKAN_Path% -V --vn triangle_vert shaders/triangle.vert -o include/triangle_vert.h
%VULKAN_Path% -V --vn triangle_frag shaders/triangle.frag -o include/triangle_frag.h

endlocal