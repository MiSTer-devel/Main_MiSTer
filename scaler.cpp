/*
Copyright 2019 alanswx
with help from the MiSTer contributors including Grabulosaure
*/

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <sched.h>
#include <inttypes.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <atomic>

#include <sys/types.h>
#include <err.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
const int VEC_WIDTH = 16;
#endif

#include "video.h"
#include "cfg.h"
#include "offload.h"
#include "scaler.h"
#include "lib/imlib2/Imlib2.h"
#include "shmem.h"
#include "file_io.h"
#include "menu.h"

#ifdef PROFILING
#include "profiling.h"
#endif

mister_scaler * mister_scaler_init()
{
    mister_scaler *ms = (mister_scaler *)calloc(1, sizeof(mister_scaler));
    int	 pagesize = sysconf(_SC_PAGE_SIZE);
    if (pagesize==0) pagesize=4096;
    int offset = MISTER_SCALER_BASEADDR;
    int	map_start = offset & ~(pagesize - 1);
    ms->map_off = offset - map_start;
    ms->num_bytes=MISTER_SCALER_BUFFERSIZE;
    //printf("map_start = %d map_off=%d offset=%d\n",map_start,ms->map_off,offset);

    unsigned char *buffer;
    ms->map=(char *)shmem_map(map_start, ms->num_bytes+ms->map_off);
    if (!ms->map)
    {
        mister_scaler_free(ms);
        return NULL;
    }
    buffer = (unsigned char *)(ms->map+ms->map_off);
    printf (" 1: %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X\n",
            buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],
            buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13],buffer[14],buffer[15]);
    if (buffer[0]!=1 || buffer[1]!=1) {
        printf("problem\n");
        mister_scaler_free(ms);
        return NULL;
    }

    ms->header=buffer[2]<<8 | buffer[3];
    ms->width =buffer[6]<<8 | buffer[7];
    ms->height=buffer[8]<<8 | buffer[9];
    ms->line  =buffer[10]<<8 | buffer[11];
    ms->output_width =buffer[12]<<8 | buffer[13];
    ms->output_height=buffer[14]<<8 | buffer[15];

    printf ("Image: Width=%i Height=%i  Line=%i  Header=%i output_width=%i output_height=%i \n",ms->width,ms->height,ms->line,ms->header,ms->output_width,ms->output_height);
   /*
    printf (" 1: %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X   %02X %02X %02X %02X\n",
            buffer[0],buffer[1],buffer[2],buffer[3],buffer[4],buffer[5],buffer[6],buffer[7],
            buffer[8],buffer[9],buffer[10],buffer[11],buffer[12],buffer[13],buffer[14],buffer[15]);
    */

   return ms;

}

void mister_scaler_free(mister_scaler *ms)
{
   shmem_unmap(ms->map,ms->num_bytes+ms->map_off);
   free(ms);
}

// not currently used - leaving in for reference in case we want to add YUV later
/*
int mister_scaler_read_yuv(mister_scaler *ms,int lineY,unsigned char *bufY, int lineU, unsigned char *bufU, int lineV, unsigned char *bufV)
{
    unsigned char *buffer;
    buffer = (unsigned char *)(ms->map+ms->map_off);

    // do this slow way for now..
    unsigned char *pixbuf;
    unsigned char *outbufy;
    unsigned char *outbufU;
    unsigned char *outbufV;
    for (int  y=0; y< ms->height ; y++)
	{
        pixbuf=&buffer[ms->header + y*ms->line];
        outbufy=&bufY[y*(lineY)];
        outbufU=&bufU[y*(lineU)];
        outbufV=&bufV[y*(lineV)];
        for (int x = 0; x < ms->width ; x++)
		{
			int R,G,B;
			R = *pixbuf++;
			G = *pixbuf++;
			B = *pixbuf++;
			int Y =  (0.257 * R) + (0.504 * G) + (0.098 * B) + 16;
			int U = -(0.148 * R) - (0.291 * G) + (0.439 * B) + 128;
			int V =  (0.439 * R) - (0.368 * G) - (0.071 * B) + 128;

			*outbufy++ = Y;
			*outbufU++ = U;
			*outbufV++ = V;
        }
    }

    return 0;
}
*/

// use NEON if available
#if defined(__ARM_NEON)

