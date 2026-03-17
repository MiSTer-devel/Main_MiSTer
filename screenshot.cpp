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
#ifdef PROFILING
#include "profiling.h"
#endif

/*
    screenshots
    ===========
    we indicate our desire to take a screenshot by setting the request_screenshot_atomic to true

    a screenshot callback set in user_io_poll() runs every vsync and checks if a screenshot
    has been requested. we do it this way to reduce the risk of taking a screenshot while the
    scaler is being updated and getting a corrupted image or tearing.

    the scaler is copied to a buffer in the main thread, then all scaling/compression/saving
    to the final .png is offloaded to an asynchronous worker.

    the worker reports back via the ScreenshotResult_atomic struct.
    the same callback that checks for screenshot requests also checks those results.
    Info() corrupted the screen if I called it from a worker thread, so I assume this means it's
    not thread safe.
*/

extern char last_filename[1024];

enum ScreenshotResult {
    SCREENSHOT_NONE = 0,
    SCREENSHOT_SUCCESS = 1,
    SCREENSHOT_FAILURE = 2
};

struct ScreenshotResult_atomic {
    int result;
    char *filename;
};

// atomic variables for worker process
static std::atomic<bool> screenshot_pending_atomic{false};
static std::atomic<bool> screenshot_requested_atomic{false};
static std::atomic<int>  screenshot_rescale_atomic{0};
static std::atomic<char *> screenshot_filename_atomic{NULL};
static std::atomic<ScreenshotResult_atomic*> screenshot_result_data_atomic{nullptr};

void do_screenshot(char* imgname)

{
	#ifdef PROFILING
		PROFILE_FUNCTION();
	#endif
	
	screenshot_pending_atomic = true;
	screenshot_requested_atomic = false;

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

	FileGenerateScreenshotName(basename, filename, 1024);

	free(imgname);
	imgname = NULL;

	int base_width = ms->width;
	int base_height = ms->height;
	int scaled_width = ms->output_width;
	int scaled_height = ms->output_height;

	unsigned char *outputbuf = (unsigned char *)calloc(base_width*base_height * 4, 1);
	
	if (!outputbuf) {
		mister_scaler_free(ms);
		screenshot_pending_atomic = false;
		screenshot_rescale_atomic = 0;
		return;
	}

	mister_scaler_read(ms, outputbuf, BGRA);
	mister_scaler_free(ms);

	if (video_get_rotated())
	{
		//If the video is rotated, the scaled output resolution results in a squished image.
		//Calculate the scaled output res using the original AR
		scaled_width = scaled_height * ((float)base_width/base_height);
	}
	
	char* filename_copy = strdup(filename);
	
	if (!filename_copy) {
		free(outputbuf);
		screenshot_pending_atomic = false;
		screenshot_rescale_atomic = 0;
		return;
	}

	int do_rescale = screenshot_rescale_atomic;

	offload_add_work([do_rescale, base_width, base_height, scaled_width, scaled_height, outputbuf, filename_copy] {
		bool success = false;
        if (do_rescale)
		{
			printf("rescaling screenshot from %dx%d to %dx%d\n", base_width, base_height, scaled_width, scaled_height);
			success = write_png_32(filename_copy, outputbuf, base_width, base_height, scaled_width, scaled_height);
		}
		else
		{
			printf("saving screenshot at native resolution %dx%d\n", base_width, base_height);
			success = write_png_32(filename_copy, outputbuf, base_width, base_height);
		}
		free(filename_copy);
		free(outputbuf);

        ScreenshotResult_atomic *result_data =
        (ScreenshotResult_atomic *)malloc(sizeof(ScreenshotResult_atomic));
        
        if (result_data)
        {
            result_data->result = success ? SCREENSHOT_SUCCESS : SCREENSHOT_FAILURE;
            result_data->filename = filename_copy;
            screenshot_result_data_atomic = result_data;
        }
        else
        {
            free(filename_copy);
        }

		screenshot_pending_atomic = false;
	});

	return;
}

void request_screenshot(char *cmd, int scaled)
{
    if (screenshot_pending_atomic || screenshot_requested_atomic)
        return;

    while (*cmd != '\0' && (*cmd == ' ' || *cmd == '\t' || *cmd == '\n'))
        cmd++;

    char *copy = strdup(cmd);
    if (!copy)
        return;

    screenshot_filename_atomic = copy;
    screenshot_rescale_atomic = scaled;
    screenshot_requested_atomic = true;
}

void screenshot_cb(void)
{
	if (screenshot_requested_atomic && !screenshot_pending_atomic)
	{
		char *imgname = screenshot_filename_atomic.exchange(nullptr);
		do_screenshot(imgname);
	}

    ScreenshotResult_atomic *result_data =
        screenshot_result_data_atomic.exchange(nullptr);

    if (!result_data)
        return;

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