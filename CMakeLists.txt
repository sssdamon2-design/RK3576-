cmake_minimum_required(VERSION 3.10)    #要求的CMake版本，必须写

project(vehicle_terminal LANGUAGES CXX)   #定义项目名称，要求语言C++

option(ENABLE_DHT11 "Enable dht11 support" OFF)  #定义一个开关， 名：ENABLE_DHT11  值：off   说明："Enable dht11 support"

set(CMAKE_CXX_STANDARD 11)              #set(变量名 值)  CMAKE_CXX_STANDARD 11 表示设置C++ 11版本进行编译
set(CMAKE_CXX_STANDARD_REQUIRED ON)    #必须按照指定的 C++ 标准编译。

set(CMAKE_AUTOMOC ON)     #开启QT元对象
set(CMAKE_AUTOUIC ON)       #处理.ui对象
set(CMAKE_AUTORCC ON)       #处理.qrc对象

find_package(Qt5 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Multimedia
)
#在系统里寻找 Qt5 的 Core、Gui、Widgets 模块。 找到之后才可以使用target_link_libraries

add_executable(vehicle_terminal
    src/main.cpp

    src/mainwindow.cpp
    src/mainwindow.h

    src/pages/homepage.cpp
    src/pages/homepage.h

    src/pages/monitorpage.cpp
    src/pages/monitorpage.h

    src/pages/musicpage.cpp
    src/pages/musicpage.h

    src/services/systeminfoservice.cpp
    src/services/systeminfoservice.h

    src/services/audioplayerservice.cpp
    src/services/audioplayerservice.h

    src/pages/settingspage.cpp
    src/pages/settingspage.h
)
#add_executable 表示把这些文件编译并链接为一个可执行程序


if(ENABLE_DHT11)

    target_sources(
        vehicle_terminal 
        PRIVATE
        src/services/dht11service.cpp
        src/services/dht11service.h
        )   
# target_sources是在已经有add_executable的定义下，向链接的文件继续添加文件使用的，第二项必须为private
    target_compile_definitions(vehicle_terminal PRIVATE ENABLE_DHT11)  
    #target_compile_definitions相当于给代码写宏命令  #define ENABLE_DHT11  这样可以写#ifdef ENABLE_DHT11。。。
endif()

target_include_directories(vehicle_terminal
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# target_include_directories 告诉编译器去哪里找头文件  ${}表示引用变量   CMAKE_CURRENT_SOURCE_DIR表示cmake文件当前路径
#即设置头文件搜索路径 告诉 C++ 编译器：当代码写 #include "xxx.h" 的时候，可以去哪些文件夹查找头文件。



target_link_libraries(vehicle_terminal
    PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Multimedia
)
#find_package找到各个库之后，使用target_link_libraries把各个库和主文件链接
#链接阶段，给可执行文件 / 库，绑定依赖库    也就是说编译的时候只看声明，链接的时候需要实现了，这里写的就是实现用的代码放在哪里的