// simd optimized copy from scaler to buffer.
// simd seems like overkill but it reduced the time it took to copy data from the scaler into
// a buffer by an order of magnitude (~50-80ms down to ~3-5ms) which is huge when we perform
// the copy on the main thread.

// if you've not done simd stuff before this is a very straightforward use of it
// so this might be a nice example.

// ARGB32 explicitly respects endianness, the other formats don't

int mister_scaler_read(mister_scaler *ms, unsigned char *gbuf, mister_scaler_format_t format)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif

    unsigned char *buffer = (unsigned char *)(ms->map + ms->map_off);
    
    // this is a "splat" - prefilling a vector register with a single value
    const uint8x16_t alpha = vdupq_n_u8(0xFF);

    for (int y = 0; y < ms->height; y++) {
        unsigned char *pixbuf = &buffer[ms->header + y * ms->line];
        unsigned char *outbuf;

        if (format == RGBA || format == BGRA || format == ARGB32)
            outbuf = &gbuf[y * (ms->width * 4)];
        else
            outbuf = &gbuf[y * (ms->width * 3)];
        
        // VEC_WIDTH is the number of elements a vector register can hold.
        // 24/32-bit image data is stored as pixels of 3 or 4 bytes (8 bits).
        // our ARMv7 NEON registers are 128 bits / 8 bits = 16 bytes per register.
        // any data left after doing 16-byte chunks falls back to our scalar code to be completed.
        int limit = ms->width - (ms->width % VEC_WIDTH);
        for (int x = 0; x < limit; x += VEC_WIDTH) {
            
            // load 16 pixels (48 bytes) from the scaler buffer into our vector registers.
            // uint8x16x3_t is a struct of 3 uint8x16_t vectors, so it can hold 16 pixels of RGB data.
            // behind the scenes we'll have three vector registers, each containing 16 bytes
            // representing red, green, or blue values for that pixel.
            uint8x16x3_t rgb = vld3q_u8(pixbuf + x * 3);

            switch (format)
            {
                case RGB:
                    vst3q_u8(outbuf + x * 3, rgb);
                    break;
                // some image formats don't use RGB ordering, so we need to shuffle our data
                // around. this is easy since uint8x16x3_t is a struct
                case BGR:
                    uint8x16x3_t bgr;
                    bgr.val[0] = rgb.val[2];   
                    bgr.val[1] = rgb.val[1];   
                    bgr.val[2] = rgb.val[0];   
                    vst3q_u8(outbuf + x * 3, bgr);
                    break;
                case RGBA:
                    uint8x16x4_t rgba;
                    rgba.val[0] = rgb.val[0];   
                    rgba.val[1] = rgb.val[1];   
                    rgba.val[2] = rgb.val[2];   
                    rgba.val[3] = alpha;        
                    vst4q_u8(outbuf + x * 4, rgba);
                    break;
                case BGRA:
                    uint8x16x4_t bgra;
                    bgra.val[0] = rgb.val[2];   
                    bgra.val[1] = rgb.val[1];   
                    bgra.val[2] = rgb.val[0];   
                    bgra.val[3] = alpha;        
                    vst4q_u8(outbuf + x * 4, bgra);
                    break;
                case ARGB32:
                    uint8x16x4_t argb32;
                #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                    // 0xAARRGGBB in == BB GG RR AA
                    argb32.val[0] = rgb.val[2];   // B
                    argb32.val[1] = rgb.val[1];   // G
                    argb32.val[2] = rgb.val[0];   // R
                    argb32.val[3] = alpha;        // A
                #else
                    // 0xAARRGGBB == AA RR GG BB
                    argb32.val[0] = alpha;        // A
                    argb32.val[1] = rgb.val[0];   // R
                    argb32.val[2] = rgb.val[1];   // G
                    argb32.val[3] = rgb.val[2];   // B
                #endif
                    vst4q_u8(outbuf + x * 4, argb32);
                    break;
                case YUV:
                    // not supported yet
                default:
                    break;
            }
        }

        // scalar tail; this processes any remaining data that didn't fit into our vector
        // registers. this is (basically) the same code we'd use if we weren't using simd at all.
        switch (format)
        {
            case RGB:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 2];
                }
                break;
            case BGR:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 2];
                }
                break;
            case RGBA:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case BGRA:
                for (int x = limit; x < ms->width; x++) {
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case ARGB32:
                for (int x = limit; x < ms->width; x++) {
                #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 2]; // B
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1]; // G
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 0]; // R
                    outbuf[x * 4 + 3] = 0xFF;              // A
                #else
                    outbuf[x * 4 + 0] = 0xFF;              // A
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 0]; // R
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 1]; // G
                    outbuf[x * 4 + 3] = pixbuf[x * 3 + 2]; // B
                #endif
                }
                break;
            case YUV:
                // not supported yet
            default:
                break;
        }
    }
    return 0;
}

