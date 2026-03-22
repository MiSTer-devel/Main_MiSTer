#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "screenshot.h"
#include "shmem.h"
#include "user_io.h"
#include "scaler.h"
#include "image_save.h"
#include "menu.h"
#include "video.h"
#include "offload.h"
#include "cfg.h"

#ifdef PROFILING
#include "profiling.h"
#endif

/*
    screenshots
    ===========
    we indicate our desire to take a screenshot by setting the request_screenshot to true

    only one screenshot can be in process at a time. we gate this via screenshot_pending_atomic.
    
    this is important because we use a static buffer to capture the image into to avoid the
    overhead of a malloc/free every time we take a screenshot.

    a screenshot callback set in user_io_poll() runs every vsync and checks if a screenshot
    has been requested. we do it this way to reduce the risk of taking a screenshot while the
    scaler is being updated and getting a corrupted image or tearing.

    the scaler is copied to a static buffer in the main thread, then all scaling/compression/saving
    to the final image is offloaded to an asynchronous worker.

    the worker reports back via the ScreenshotResult_atomic struct.
    the same callback that checks for screenshot requests also checks those results.
    
    Info() corrupted the screen if I called it from a worker thread, so I assume this means it's
    not thread safe.
*/

static uint8_t screenshot_outputbuf[MISTER_SCALER_BUFFERSIZE];

extern char last_filename[1024];

enum ScreenshotResult {
    SCREENSHOT_SUCCESS,
    SCREENSHOT_FAILURE
};

enum ScreenshotFormat {
    FORMAT_PNG,
    FORMAT_BMP,
    FORMAT_RGB
};

struct ScreenshotResult_atomic {
    int result;
    char *filename;
};

static bool screenshot_requested = false;
static int screenshot_rescale = 0;
static char* screenshot_filename = NULL;

// atomic variables for worker process
static std::atomic<bool> screenshot_pending_atomic{false};
static std::atomic<ScreenshotResult_atomic*> screenshot_result_data_atomic{nullptr};

static void save_screenshot(ScreenshotFormat image_format, int do_rescale, int base_width, int base_height, int scaled_width, int scaled_height, unsigned char *outputbuf, char *filename) {
		bool success = false;
        if (do_rescale)
		{
			printf("rescaling screenshot from %dx%d to %dx%d\n", base_width, base_height, scaled_width, scaled_height);
			switch (image_format) {
                case FORMAT_PNG:
                    success = write_png_32(filename, outputbuf, base_width, base_height, scaled_width, scaled_height);
                    break;
                case FORMAT_BMP:
                    success = write_bmp_24(filename, outputbuf, base_width, base_height, scaled_width, scaled_height);
                    break;
                case FORMAT_RGB:
                    success = write_raw_rgb(filename, outputbuf, base_width, base_height, scaled_width, scaled_height);
                    break;
                default:
                    // should be unreachable
                    success = false;
                    break;
            }
            
		}
		else
		{
            switch (image_format) {
                case FORMAT_PNG:
                    success = write_png_32(filename, outputbuf, base_width, base_height);
                    break;
                case FORMAT_BMP:
                    success = write_bmp_24(filename, outputbuf, base_width, base_height);
                    break;
                case FORMAT_RGB:
                    success = write_raw_rgb(filename, outputbuf, base_width, base_height);
                    break;
                default:
                    // should be unreachable
                    success = false;
                    break;
            }
		}

        ScreenshotResult_atomic *result_data =
        (ScreenshotResult_atomic *)malloc(sizeof(ScreenshotResult_atomic));
        
        if (result_data)
        {
            result_data->result = success ? SCREENSHOT_SUCCESS : SCREENSHOT_FAILURE;
            result_data->filename = filename;
            screenshot_result_data_atomic = result_data;
        }
        else
        {
            free(filename);
        }

		screenshot_pending_atomic = false;
	};

