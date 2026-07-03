cmake_minimum_required(VERSION 3.16)
project(convnet_qt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# MSVC 下源码是 UTF-8（含中文字面量），必须显式告知，否则乱码/报错
if(MSVC)
    add_compile_options(/utf-8)
endif()

# Qt6 优先。若本机仅有 Qt5，可将下行改为 find_package(Qt5 ...) 并把 Qt6:: 换成 Qt5::。
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# ---- libdatachannel（C++ WebRTC，P2P 数据通道 + STUN）----
# 优先用系统已安装的（apt install libdatachannel-dev / 或自行 make install）；
# 找不到则用 FetchContent 从源码构建（首次编译会较慢，需要 CMake/OpenSSL 等构建依赖）。
find_package(LibDataChannel QUIET)
if(NOT LibDataChannel_FOUND)
    message(STATUS "LibDataChannel 未安装，改用 FetchContent 从源码构建（首次较慢）。"
                   "如需加速：sudo apt install libdatachannel-dev 后重新 cmake。")
    include(FetchContent)
    # 只用 DataChannel，关掉音视频(SRTP)/WebSocket/示例/测试，显著减少编译量
    set(NO_EXAMPLES  ON CACHE BOOL "" FORCE)
    set(NO_TESTS     ON CACHE BOOL "" FORCE)
    set(NO_MEDIA     ON CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    # CMake 4.x 移除了 <3.5 兼容，libdatachannel 子模块 plog 最低版本号过老，放宽之
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
    FetchContent_Declare(libdatachannel
        GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
        GIT_TAG        v0.22.4
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(libdatachannel)
endif()

add_executable(convnet-qt
    src/main.cpp

    src/core/Protocol.h
    src/core/FrameCodec.h
    src/core/FrameCodec.cpp
    src/core/SignalingClient.h
    src/core/SignalingClient.cpp

    src/p2p/PeerLink.h
    src/p2p/PeerLink.cpp
    src/p2p/P2PManager.h
    src/p2p/P2PManager.cpp
    src/p2p/RelayTransport.h
    src/p2p/RelayTransport.cpp

    src/net/ITapDevice.h
    src/net/Router.h
    src/net/Router.cpp
    src/net/TapManager.h
    src/net/TapManager.cpp
    src/net/Firewall.h
    src/net/Firewall.cpp

    src/model/Types.h
    src/model/Identity.h
    src/model/Identity.cpp
    src/model/AppModel.h
    src/model/AppModel.cpp

    src/ui/LoginDialog.h
    src/ui/LoginDialog.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/ui/ChatWindow.h
    src/ui/ChatWindow.cpp
    src/ui/AddFriendDialog.h
    src/ui/AddFriendDialog.cpp
    src/ui/GroupDialog.h
    src/ui/GroupDialog.cpp
    src/ui/UserInfoDialog.h
    src/ui/Theme.h
    src/ui/FirewallDialog.h
    src/ui/FirewallDialog.cpp
)

# 平台相关的网卡后端：
#   Windows：Wintun(TUN) + tap-windows6(TAP)
#   Linux：  /dev/net/tun（IFF_TUN/IFF_TAP）
if(WIN32)
    target_sources(convnet-qt PRIVATE
        src/net/WinTun.h src/net/WinTun.cpp
        src/net/WinTap.h src/net/WinTap.cpp)
else()
    target_sources(convnet-qt PRIVATE src/net/LinuxTun.h src/net/LinuxTun.cpp)
endif()

target_include_directories(convnet-qt PRIVATE src)
target_link_libraries(convnet-qt PRIVATE Qt6::Widgets Qt6::Network LibDataChannel::LibDataChannel)
