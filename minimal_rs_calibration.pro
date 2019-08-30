TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

SOURCES += \
        main.cpp

INCLUDEPATH += /home/gekko/librealsense/include
INCLUDEPATH += /home/gekko/librscalibrationapi/usr/include

DEFINES += WITH_GUI