void do_screenshot(char* imgname)
{
	#ifdef PROFILING
		PROFILE_FUNCTION();
	#endif
	
	screenshot_pending_atomic = true;
	screenshot_requested = false;

	mister_scaler *ms = mister_scaler_init();
	if (ms == NULL)
	{
		printf("problem with scaler, maybe not a new enough version\n");
		Info("Scaler not compatible");
		screenshot_pending_atomic = false;
		free(imgname);
		imgname = NULL;
		return;
	}
	
	const char *basename = last_filename;
	if( imgname && *imgname )
		basename = imgname;

	char filename[1024];

	int base_width = ms->width;
	int base_height = ms->height;
	int scaled_width = ms->output_width;
	int scaled_height = ms->output_height;

    size_t needed = (size_t)base_width * (size_t)base_height * 4;
    
    if (needed > sizeof(screenshot_outputbuf))
    { mister_scaler_free(ms);
		screenshot_pending_atomic = false;
		screenshot_rescale = 0;
		return; 
    }
	
    ScreenshotFormat image_format;

    const char *extension;

    if (!strcasecmp(cfg.screenshot_image_format, "png"))
        image_format = FORMAT_PNG;
    else if (!strcasecmp(cfg.screenshot_image_format, "bmp"))
        image_format = FORMAT_BMP;
    else if (!strcasecmp(cfg.screenshot_image_format, "rgb"))
        image_format = FORMAT_RGB;
    else {
        printf("Unknown screenshot image format in config: %s; defaulting to PNG\n", cfg.screenshot_image_format);
        image_format = FORMAT_PNG;
    }

    switch (image_format) {
        case FORMAT_PNG:
            mister_scaler_read(ms, screenshot_outputbuf, BGRA);
            extension = ".png";
            break;
        case FORMAT_BMP:
            mister_scaler_read(ms, screenshot_outputbuf, BGR);
            extension = ".bmp";
            break;
        case FORMAT_RGB:
            mister_scaler_read(ms, screenshot_outputbuf, RGB);
            extension = ".rgb";
            break;
        default:
            // should be unreachable
            break;
    }
    
    FileGenerateScreenshotName(basename, filename, extension, 1024);

    free(imgname);
	imgname = NULL;
	
    mister_scaler_free(ms);

	if (video_get_rotated())
	{
		//If the video is rotated, the scaled output resolution results in a squished image.
		//Calculate the scaled output res using the original AR
		scaled_width = scaled_height * ((float)base_width/base_height);
	}
	
	char* filename_copy = strdup(filename);
	
	if (!filename_copy) {
		screenshot_pending_atomic = false;
		screenshot_rescale = 0;
		return;
	}

	int do_rescale = screenshot_rescale;
	offload_add_work([=]() {
        save_screenshot(image_format, do_rescale, base_width, base_height, scaled_width, scaled_height, screenshot_outputbuf, filename_copy);
    });
	return;
}

void request_screenshot(char *cmd, int scaled)
{
    if (screenshot_pending_atomic || screenshot_requested)
        return;
    
    if (!cmd) // guard against NULL
        cmd = (char *)"";

    while (*cmd != '\0' && (*cmd == ' ' || *cmd == '\t' || *cmd == '\n'))
        cmd++;

    char *copy = strdup(cmd);
    if (!copy)
        return;

    screenshot_filename = copy;
    screenshot_rescale = scaled;
    screenshot_requested = true;
}

void screenshot_cb(void)
{
    ScreenshotResult_atomic *result_data =
        screenshot_result_data_atomic.exchange(nullptr);

    if (result_data) {

        switch (result_data->result)
        {
            case SCREENSHOT_SUCCESS:
                if (result_data->filename)
                {
                    char msg[1024];
                    snprintf(msg, sizeof(msg), "Screen saved to\n%s",
                            result_data->filename + strlen(SCREENSHOT_DIR "/"));
                    printf("%s\n", msg);
                    Info(msg);
                }
                else
                {
                    printf("Screenshot saved\n");
                    Info("Screenshot saved");
                }
                break;

            case SCREENSHOT_FAILURE:
                printf("Screenshot failed\n");
                Info("Screenshot failed");
                break;

            default:
                break;
        }
        free(result_data->filename);
        free(result_data);
    }


	if (screenshot_requested && !screenshot_pending_atomic)
	{
		char *imgname = screenshot_filename;
		screenshot_filename = NULL;
		do_screenshot(imgname);
	}
    
}