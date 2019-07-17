#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include <chrono>

#include <librealsense2/rs.h>
#include <librealsense2/rs_advanced_mode.h>
#include <librealsense2/h/rs_pipeline.h>
#include <librealsense2/h/rs_option.h>
#include <librealsense2/h/rs_frame.h>
#include <librealsense2/rsutil.h>

#include <librscalibrationapi/DSCalData.h>
#include <librscalibrationapi/DSDynamicCalibration.h>
#include <librscalibrationapi/DSOSUtils.h>
#include <librscalibrationapi/DSShared.h>
#include <librscalibrationapi/rs2-custom-calibration-mm.h>

const int cDepthW = 1280;
const int cDepthH = 720;
const int cColorW = 1920;
const int cColorH = 1080;
uint16_t* depthbuf = NULL;
uint8_t* ir_img_left = NULL;
uint8_t* ir_img_right = NULL;

class ReleaseLater
{
public:
	ReleaseLater() {
		frames = NULL;
	}

	virtual ~ReleaseLater() {
		for (auto& f : to_release)
			rs2_release_frame(f);
		if (frames != NULL)
			rs2_release_frame(frames);
	}
	std::vector<rs2_frame*> to_release;
	rs2_frame* frames;
};

#define RS_STATE_SENSORS_MAX 20
struct RS_State
{
	rs2_context* ctx;
	rs2_device_list* device_list;
	int32_t dev_count;
	rs2_device* dev;
	rs2_sensor_list* sensor_list;
	rs2_sensor* sensors[RS_STATE_SENSORS_MAX];
	int32_t sensors_created;
	int32_t sensor_list_count;
	int32_t advanced_enabled;
	rs2_pipeline* pipe;
	rs2_pipeline_profile* selection;
	rs2_pipeline_profile* started_pipeline;
	rs2_stream_profile_list* stream_list;
	int32_t stream_list_count;
	rs2_config* config;
	rs2_processing_block* temporal_filter;
	rs2_frame_queue* frame_queue;
};

int8_t check_error(rs2_error* e)
{
	if (e)
	{
		fprintf(stderr, "rs_error was raised when calling %s(%s): \n",
			   rs2_get_failed_function(e), rs2_get_failed_args(e));
		fprintf(stderr, "%s\n", rs2_get_error_message(e));
		return 1;
	}
	return 0;
}

int8_t clear_state(struct RS_State* s)
{
    if (s == NULL) {
        fprintf(stderr, "Cannot clear state: given pointer is null\n");
        return 1;
    }

    if(s->temporal_filter) {
        rs2_delete_processing_block(s->temporal_filter);
    }

    if (s->frame_queue) {
        rs2_delete_frame_queue(s->frame_queue);
    }

    if (s->dev) {
        rs2_delete_device(s->dev);
    }

    int32_t sen;
    for (sen = 0; sen < s->sensors_created; sen++) {
        rs2_delete_sensor(s->sensors[sen]);
    }

    if (s->sensor_list) {
        rs2_delete_sensor_list(s->sensor_list);
    }

    if (s->device_list) {
        rs2_delete_device_list(s->device_list);
    }

    if (s->config) {
        rs2_delete_config(s->config);
    }

    if (s->stream_list) {
        rs2_delete_stream_profiles_list(s->stream_list);
    }

    if (s->selection) {
        rs2_delete_pipeline_profile(s->selection);
    }

    if (s->started_pipeline) {
        rs2_delete_pipeline_profile(s->started_pipeline);
    }

    if (s->pipe) {
        rs2_pipeline_stop(s->pipe, NULL);
        rs2_delete_pipeline(s->pipe);
    }

    if (s->ctx) {
        rs2_delete_context(s->ctx);
    }

    memset(s, 0, sizeof(struct RS_State));
    return 0;
}

int clean_exit(int exitcode, struct RS_State* s) {
	delete[] depthbuf;
	delete[] ir_img_left;
	delete[] ir_img_right;
	depthbuf = NULL;
	ir_img_left = NULL;
	ir_img_right = NULL;

	clear_state(s);
	fprintf(stderr, "Exiting with exit code: %d\n", exitcode);
	return exitcode;
}

