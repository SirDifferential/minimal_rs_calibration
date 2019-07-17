# minimal_rs_calibration

This code can be used to debug the Intel Realsense D4XX target-less calibration as specified by the librscalibration api: https://www.intel.com/content/dam/support/us/en/documents/emerging-technologies/intel-realsense-technology/RealSense_D400_Dyn_Calib_Programmer.pdf

To build, do something comparable to `g++ main.cpp -I/home/gekko/librealsense/include -I/home/gekko/librscalibrationapi/include -L/home/gekko/librealsense/build -lrealsense2 -L/home/gekko/librscalibrationapi/lib -lDSDynamicCalibrationAPI`

