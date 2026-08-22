cmake_minimum_required(VERSION 3.20)
# 版本号单一来源（用户 179 轮：不硬编码）——更新版本只需改这里的 VERSION（major.minor.patch）
project(vulkan-blank VERSION 1.242.15 LANGUAGES CXX)

# 纯 Vulkan API 应用（Vulkan 1.0+ 双渲染路径 / C++20 / GPU 圆角矩形绘制）
# 仅依赖 Vulkan 头文件/库 + 构建期 glslc，无任何第三方库

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan REQUIRED)
if(NOT Vulkan_GLC_EXECUTABLE)
    find_program(Vulkan_GLC_EXECUTABLE glslc)
endif()
if(NOT Vulkan_GLC_EXECUTABLE)
    set(Vulkan_GLC_EXECUTABLE
        "C:/Users/Administrator/WorkBuddy/2026-08-16-23-13-11/msys64/mingw64/bin/glslc.exe")
endif()

# 构建期工具：SPIR-V -> C 数组（嵌入 exe，分发自包含）
find_program(PYTHON3_EXECUTABLE python3 python)
if(NOT PYTHON3_EXECUTABLE)
    set(PYTHON3_EXECUTABLE
        "C:/Users/Administrator/.workbuddy/binaries/python/versions/3.13.12/python.exe")
endif()

# ---- 着色器：glslc 编译 SPIR-V → 转 C 头文件（构建期自动生成） ----
set(SHADER_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)
set(SHADER_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/shaders)
file(MAKE_DIRECTORY ${SHADER_GEN_DIR})

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/rounded_rect.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/rounded_rect.vert
            -o ${SHADER_GEN_DIR}/rounded_rect.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/rounded_rect.vert.spv
            ${SHADER_GEN_DIR}/rounded_rect.vert.inc kRoundedRectVertSpv
    DEPENDS ${SHADER_SRC_DIR}/rounded_rect.vert
    COMMENT "编译顶点着色器 (rounded_rect.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/rounded_rect.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/rounded_rect.frag
            -o ${SHADER_GEN_DIR}/rounded_rect.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/rounded_rect.frag.spv
            ${SHADER_GEN_DIR}/rounded_rect.frag.inc kRoundedRectFragSpv
    DEPENDS ${SHADER_SRC_DIR}/rounded_rect.frag
    COMMENT "编译片元着色器 (rounded_rect.frag)")

# 统一 3D 渲染（线框 LINE_LIST + 实体 TRIANGLE_LIST 共用同一 shader，mode 切换）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/unified3d.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/unified3d.vert
            -o ${SHADER_GEN_DIR}/unified3d.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/unified3d.vert.spv
            ${SHADER_GEN_DIR}/unified3d.vert.inc kUnified3dVertSpv
    DEPENDS ${SHADER_SRC_DIR}/unified3d.vert
    COMMENT "编译统一 3D 顶点着色器 (unified3d.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/unified3d.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/unified3d.frag
            -o ${SHADER_GEN_DIR}/unified3d.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/unified3d.frag.spv
            ${SHADER_GEN_DIR}/unified3d.frag.inc kUnified3dFragSpv
    DEPENDS ${SHADER_SRC_DIR}/unified3d.frag
    COMMENT "编译统一 3D 片元着色器 (unified3d.frag)")

# 3D 线框（选中物体高亮/线框预览，Round237）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/line3d.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/line3d.vert
            -o ${SHADER_GEN_DIR}/line3d.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/line3d.vert.spv
            ${SHADER_GEN_DIR}/line3d.vert.inc kLine3dVertSpv
    DEPENDS ${SHADER_SRC_DIR}/line3d.vert
    COMMENT "编译 3D 线框顶点着色器 (line3d.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/line3d.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/line3d.frag
            -o ${SHADER_GEN_DIR}/line3d.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/line3d.frag.spv
            ${SHADER_GEN_DIR}/line3d.frag.inc kLine3dFragSpv
    DEPENDS ${SHADER_SRC_DIR}/line3d.frag
    COMMENT "编译 3D 线框片元着色器 (line3d.frag)")