int main(int argc, char** argv) {
	rs2_error* e = NULL;

	struct RS_State rs_state;
	memset(&rs_state, 0, sizeof(rs_state));

	DynamicCalibrationAPI::DSDynamicCalibration dyncal;

	depthbuf = new uint16_t[cDepthH*cDepthW];
	ir_img_left = new uint8_t[cDepthH*cDepthW];
	ir_img_right = new uint8_t[cDepthH*cDepthW];

	if (depthbuf == NULL || ir_img_left == NULL || ir_img_right == NULL) {
		fprintf(stderr, "failed allocating memory\n");
		return clean_exit(1, &rs_state);
	}

	memset(depthbuf, 0, cDepthH*cDepthW*sizeof(uint16_t));
	memset(ir_img_left, 0, cDepthH*cDepthW);
	memset(ir_img_right, 0, cDepthH*cDepthW);

	if (rs_state.ctx != NULL) {
		fprintf(stderr, "create_context called, but context exists\n");
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "creating context\n");

	rs_state.ctx = rs2_create_context(RS2_API_VERSION, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed creating rs context\n");
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "context created\n");

	rs_state.device_list = rs2_query_devices(rs_state.ctx, &e);
	if (check_error(e) != 0) {
		return clean_exit(1, &rs_state);
	}

	rs_state.dev_count = rs2_get_device_count(rs_state.device_list, &e);
	if (check_error(e) != 0) {
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "There are %d connected RealSense devices.\n", rs_state.dev_count);
	if (0 == rs_state.dev_count) {
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "Creating device\n");
	rs_state.dev = rs2_create_device(rs_state.device_list, 0, &e);
	if (check_error(e) != 0) {
		return clean_exit(1, &rs_state);
	}

	rs_state.pipe = rs2_create_pipeline(rs_state.ctx, &e);
	if (check_error(e) != 0) {
		return clean_exit(1, &rs_state);
	}

	rs_state.config = rs2_create_config(&e);
	if (check_error(e) != 0) {
		return clean_exit(1, &rs_state);
	}

	rs2_config_enable_stream(rs_state.config, RS2_STREAM_DEPTH, -1, cDepthW, cDepthH, RS2_FORMAT_Z16, 30, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed initting depth streaming\n");
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "Depth stream created: %d x %d at %d FPS\n", cDepthW, cDepthH, 30);

	rs2_config_enable_stream(rs_state.config, RS2_STREAM_COLOR, -1, cColorW, cColorH, RS2_FORMAT_RGB8, 30, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed initting color streaming\n");
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "Color stream created: %d x %d at %d FPS\n", cColorW, cColorH, 30);

	rs2_config_enable_stream(rs_state.config, RS2_STREAM_INFRARED, 1, cDepthW, cDepthH, RS2_FORMAT_Y8, 30, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed initting ir 1 streaming\n");
		return clean_exit(1, &rs_state);
	}

	rs2_config_enable_stream(rs_state.config, RS2_STREAM_INFRARED, 2, cDepthW, cDepthH, RS2_FORMAT_Y8, 30, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed initting ir 2 streaming\n");
		return clean_exit(1, &rs_state);
	}

	rs_state.selection = rs2_config_resolve(rs_state.config, rs_state.pipe, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed resolving config\n");
		return clean_exit(1, &rs_state);
	}

	rs_state.dev = rs2_pipeline_profile_get_device(rs_state.selection, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed getting device for pipeline profile\n");
		return clean_exit(1, &rs_state);
	}

	rs_state.sensor_list = rs2_query_sensors(rs_state.dev, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed querying for sensors\n");
		return clean_exit(1, &rs_state);
	}

	rs_state.sensor_list_count = rs2_get_sensors_count(rs_state.sensor_list, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed getting sensor list count\n");
		return clean_exit(1, &rs_state);
	}

	int sensor;
	for (sensor = 0; sensor < rs_state.sensor_list_count; sensor++) {
		rs_state.sensors[sensor] = rs2_create_sensor(rs_state.sensor_list, sensor, &e);
		if (check_error(e) != 0) {
			fprintf(stderr, "Failed creating sensor %d / %d\n", sensor, rs_state.sensor_list_count);
			return clean_exit(1, &rs_state);
		}

		rs_state.sensors_created++;
	}

	rs_state.stream_list = rs2_pipeline_profile_get_streams(rs_state.selection, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed getting pipeline profile streams\n");
		return clean_exit(1, &rs_state);
	}

	rs_state.stream_list_count = rs2_get_stream_profiles_count(rs_state.stream_list, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed getting pipeline profile stream count\n");
		return clean_exit(1, &rs_state);
	}

	rs_state.started_pipeline = rs2_pipeline_start_with_config(rs_state.pipe, rs_state.config, &e);
	if (check_error(e) != 0) {
		fprintf(stderr, "Failed starting pipeline\n");
		return clean_exit(1, &rs_state);
	}

	fprintf(stderr, "pipeline started\n");

	int retcode = dyncal.Initialize(rs_state.dev,
		DynamicCalibrationAPI::DSDynamicCalibration::CAL_MODE_INTEL_TARGETLESS,
		cDepthW, cDepthH, true);

	switch (retcode) {
	case DC_SUCCESS:
		fprintf(stderr, "dyncal initialized successfully\n");
		break;
	case DC_ERROR_INVALID_PARAMETER:
		fprintf(stderr, "dyncal initialize DC_ERROR_INVALID_PARAMETER\n");
		return clean_exit(1, &rs_state);
	case DC_ERROR_RESOLUTION_NOT_SUPPORTED_V2:
		fprintf(stderr, "dyncal initialize DC_ERROR_RESOLUTION_NOT_SUPPORTED_V2\n");
		return clean_exit(1, &rs_state);
	case DC_ERROR_TABLE_NOT_SUPPORTED:
		fprintf(stderr, "dyncal initialize DC_ERROR_TABLE_NOT_SUPPORTED\n");
		return clean_exit(1, &rs_state);
	case DC_ERROR_TABLE_NOT_VALID_RESOLUTION:
		fprintf(stderr, "dyncal initialize DC_ERROR_TABLE_NOT_VALID_RESOLUTION\n");
		return clean_exit(1, &rs_state);
	}

	while (true) {
		rs2_frame* frames = NULL;
		ReleaseLater releaseLater;
		releaseLater.frames = frames;
		int got_ir_frames = 0;

		frames = rs2_pipeline_wait_for_frames(rs_state.pipe, 30000, &e);
		if (check_error(e) != 0) {
			fprintf(stderr, "Failed waiting for frames\n");
			return clean_exit(1, &rs_state);
		}

		int num_frames = rs2_embedded_frames_count(frames, &e);
		if (check_error(e) != 0) {
			fprintf(stderr, "Failed getting framelist size\n");
			return clean_exit(1, &rs_state);
		}

		for (int f = 0; f < num_frames; f++)
		{
			rs2_frame* fr = rs2_extract_frame(frames, f, &e);
			if (check_error(e) != 0) {
				fprintf(stderr, "Failed extracting frame %d / %d\n", f, num_frames);
				return clean_exit(1, &rs_state);
			}

			releaseLater.to_release.push_back(fr);

			if (rs2_is_frame_extendable_to(fr, RS2_EXTENSION_DEPTH_FRAME, &e) == 1)
			{
				releaseLater.to_release.push_back(fr);

				if (check_error(e) != 0) {
					fprintf(stderr, "Failed waiting for frame after frame queue\n");
					return clean_exit(1, &rs_state);
				}

				uint16_t* dbuf = (uint16_t*)(rs2_get_frame_data(fr, &e));
				if (check_error(e) != 0) {
					fprintf(stderr, "Failed getting depth frame\n");
					return clean_exit(1, &rs_state);
				}

				memcpy(depthbuf, dbuf, cDepthW*cDepthH*sizeof(uint16_t));
			} else if (rs2_is_frame_extendable_to(fr, RS2_EXTENSION_VIDEO_FRAME, &e) == 1) {
				int fheight = rs2_get_frame_height(fr, &e);
				if (check_error(e) != 0) {
					fprintf(stderr, "Failed getting color frame\n");
					return clean_exit(1, &rs_state);
				}

				// Color and IR frames can be told apart by their resolution
				if (fheight == cDepthH) {

					uint8_t* data = (uint8_t*)rs2_get_frame_data(fr, &e);
					if (f == 2) {
						memcpy(ir_img_left, data, cDepthW*cDepthH);
					} else {
						memcpy(ir_img_right, data, cDepthW*cDepthH);
					}

					got_ir_frames++;
				} else {
					// color not used in this example
				}

			}
		}

		if (got_ir_frames == 2) {

			int64_t timestamp_us = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();

			fprintf(stderr, "got 2 ir frames for calibration\n");
			if (!dyncal.IsGridFull()) {
				int err = dyncal.AddImages(ir_img_left, ir_img_right, depthbuf, timestamp_us/1000);
				switch (err) {
				case DC_SUCCESS: break;
				case DC_ERROR_RECT_INVALID_IMAGES:
					fprintf(stderr, "DC_ERROR_RECT_INVALID_IMAGES\n");
					break;
				case DC_ERROR_RECT_INVALID_GRID_FILL:
					fprintf(stderr, "DC_ERROR_RECT_INVALID_GRID_FILL\n");
					break;
				case DC_ERROR_RECT_TOO_SIMILAR:
					fprintf(stderr, "DC_ERROR_RECT_TOO_SIMILAR\n");
					break;
				case DC_ERROR_RECT_TOO_MUCH_FEATURES:
					fprintf(stderr, "DC_ERROR_RECT_TOO_MUCH_FEATURES\n");
					break;
				case DC_ERROR_RECT_NO_FEATURES:
					fprintf(stderr, "DC_ERROR_RECT_NO_FEATURES\n");
					break;
				case DC_ERROR_RECT_GRID_FULL:
					fprintf(stderr, "DC_ERROR_RECT_GRID_FULL\n");
					break;
				case DC_ERROR_UNKNOWN:
					fprintf(stderr, "DC_ERROR_UNKNOWN\n");
					break;
				}
			}
			else {
				fprintf(stderr, "Writing calibration tables\n");
				int err = dyncal.UpdateCalibrationTables();
				switch (err) {
				case DC_ERROR_FAIL:
					fprintf(stderr, "Error writing calibration into the device\n");
					break;
				case DC_SUCCESS:
					fprintf(stderr, "Successfully wrote calibration into the device\n");
					break;
				}

				return clean_exit(0, &rs_state);
			}
		} else {
			fprintf(stderr, "got invalid number of	ir frames for calibration: %d\n", got_ir_frames);
		}
	}

	return clean_exit(0, &rs_state);
}

