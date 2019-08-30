# minimal_rs_calibration

This code can be used to debug the Intel Realsense D4XX target-less calibration as specified by the librscalibration api: https://www.intel.com/content/dam/support/us/en/documents/emerging-technologies/intel-realsense-technology/RealSense_D400_Dyn_Calib_Programmer.pdf

### Building

* Edit `Makefile` and `run.sh`
* Make sure you are using only one version of librealsense - currently (2019.08.30) librscalibrationapi bundles its own version of librealsense which might conflict with your locally available copy
* `make`
* `sh run.sh`
