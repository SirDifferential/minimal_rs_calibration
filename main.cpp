/**
 * Minimal C++ program that utilizes Realsense D400 devices and streams their
 * depth and color streams. This program also enables a visual preset mode which
 * can be useful to make sure the installed librealsense SDK, kernel and libraries
 * work properly as the advanced mode seems to be a feature that fails to work
 * properly on some systems.
 *
 * To keep the code smaller and capable of running on minimal systems, no GUI
 * is used at all.
 */

#include <iostream>
#include <string.h>
#include <stdint.h>
#include <chrono>
#include <list>

#ifdef WIN32
#include <Windows.h>
#else
#include <csignal>
#endif

#include <librealsense2/rs.hpp>
#include <librealsense2/rs_advanced_mode.hpp>

#include <librscalibrationapi/DSCalData.h>
#include <librscalibrationapi/DSDynamicCalibration.h>
#include <librscalibrationapi/DSOSUtils.h>
#include <librscalibrationapi/DSShared.h>
#include <librscalibrationapi/rs2-custom-calibration-mm.h>

#define PRESET_COUNT 3
const char* presets[PRESET_COUNT] = {
    "High Accuracy",
    "High Density",
    "Hand"
};

bool got_sigint = false;
const int color_w = 1920;
const int color_h = 1080;
const int depth_w = 1280;
const int depth_h = 720;

/**
 * Wrapper to call delete or delete[] on dtor
 */
template <class T>
class DeleteLater {
public:
    DeleteLater(T* p, bool is_arr, std::string n) {
        m_p = p;
        m_arr = is_arr;
        m_name = n;
    }

    virtual ~DeleteLater() {
        if (!m_p)
            return;

        std::cout << "freeing memory: " << m_name << std::endl;

        if (m_arr) {
            delete[] m_p;
        } else {
            delete m_p;
        }
    }

    T* m_p;
    bool m_arr;
    std::string m_name;
};

void stop(rs2::pipeline& p) {
    p.stop();
}

#ifdef WIN32
bool sigint_handler(DWORD fdwCtrlType) {
    if(fdwCtrlType == CTRL_C_EVENT) {
        std::cout << "signal caught: " << fdwCtrlType<< std::endl;
        got_sigint = true;
        return true;
    }

    return false;
}
#else
void sigint_handler(int sig)
{
    std::cout << "signal caught: " << sig << std::endl;
    got_sigint = true;
}
#endif

int main() try {

    // register signal handlers
#ifdef WIN32
    SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sigint_handler, TRUE );
#else
    signal(SIGINT, sigint_handler);