#else

// no NEON available, do all scalar
int mister_scaler_read(mister_scaler *ms, unsigned char *gbuf, mister_scaler_format_t format = RGB)
{
    #ifdef PROFILING
        PROFILE_FUNCTION();
    #endif

    unsigned char *buffer = (unsigned char *)(ms->map + ms->map_off);

    for (int y = 0; y < ms->height; y++) {
        unsigned char *pixbuf = &buffer[ms->header + y * ms->line];
        unsigned char *outbuf;

        if (format == RGBA || format == BGRA)
            outbuf = &gbuf[y * (ms->width * 4)];
        else
            outbuf = &gbuf[y * (ms->width * 3)];

        // scalar version
        switch (format)
        {
            case RGB:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 2];
                }
                break;
            case BGR:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 3 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 3 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 3 + 0] = pixbuf[x * 3 + 2];
                }
                break;
            case RGBA:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case BGRA:
                for (int x = 0; x < ms->width; x++) {
                    outbuf[x * 4 + 2] = pixbuf[x * 3 + 0];
                    outbuf[x * 4 + 1] = pixbuf[x * 3 + 1];
                    outbuf[x * 4 + 0] = pixbuf[x * 3 + 2];
                    outbuf[x * 4 + 3] = 0xFF;
                }
                break;
            case ARGB32:
            for (int x = limit; x < ms->width; x++) {
            #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
                outbuf[x * 4 + 0] = pixbuf[x * 3 + 2]; // B
                outbuf[x * 4 + 1] = pixbuf[x * 3 + 1]; // G
                outbuf[x * 4 + 2] = pixbuf[x * 3 + 0]; // R
                outbuf[x * 4 + 3] = 0xFF;              // A
            #else
                outbuf[x * 4 + 0] = 0xFF;              // A
                outbuf[x * 4 + 1] = pixbuf[x * 3 + 0]; // R
                outbuf[x * 4 + 2] = pixbuf[x * 3 + 1]; // G
                outbuf[x * 4 + 3] = pixbuf[x * 3 + 2]; // B
            #endif
                }
                break;
            case YUV:
                // not supported yet
            default:
                break;
        }
    }
    return 0;
}

#endif

/*
    screenshots
    ===========
    screenshot_cb -> screenshot callback that runs every vsync and checks for requests/results
    request_screenshot -> say we want a screenshot
    do_screenshot -> runs on main thread and copies from the scaler
    save_screenshot -> wrapper to call write_screenshot with expected parameters (worker thread)
    write_screenshot -> does the actual work of writing the screenshot to disk (worker thread)

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

// static buffers to avoid malloc/free every screenshot
static uint8_t screenshot_outputbuf[MISTER_SCALER_BUFFERSIZE];

extern char last_filename[1024];

enum ScreenshotResult {
    SCREENSHOT_SUCCESS,
    SCREENSHOT_FAILURE
};

struct ScreenshotResult_atomic {
    int result;
    char *filename;
};

static bool screenshot_requested = false;
static int screenshot_rescale = 0;
static char* screenshot_filename = NULL;

bool write_screenshot(const char *filename, const uint8_t *argb,
                      int width, int height, int output_width = 0, int output_height = 0);

// atomic variables for worker process
static std::atomic<bool> screenshot_pending_atomic{false};
static std::atomic<ScreenshotResult_atomic*> screenshot_result_data_atomic{nullptr};

static struct { const char *fmtstr; Imlib_Load_Error errno; } err_strings[] = {
  {"file '%s' does not exist", IMLIB_LOAD_ERROR_FILE_DOES_NOT_EXIST},
  {"file '%s' is a directory", IMLIB_LOAD_ERROR_FILE_IS_DIRECTORY},
  {"permission denied to read file '%s'", IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_READ},
  {"no loader for the file format used in file '%s'", IMLIB_LOAD_ERROR_NO_LOADER_FOR_FILE_FORMAT},
  {"path for file '%s' is too long", IMLIB_LOAD_ERROR_PATH_TOO_LONG},
  {"a component of path '%s' does not exist", IMLIB_LOAD_ERROR_PATH_COMPONENT_NON_EXISTANT},
  {"a component of path '%s' is not a directory", IMLIB_LOAD_ERROR_PATH_COMPONENT_NOT_DIRECTORY},
  {"path '%s' has too many symbolic links", IMLIB_LOAD_ERROR_TOO_MANY_SYMBOLIC_LINKS},
  {"ran out of file descriptors trying to access file '%s'", IMLIB_LOAD_ERROR_OUT_OF_FILE_DESCRIPTORS},
  {"denied write permission for file '%s'", IMLIB_LOAD_ERROR_PERMISSION_DENIED_TO_WRITE},
  {"out of disk space writing to file '%s'", IMLIB_LOAD_ERROR_OUT_OF_DISK_SPACE},
  {(const char *)NULL, (Imlib_Load_Error) 0}
};

static void print_imlib_load_error (Imlib_Load_Error err, const char *filepath) {
  int i;
  for (i = 0; err_strings[i].fmtstr != NULL; i++) {
    if (err == err_strings[i].errno) {
	printf("Screenshot Error (%d): ",err);
	printf(err_strings[i].fmtstr,filepath);
	printf("\n");
      return ;
    }
  }
  /* Unrecognised error */
    printf("Screenshot Error (%d): unrecognized error accessing file '%s'\n",err,filepath);
  return ;
}

