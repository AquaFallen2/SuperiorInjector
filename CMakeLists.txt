cmake_minimum_required(VERSION 3.16)
project(InjectorGUI CXX)

set(CMAKE_CXX_STANDARD 17)

# Находим или подключаем GLFW
find_package(glfw3 REQUIRED)
find_package(OpenGL REQUIRED)

# Исходники ImGui
set(IMGUI_DIR thirdparty/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(InjectorGUI src/main.cpp ${IMGUI_SOURCES})

target_include_directories(InjectorGUI PRIVATE 
    ${IMGUI_DIR} 
    ${IMGUI_DIR}/backends
)

target_link_libraries(InjectorGUI PRIVATE glfw OpenGL::GL)