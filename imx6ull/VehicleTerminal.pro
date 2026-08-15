QT += core gui widgets\
    multimedia

CONFIG += c++11
CONFIG -= app_bundle

TEMPLATE = app
TARGET = vehicle_terminal

INCLUDEPATH += \
    src \
    src/pages \
    include

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/pages/homepage.cpp \
    src/pages/monitorpage.cpp \
    src/pages/settingspage.cpp \
    src/services/systeminfoservice.cpp\
    src/services/dht11service.cpp \
    src/services/audioplayerservice.cpp\
    src/pages/musicpage.cpp
HEADERS += \
    src/mainwindow.h \
    src/pages/homepage.h \
    src/pages/monitorpage.h \
    src/pages/settingspage.h \
    src/services/systeminfoservice.h\
    src/services/dht11service.h\
    src/services/audioplayerservice.h\
    src/pages/musicpage.h