# 无限地面网格（GPU shader 射线求交 + LOD）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/grid.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/grid.vert
            -o ${SHADER_GEN_DIR}/grid.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/grid.vert.spv
            ${SHADER_GEN_DIR}/grid.vert.inc kGridVertSpv
    DEPENDS ${SHADER_SRC_DIR}/grid.vert
    COMMENT "编译网格顶点着色器 (grid.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/grid.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/grid.frag
            -o ${SHADER_GEN_DIR}/grid.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/grid.frag.spv
            ${SHADER_GEN_DIR}/grid.frag.inc kGridFragSpv
    DEPENDS ${SHADER_SRC_DIR}/grid.frag
    COMMENT "编译网格片元着色器 (grid.frag)")

# 世界坐标轴（穿过原点的 XYZ 三色粗线，billboard 屏幕空间固定像素宽 + 深度测试）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/axis3d.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/axis3d.vert
            -o ${SHADER_GEN_DIR}/axis3d.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/axis3d.vert.spv
            ${SHADER_GEN_DIR}/axis3d.vert.inc kAxis3dVertSpv
    DEPENDS ${SHADER_SRC_DIR}/axis3d.vert
    COMMENT "编译世界坐标轴顶点着色器 (axis3d.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/axis3d.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/axis3d.frag
            -o ${SHADER_GEN_DIR}/axis3d.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/axis3d.frag.spv
            ${SHADER_GEN_DIR}/axis3d.frag.inc kAxis3dFragSpv
    DEPENDS ${SHADER_SRC_DIR}/axis3d.frag
    COMMENT "编译世界坐标轴片元着色器 (axis3d.frag)")

# 文字纹理（预光栅化中文字符，"设置"按钮等）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/text.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/text.vert
            -o ${SHADER_GEN_DIR}/text.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/text.vert.spv
            ${SHADER_GEN_DIR}/text.vert.inc kTextVertSpv
    DEPENDS ${SHADER_SRC_DIR}/text.vert
    COMMENT "编译文字顶点着色器 (text.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/text.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/text.frag
            -o ${SHADER_GEN_DIR}/text.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/text.frag.spv
            ${SHADER_GEN_DIR}/text.frag.inc kTextFragSpv
    DEPENDS ${SHADER_SRC_DIR}/text.frag
    COMMENT "编译文字片元着色器 (text.frag)")

# FXAA 后处理（全屏 quad + 边缘检测/模糊，只对 3D 中间纹理）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/fxaa.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/fxaa.vert
            -o ${SHADER_GEN_DIR}/fxaa.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/fxaa.vert.spv
            ${SHADER_GEN_DIR}/fxaa.vert.inc kFxaaVertSpv
    DEPENDS ${SHADER_SRC_DIR}/fxaa.vert
    COMMENT "编译 FXAA 顶点着色器 (fxaa.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/fxaa.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/fxaa.frag
            -o ${SHADER_GEN_DIR}/fxaa.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/fxaa.frag.spv
            ${SHADER_GEN_DIR}/fxaa.frag.inc kFxaaFragSpv
    DEPENDS ${SHADER_SRC_DIR}/fxaa.frag
    COMMENT "编译 FXAA 片元着色器 (fxaa.frag)")

# Round370：选中物体屏幕投影描边（mask + Sobel 后处理）
add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/outline.vert.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/outline.vert
            -o ${SHADER_GEN_DIR}/outline.vert.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/outline.vert.spv
            ${SHADER_GEN_DIR}/outline.vert.inc kOutlineVertSpv
    DEPENDS ${SHADER_SRC_DIR}/outline.vert
    COMMENT "编译选中描边顶点着色器 (outline.vert)")

add_custom_command(
    OUTPUT ${SHADER_GEN_DIR}/outline.frag.inc
    COMMAND ${Vulkan_GLC_EXECUTABLE} ${SHADER_SRC_DIR}/outline.frag
            -o ${SHADER_GEN_DIR}/outline.frag.spv
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/spv_to_inc.py
            ${SHADER_GEN_DIR}/outline.frag.spv
            ${SHADER_GEN_DIR}/outline.frag.inc kOutlineFragSpv
    DEPENDS ${SHADER_SRC_DIR}/outline.frag
    COMMENT "编译选中描边片元着色器 (outline.frag)")