// use imlib2 to save screenshot to disk. expects argb (bgra on little-endian) format.
// imlib2 determines output format by filename extension
// if we pass anything to output_width/height it will be scaled to that size
bool write_screenshot(const char *filename, const uint8_t *inbuf,
                      int width, int height, int output_width, int output_height)
{
    Imlib_Image im = imlib_create_image_using_data(width, height, (unsigned int *)inbuf);
    if (!im) {
        printf("Failed to create imlib image for screenshot\n");
        return false;
    }

    imlib_context_set_image(im);
    imlib_context_set_anti_alias(0);
    imlib_context_set_dither(0);
    imlib_context_set_blend(0);

    Imlib_Image scaled = im;

    if (output_width > 0 && output_height > 0) {
        scaled = imlib_create_cropped_scaled_image(
            0, 0,
            width, height,
            output_width, output_height
        );

        if (!scaled) {
            imlib_context_set_image(im);
            imlib_free_image_and_decache();
            printf("Failed to scale image for saving\n");
            return false;
        }

        imlib_context_set_image(scaled);
        imlib_context_set_anti_alias(0);
        imlib_context_set_dither(0);
        imlib_context_set_blend(0);
    }

    Imlib_Load_Error error;
    imlib_save_image_with_error_return(getFullPath(filename), &error);

    if (scaled && scaled != im) {
        imlib_context_set_image(scaled);
        imlib_free_image_and_decache();
    }

    if (im) {
        imlib_context_set_image(im);
        imlib_free_image_and_decache();
    }
    
    if (error != IMLIB_LOAD_ERROR_NONE) {
        print_imlib_load_error(error, filename);
        return false;
    }

    return true;
}

static void save_screenshot(int do_rescale, int base_width, int base_height, int scaled_width, int scaled_height, unsigned char *outputbuf, char *filename) {
    bool success = false;
    if (do_rescale)
    {
        printf("rescaling screenshot from %dx%d to %dx%d\n", base_width, base_height, scaled_width, scaled_height);
        success = write_screenshot(filename, outputbuf, base_width, base_height, scaled_width, scaled_height);
    }
    else
    {
        printf("saving screenshot at native res %dx%d\n", base_width, base_height);
        success = write_screenshot(filename, outputbuf, base_width, base_height);
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
	
    const char *extension;

    if (!strcasecmp(cfg.screenshot_image_format, "png"))
    {
        extension = ".png";
    }
    else if (!strcasecmp(cfg.screenshot_image_format, "bmp"))
    {   
        extension = ".bmp";
    } else {
        printf("Unknown screenshot image format in config: %s; defaulting to PNG\n", cfg.screenshot_image_format);
        extension = ".png";
    }

    mister_scaler_read(ms, screenshot_outputbuf);
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
        save_screenshot(do_rescale, base_width, base_height, scaled_width, scaled_height, screenshot_outputbuf, filename_copy);
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