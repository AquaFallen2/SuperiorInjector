@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

cd /d "%~dp0"

cl.exe /std:c++17 /EHsc /W3 /I imgui/imgui /I imgui src/main.cpp imgui/imgui/imgui.cpp imgui/imgui/imgui_draw.cpp imgui/imgui/imgui_tables.cpp imgui/imgui/imgui_widgets.cpp imgui/imgui/imgui_impl_win32.cpp imgui/imgui/imgui_impl_dx11.cpp /Fe:Injector.exe d3d11.lib d3dcompiler.lib user32.lib gdi32.lib ole32.lib wininet.lib crypt32.lib advapi32.lib comdlg32.lib gdiplus.lib