# 警告 + 字符集（UTF-8 源文件 → 宽字面量正确转 UTF-16，否则 MinGW 按系统 ANSI 解释
#   → L"示例.obj" 变乱码 → CreateFileW 找不到中文文件名 → 模型不加载）
# ---- 版本号生成（用户 179 轮：单一来源 = 上方 project() 的 VERSION）----
# 由 tools/gen_version.py 生成 version_gen.h（C++ 宏）与 version.rc（VERSIONINFO 资源）
set(VKB_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")           # "1.178"
set(VKB_RC_VERSION "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")  # "1,178,0,0"
set(VERSION_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${VERSION_GEN_DIR})
add_custom_command(
    OUTPUT ${VERSION_GEN_DIR}/version_gen.h ${VERSION_GEN_DIR}/version.rc
    COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_version.py
            ${VKB_VERSION} ${VKB_RC_VERSION}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in ${VERSION_GEN_DIR}/version_gen.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in ${VERSION_GEN_DIR}/version.rc
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in
    COMMENT "生成版本头/版本资源")

# 必须在 add_executable 之前声明，CMake 才会在生成命令中包含这些选项
# -fshort-wchar：强制 wchar_t=2 字节（与 Win32 UTF-16 一致）。MinGW g++ 默认 4 字节，
#   会导致 GetModuleFileNameW/CreateFileW 等 Win32 API 写入时字节布局错位，路径被截断
if(MSVC)
    add_compile_options(/W4 /utf-8)
else()
    add_compile_options(-Wall -Wextra -finput-charset=UTF-8 -fexec-charset=UTF-8 -fshort-wchar)
endif()

# 实体模型（无材质，Blender solid 光照）——已并入 unified3d（mode=1），此规则删除
# （unified3d 同时覆盖线框 LINE_LIST 与实体 TRIANGLE_LIST）

add_executable(vulkan-blank WIN32
    src/main.cpp
    src/vulkan_loader.cpp
    src/settings_window.cpp
    src/camera.cpp
    src/model_import.cpp
    src/import_pipeline.cpp
    src/import_window.cpp
    src/ui_button.cpp
    src/gdi_util.cpp
    src/mc_blocks.cpp
    src/mc_map_pipeline.cpp
    assets/icon.rc
    src/ball_icons.rc
    ${VERSION_GEN_DIR}/version.rc
    ${VERSION_GEN_DIR}/version_gen.h
    ${SHADER_GEN_DIR}/rounded_rect.vert.inc
    ${SHADER_GEN_DIR}/rounded_rect.frag.inc
    ${SHADER_GEN_DIR}/unified3d.vert.inc
    ${SHADER_GEN_DIR}/unified3d.frag.inc
    ${SHADER_GEN_DIR}/grid.vert.inc
    ${SHADER_GEN_DIR}/grid.frag.inc
    ${SHADER_GEN_DIR}/axis3d.vert.inc
    ${SHADER_GEN_DIR}/axis3d.frag.inc
    ${SHADER_GEN_DIR}/line3d.vert.inc
    ${SHADER_GEN_DIR}/line3d.frag.inc
    ${SHADER_GEN_DIR}/text.vert.inc
    ${SHADER_GEN_DIR}/text.frag.inc
    ${SHADER_GEN_DIR}/fxaa.vert.inc
    ${SHADER_GEN_DIR}/fxaa.frag.inc
    ${SHADER_GEN_DIR}/outline.vert.inc   # Round370：选中描边
    ${SHADER_GEN_DIR}/outline.frag.inc
    src/gear_png.inc)
target_link_libraries(vulkan-blank PRIVATE windowscodecs comdlg32 ole32 shell32)
target_include_directories(vulkan-blank PRIVATE ${SHADER_GEN_DIR} ${VERSION_GEN_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src ${Vulkan_INCLUDE_DIR})

# icon.ico 变更时强制重新编译 icon.rc（否则只替换 icon.ico 不会触发重编，exe 仍嵌旧图标）
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.rc PROPERTIES
    OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon.ico)
# 球按钮/变换按钮图标 PNG（源在 资源/ 中文路径，windres 不支持 → 复制到 build/ball_icons/ 英文路径）
# 注意：改图标 PNG 后需重新跑 cmake（file(COPY) 只在 configure 阶段执行一次）
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/资源/1.png
     ${CMAKE_CURRENT_SOURCE_DIR}/资源/2.png
     ${CMAKE_CURRENT_SOURCE_DIR}/资源/3.png
     ${CMAKE_CURRENT_SOURCE_DIR}/资源/4.png
     ${CMAKE_CURRENT_SOURCE_DIR}/资源/5.png
     ${CMAKE_CURRENT_SOURCE_DIR}/资源/6.png
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR}/ball_icons/)

