#!/bin/sh

g++ main.cpp -I/home/gekko/librealsense/include -I/home/gekko/librscalibrationapi/include -L/home/gekko/librealsense/build -lrealsense2 -L/home/gekko/librscalibrationapi/lib -lDSDynamicCalibrationAPI