#endif

    const char* desired_preset = presets[0];
    size_t preset_strlen = strnlen(desired_preset, 15);

    // allocate buffers for reading color and depth frames
    unsigned char* colorbuf = new unsigned char[color_w * color_h * 3];
    unsigned char* irbuf_l = new unsigned char[depth_w * depth_h];
    unsigned char* irbuf_r = new unsigned char[depth_w * depth_h];
    uint16_t* depthbuf = new uint16_t[depth_w * depth_h];

    if (depthbuf == NULL || colorbuf == NULL || irbuf_l == NULL || irbuf_r == NULL) {
        std::cout << "failed allocating memory" << std::endl;
        return 1;
    }

    DeleteLater<unsigned char> del1(colorbuf, true, "colorbuffer");
    DeleteLater<uint16_t> del2(depthbuf, true, "depthbuffer");
    DeleteLater<unsigned char> del3(irbuf_l, true, "irbuf_l");
    DeleteLater<unsigned char> del4(irbuf_r, true, "irbuf_r");

    memset(colorbuf, 0, color_w*color_h*3);
    memset(depthbuf, 0, depth_w*depth_h*sizeof(uint16_t));
    memset(irbuf_l, 0, depth_w*depth_h);
    memset(irbuf_r, 0, depth_w*depth_h);

    std::cout << "Allocated memory" << std::endl;

    rs2::context context;

    // Create a Pipeline - this serves as a top-level API for streaming and processing frames
    rs2::pipeline pipeline(context);
    std::shared_ptr<rs2_pipeline> p_pipeline = std::shared_ptr<rs2_pipeline>(pipeline);

    std::cout << "Created pipeline" << std::endl;

    rs2::device_list devs = context.query_devices();
    if (devs.size() != 1) {
        std::cout << "Expecting to find one device connected to the computer" << std::endl;
        return 1;
    }

    rs2::device dev = devs[0];
    const char* serial = dev.get_info(RS2_CAMERA_INFO_SERIAL_NUMBER);
    std::cout << "Using camera: " << serial << std::endl;

    std::vector<rs2::sensor> sensors = dev.query_sensors();
    std::cout << "Device has " << sensors.size() << " sensors" << std::endl;

    DynamicCalibrationAPI::DSDynamicCalibration dyncal;
    int retcode = dyncal.Initialize((void*)&dev,
        DynamicCalibrationAPI::DSDynamicCalibration::CAL_MODE_INTEL_TARGETLESS,
        depth_w, depth_h, true);

    switch (retcode) {
    case DC_SUCCESS:
        fprintf(stderr, "dyncal initialized successfully\n");
        break;
    case DC_ERROR_INVALID_PARAMETER:
        fprintf(stderr, "dyncal initialize DC_ERROR_INVALID_PARAMETER\n");
        return 1;
    case DC_ERROR_RESOLUTION_NOT_SUPPORTED_V2:
        fprintf(stderr, "dyncal initialize DC_ERROR_RESOLUTION_NOT_SUPPORTED_V2\n");
        return 1;
    case DC_ERROR_TABLE_NOT_SUPPORTED:
        fprintf(stderr, "dyncal initialize DC_ERROR_TABLE_NOT_SUPPORTED\n");
        return 1;
    case DC_ERROR_TABLE_NOT_VALID_RESOLUTION:
        fprintf(stderr, "dyncal initialize DC_ERROR_TABLE_NOT_VALID_RESOLUTION\n");
        return 1;
    }

    rs400::advanced_mode adv(dev);
    if (!adv.is_enabled()) {
        std::cout << "advanced mode is not enabled -> enabling it" << std::endl;

        adv.toggle_advanced_mode(true);
        std::cout << "Finished toggling advanced mode" << std::endl;
    } else {
        std::cout << "advanced mode is already enabled" << std::endl;
    }

    // Enable desired preset
    bool enabled_preset = false;
    std::cout << "Enabling preset " << desired_preset << std::endl;

    for (auto&& s : sensors) {
        if (s.supports(RS2_OPTION_VISUAL_PRESET)) {

            // See if the preset is already in use
            float pres = s.get_option(RS2_OPTION_VISUAL_PRESET);
            const char* cur_desc = s.get_option_value_description(RS2_OPTION_VISUAL_PRESET, pres);
            if (strncmp(cur_desc, desired_preset, preset_strlen) == 0) {
                std::cout << "already using desired preset" << std::endl;
                enabled_preset = true;
                break;
            }

            rs2::option_range r = s.get_option_range(RS2_OPTION_VISUAL_PRESET);

            // Go through all available presets and find the proper index to enable
            for (int i = (int)r.min; i < (int)r.max; ++i) {
                const char* desc = s.get_option_value_description(RS2_OPTION_VISUAL_PRESET, i);
                if (strncmp(desc, desired_preset, preset_strlen) == 0) {
                    s.set_option(RS2_OPTION_VISUAL_PRESET, i);
                    enabled_preset = true;
                    std::cout << "Enabled desired preset" << std::endl;
                    break;
                }
            }

            break;
        }
    }

    if (enabled_preset == false) {
        std::cout << "Did not find sensor that supports visual preset option" << std::endl;
        return 1;
    }

    // Enable max resolution streams
    rs2::config conf;

    conf.enable_device(serial);
    conf.enable_stream(RS2_STREAM_DEPTH, -1, depth_w, depth_h, RS2_FORMAT_Z16, 30);
    conf.enable_stream(RS2_STREAM_COLOR, -1, color_w, color_h, RS2_FORMAT_RGB8, 30);
    conf.enable_stream(RS2_STREAM_INFRARED, 1, depth_w, depth_h, RS2_FORMAT_Y8, 30);
    conf.enable_stream(RS2_STREAM_INFRARED, 2, depth_w, depth_h, RS2_FORMAT_Y8, 30);

    std::cout << "streams enabled" << std::endl;

    // Configure and start the pipeline
    rs2::pipeline_profile prof = pipeline.start(conf);
    std::cout << "pipeline started" << std::endl;

    float d_width, d_height;
    float c_width, c_height;
    uint64_t frames_got = 0;

    std::cout << "entering main loop" << std::endl;

    std::list<int> ftimes;
    int dur_sum = 0;

    while (true)
    {
        std::chrono::high_resolution_clock::time_point t1 = std::chrono::high_resolution_clock::now();

        if (got_sigint) {
            break;
        }
        // Block program until frames arrive
        rs2::frameset frames = pipeline.wait_for_frames(3000);

        bool got_depth = false;
        bool got_color = false;
        bool got_ir_left = false;
        bool got_ir_right = false;
        int ir_index = 0;

        // get specific frame instances
        for (auto&& f : frames) {
            if (f.is<rs2::depth_frame>()) {
                rs2::depth_frame dframe = f.as<rs2::depth_frame>();
                d_width = dframe.get_width();
                d_height = dframe.get_height();

                if (d_width != depth_w || d_height != depth_h) {

                    std::cout << "Invalid depth frame resolution: "
                        << d_width << ", " << d_height << std::endl;

                    stop(pipeline);
                    return 1;
                }

                uint16_t* depthdata = (uint16_t*)dframe.get_data();
                memcpy(depthbuf, depthdata, depth_w * depth_h * sizeof(uint16_t));
                got_depth = true;

            } else if (f.template is<rs2::video_frame>()) {
                rs2::video_frame cframe = f.template as<rs2::video_frame>();
                c_width = cframe.get_width();
                c_height = cframe.get_height();

                // IR frames are sized the same as depth frames
                if (c_width == d_width) {
                    unsigned char* irdata = (unsigned char*)cframe.get_data();
                    if (ir_index == 0) {
                        memcpy(irbuf_l, irdata, depth_h * depth_w);
                        got_ir_left = true;
                    } else {
                        memcpy(irbuf_r, irdata, depth_h * depth_w);
                        got_ir_right = true;
                    }

                    ir_index++;
                } else {
                    if (c_width != color_w || c_height != color_h) {
                        std::cout << "Invalid color frame resolution: "
                            << c_width << ", " << c_height << std::endl;
                        stop(pipeline);
                        return 1;
                    }

                    unsigned char* colordata = (unsigned char*)cframe.get_data();
                    memcpy(colorbuf, colordata, color_w * color_h * 3);
                    got_color = true;
                }
            }
        }

        if (!got_color || !got_depth || !got_ir_left || !got_ir_right) {
            std::cout << "Did not get all frame types" << std::endl;
            break;
        }

        frames_got++;
        int64_t timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>
            (std::chrono::system_clock::now().time_since_epoch()).count();


        if (!dyncal.IsGridFull()) {
            int err = dyncal.AddImages(irbuf_l, irbuf_r, depthbuf, timestamp_ms);
            switch (err) {
            case DC_SUCCESS: break;
            case DC_ERROR_RECT_INVALID_IMAGES:
                std::cout << "DC_ERROR_RECT_INVALID_IMAGES" << std::endl;
                break;
            case DC_ERROR_RECT_INVALID_GRID_FILL:
                std::cout << "DC_ERROR_RECT_INVALID_GRID_FILL" << std::endl;
                break;
            case DC_ERROR_RECT_TOO_SIMILAR:
                std::cout << "DC_ERROR_RECT_TOO_SIMILAR" << std::endl;
                break;
            case DC_ERROR_RECT_TOO_MUCH_FEATURES:
                std::cout << "DC_ERROR_RECT_TOO_MUCH_FEATURES" << std::endl;
                break;
            case DC_ERROR_RECT_NO_FEATURES:
                std::cout << "DC_ERROR_RECT_NO_FEATURES" << std::endl;
                break;
            case DC_ERROR_RECT_GRID_FULL:
                std::cout << "DC_ERROR_RECT_GRID_FULL" << std::endl;
                break;
            case DC_ERROR_UNKNOWN:
                std::cout << "DC_ERROR_UNKNOWN" << std::endl;
                break;
            }
        }
        else {
            std::cout << "Writing calibration tables" << std::endl;
            int err = dyncal.UpdateCalibrationTables();
            switch (err) {
            case DC_ERROR_FAIL:
                std::cout << "Error writing calibration into the device" << std::endl;
                break;
            case DC_SUCCESS:
                std::cout << "Successfully wrote calibration into the device" << std::endl;
                break;
            }

            stop(pipeline);
            return 1;
        }

        // calculate frame time
        std::chrono::high_resolution_clock::time_point t2 = std::chrono::high_resolution_clock::now();
        auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();
        if (dur == 0) {
            dur = 1;
        }

        // calculate average fps
        ftimes.push_back(dur);

        if (ftimes.size() > 100) {
            dur_sum -= ftimes.front();
            ftimes.pop_front();
        }

        dur_sum += dur;
        int avg_dur = dur_sum / ftimes.size();

        if (dur_sum == 0) {
            dur_sum = 1;
        }

        int avg_fps = 1000 / avg_dur;

        std::cout << "Finished frame " << frames_got << " in " << dur
            << " milliseconds (" << avg_fps << " fps)" << std::endl;
    }

    std::cout << "exited main loop" << std::endl;

    stop(pipeline);
    std::cout << "pipeline stopped" << std::endl;

    return 0;
}
catch (const rs2::error & e) {
    std::cerr << "RealSense error calling " << e.get_failed_function()
        << "(" << e.get_failed_args() << "):\n    " << e.what() << std::endl;
    return 1;
}
catch (const std::exception& e) {
    std::cerr << "Unspecified exception: " << e.what() << std::endl;
    return 1;
}