# ---- 版本命名：优化版（Release 分发）/ 未优化版（Debug 开发） ----
# 单配置生成器（MinGW Makefiles）下按 CMAKE_BUILD_TYPE 区分。
# 说明：MinGW make 链路无法可靠生成中文文件名（实测中文会变乱码），
# 故采用 optimized / unoptimized 英文标识，语义与「优化 / 未优化」一致。
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set_target_properties(vulkan-blank PROPERTIES OUTPUT_NAME "vulkan-blank-optimized")
else()
    set_target_properties(vulkan-blank PROPERTIES OUTPUT_NAME "vulkan-blank-unoptimized")
endif()

# Debug 构建：2026-08-17 临时禁用 Khronos 验证层（验证层与 MSAA 4x
# dynamic rendering resolve 有兼容问题导致 Debug 启动栈溢出 0xC00000FD）。
# Release 构建本来就不启用验证层。
# 如需恢复验证层调试：手动开启下方 $<$<CONFIG:Debug>:VKB_ENABLE_VALIDATION>
target_compile_definitions(vulkan-blank PRIVATE)
# $<$<CONFIG:Debug>:VKB_ENABLE_VALIDATION>  # 暂时禁用（MSAA 4x 兼容问题）

# ---- 运行时依赖 DLL ----
# vulkan-1.dll 由代码 LoadLibrary 从「资源」文件夹加载（不再内嵌、不再静态链接）；
# C++ 运行时（libgcc/libstdc++/winpthread）静态链接，不再依赖外部 DLL。
# 故 VKB_NEEDED_DLLS 为空（CheckRuntimeDlls 保留接口但不检测外部 DLL）。
target_compile_definitions(vulkan-blank PRIVATE VKB_NEEDED_DLLS="")

# Release：-O2 + 完全静态 + 剥离符号（体积小，仅依赖「资源/vulkan-1.dll」）
# Debug：完全静态（-static，静态链接 libgcc/libstdc++/winpthread），不再缺这些 DLL
# - 不用 -Os：MinGW 下会抑制 std::string 内联，产生未导出 inline 符号导致链接失败
target_compile_options(vulkan-blank PRIVATE $<$<CONFIG:Release>:-O2>)
target_link_options(vulkan-blank PRIVATE -static $<$<CONFIG:Release>:-s>)

# ---- 收集运行时依赖到「资源」文件夹 ----
get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)

# vulkan-1.dll → 构建目录「资源」文件夹（代码 LoadLibrary 从该文件夹加载）
# 用 file(COPY)（configure 阶段 CMake 直接执行）而非 POST_BUILD：MinGW make 会把
# 中文「资源」转乱码（GBK 误读 UTF-8），导致文件夹名变成乱码
file(COPY "${MINGW_BIN_DIR}/vulkan-1.dll" DESTINATION "${CMAKE_BINARY_DIR}/资源")

# 按钮动画主题配置文件（明文，可手动编辑）拷贝到 exe 旁
add_custom_command(TARGET vulkan-blank POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/button_theme.txt" "$<TARGET_FILE_DIR:vulkan-blank>/button_theme.txt")
