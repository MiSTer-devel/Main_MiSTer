// Notes on the CAPSAPI library: Nothing wrong with including it. Also, according to their licence, the compiled binary can be distributed freely as long as no money exchanges hands.
// RobSmithDev

#include "ipffile.h"
#include "capsapi/Comtype.h"
#include "capsapi/CapsAPI.h"
#ifdef WINDOWS
#include "file_io.h"
#else
#include "../../file_io.h"
#include "../../debug.h"
#endif
#include <algorithm>
#include <utility>
#include <cstring>
#include <stdio.h>
#include <stdarg.h>
#ifdef WINDOWS
#define fdd_debugf(x, ...) 
#else
#include <dlfcn.h>
#include "debug.h"
#endif


#ifdef WINDOWS
#define getRootDir() ""
#define CAPSCALL __cdecl
HMODULE libHandle = 0;
#define GETFUNC GetProcAddress
#define dlclose FreeLibrary
#else
#define CAPSCALL
void* libHandle = nullptr;
#define GETFUNC dlsym
#endif

static bool ipfcapsAttempted = false;


typedef SDWORD(CAPSCALL* CAPSINIT)(void);
static CAPSINIT pCAPSInit;
typedef SDWORD(CAPSCALL* CAPSADDIMAGE)(void);
static CAPSADDIMAGE pCAPSAddImage;
typedef SDWORD(CAPSCALL* CAPSREMIMAGE)(SDWORD);
static CAPSREMIMAGE pCAPSRemImage;
typedef SDWORD(CAPSCALL* CAPSLOCKIMAGE)(SDWORD, PCHAR);
static CAPSLOCKIMAGE pCAPSLockImage;
typedef SDWORD(CAPSCALL* CAPSUNLOCKIMAGE)(SDWORD);
static CAPSUNLOCKIMAGE pCAPSUnlockImage;
typedef SDWORD(CAPSCALL* CAPSLOADIMAGE)(SDWORD, UDWORD);
static CAPSLOADIMAGE pCAPSLoadImage;
typedef SDWORD(CAPSCALL* CAPSGETIMAGEINFO)(PCAPSIMAGEINFO, SDWORD);
static CAPSGETIMAGEINFO pCAPSGetImageInfo;
typedef SDWORD(CAPSCALL* CAPSLOCKTRACK)(void*, SDWORD, UDWORD, UDWORD, UDWORD);
static CAPSLOCKTRACK pCAPSLockTrack;
typedef SDWORD(CAPSCALL* CAPSUNLOCKTRACK)(SDWORD id, UDWORD cylinder, UDWORD head);
static CAPSUNLOCKTRACK pCAPSUnlockTrack;
typedef SDWORD(CAPSCALL* CAPSUNLOCKALLTRACKS)(SDWORD);
static CAPSUNLOCKALLTRACKS pCAPSUnlockAllTracks;
typedef SDWORD(CAPSCALL* CAPSSETREVOLUTION)(SDWORD, UDWORD);
static CAPSSETREVOLUTION pCAPSSetRevolution;
typedef SDWORD(CAPSCALL* CAPSGETINFO)(PVOID, SDWORD, UDWORD, UDWORD, UDWORD, UDWORD);
static CAPSGETINFO pCAPSGetInfo;


//#define LOCK_FLAGS (DI_LOCK_INDEX    |DI_LOCK_DENVAR   |DI_LOCK_DENAUTO  |DI_LOCK_DENNOISE |DI_LOCK_NOISE    |DI_LOCK_NOISEREV |DI_LOCK_UPDATEFD |DI_LOCK_TYPE     |DI_LOCK_TRKBIT  |DI_LOCK_OVLBIT)
#define LOCK_FLAGS ((DI_LOCK_DENVAR | DI_LOCK_UPDATEFD | DI_LOCK_TYPE | DI_LOCK_TRKBIT | DI_LOCK_OVLBIT))

// DI_LOCK_SETWSEED only for LockTrack calls, never for LoadImage
#define LOCK_FLAGS_SEEDED (LOCK_FLAGS | DI_LOCK_SETWSEED)

#define DensityToNS(densityValue) ((2000ULL * densityValue) / 1000ULL)

void caps_free() {
	if (libHandle) dlclose(libHandle);
	libHandle = nullptr;
}

bool caps_init(void)
{
	//	if ((ipfcapsAttempted) && (!libHandle)) return false;
	if (libHandle) return true;
	ipfcapsAttempted = true;
#ifdef WINDOWS
	libHandle = LoadLibrary(L"CAPSImg.DLL");
#else
	libHandle = dlopen("/media/fat/linux/libcapsimg.so", RTLD_NOW);
	// Possible alternative paths
	if (!libHandle) libHandle = dlopen("/media/fat/libcapsimg.so", RTLD_NOW);
	if (!libHandle) libHandle = dlopen("/media/fat/linux/CAPSIMG/libcapsimg.so", RTLD_NOW);
#endif
	if (!libHandle) {
		fdd_debugf("Unable to load /media/fat/linux/libcapsimg.so error %s", dlerror());
		return false;
	}

	if (GETFUNC(libHandle, "CAPSLockImageMemory") == 0 || GETFUNC(libHandle, "CAPSGetVersionInfo") == 0) {
		caps_free();
		return false;
	}
	pCAPSInit = (CAPSINIT)GETFUNC(libHandle, "CAPSInit");
	pCAPSAddImage = (CAPSADDIMAGE)GETFUNC(libHandle, "CAPSAddImage");
	pCAPSRemImage = (CAPSREMIMAGE)GETFUNC(libHandle, "CAPSRemImage");
	pCAPSLockImage = (CAPSLOCKIMAGE)GETFUNC(libHandle, "CAPSLockImage");
	pCAPSUnlockImage = (CAPSUNLOCKIMAGE)GETFUNC(libHandle, "CAPSUnlockImage");
	pCAPSLoadImage = (CAPSLOADIMAGE)GETFUNC(libHandle, "CAPSLoadImage");
	pCAPSGetImageInfo = (CAPSGETIMAGEINFO)GETFUNC(libHandle, "CAPSGetImageInfo");
	pCAPSLockTrack = (CAPSLOCKTRACK)GETFUNC(libHandle, "CAPSLockTrack");
	pCAPSUnlockTrack = (CAPSUNLOCKTRACK)GETFUNC(libHandle, "CAPSUnlockTrack");
	pCAPSUnlockAllTracks = (CAPSUNLOCKALLTRACKS)GETFUNC(libHandle, "CAPSUnlockAllTracks");
	// Optional - check before use in case of older library
	pCAPSSetRevolution = (CAPSSETREVOLUTION)GETFUNC(libHandle, "CAPSSetRevolution");
	pCAPSGetInfo = (CAPSGETINFO)GETFUNC(libHandle, "CAPSGetInfo");

	if (pCAPSInit() != imgeOk) {
		caps_free();
		return false;
	}

	return true;
}


uint32_t IPFFile::firstTrack() { return m_minCylinder * numHeads(); }
uint32_t IPFFile::lastTrack() { return (m_maxCylinder * numHeads()) - 1; }
uint32_t IPFFile::numHeads() { return (m_maxHead - m_minHead) + 1; };

// Converts the density value into 2us ticks at 28mhz that the amiga core uses
uint8_t densityToTicks(uint32_t density) {
	return (uint8_t)std::min(std::max(1, (int)((density * 2 * FLUX_CLOCK_SPEED_HZ) / 1000000000LL)), 255);
}


// This data just read wasn't used. We dont want to go out of sync.
void IPFFile::unFluxRead(const uint16_t* inputBuffer, uint32_t numWords) {
	if (!fluxReady()) return;
	m_rewindBuffer.resize(numWords);
	memcpy(m_rewindBuffer.data(), inputBuffer, numWords * sizeof(uint16_t));
}

// Open IPF file
bool IPFFile::_openFile(const char* filename) {
	if (!libHandle) return false;

	static char full_path[2100];

	if (filename[0] != '/') {
#ifdef WINDOWS
		sprintf(full_path, "%s", filename);
#else
		sprintf(full_path, "%s/%s", getRootDir(), filename);
#endif
	}
	else {
#ifdef WINDOWS
		sprintf(full_path, "%s", filename);
#else
		sprintf(full_path, "%s", filename);
#endif
	}
	SDWORD result = pCAPSLockImage(m_capsImageIndex, (PCHAR)full_path);
	if (result != imgeOk) {
		//fdd_debugf("Unable to open %s (error %li)", filename, result);
		return false;
	}
	m_isOpen = true;

	CapsImageInfo info;
	if (pCAPSGetImageInfo(&info, m_capsImageIndex) != imgeOk) {
		closeFile();
		return false;
	}


	m_minHead = info.minhead;
	m_maxHead = info.maxhead;
	m_minCylinder = info.mincylinder;
	m_maxCylinder = info.maxcylinder;	

	if (pCAPSLoadImage(m_capsImageIndex, LOCK_FLAGS) != imgeOk) {
		closeFile();
		return false;
	}

	bool ret = selectTrack(firstTrack());
	if (!ret) {
		closeFile();
		return false;
	}
	return true;
}

// Change active track (this includes the head)
bool IPFFile::selectTrack(uint32_t track) {
	if (track == m_currentTrack) return true;
	if (track < firstTrack()) return false;
	if (track > lastTrack()) return false;
	m_currentTrack = 0xffff;
	m_rewindBuffer.clear();
	m_fluxTime = 0;
	uint32_t lastBufferPos = m_bufferPos;
	uint32_t lastBufferLength = m_trackBufferLength;

	// Reset revolution state on track change
	m_currentRevolution = 0;
	m_nextRevolution = 0;
	m_isFlakey = false;

	if (decodeTrack(track)) {
		// Skip to a position within the data to simulate the fact that the disk is still spinning		
		uint64_t newSamplePos = lastBufferLength ? (lastBufferPos * (m_trackBufferLength/8)) / (lastBufferLength/8) : 0;
		if (newSamplePos >= (m_trackBufferLength/8) - 1) newSamplePos = 0;
		m_bufferPos = newSamplePos;
		m_currentTrack = track;
		return true;
	}
	return false;
}

// Decode a track from the CAPS library
bool IPFFile::decodeTrack(uint32_t track) {
	CapsTrackInfoT2 trackInfo;
	memset(&trackInfo, 0, sizeof(trackInfo));
	trackInfo.type = 2;
	trackInfo.wseed = (UDWORD)rand();  // seed weak bits so they vary between reads
	m_bufferPos = 0;
	m_indexSaved = false;

	// Unlock previous track first, before any other library calls
	if (m_previousTrack >= 0) pCAPSUnlockTrack(m_capsImageIndex, m_previousTrack / numHeads(), m_minHead + (m_previousTrack % numHeads()));
	m_previousTrack = -1;

	// Set revolution AFTER unlocking previous track
	if (pCAPSSetRevolution) pCAPSSetRevolution(m_capsImageIndex, m_currentRevolution);

	// Load track - use seeded flags so wseed above is used for weak bit generation
	if (pCAPSLockTrack(&trackInfo, m_capsImageIndex, track / numHeads(), m_minHead + (track % numHeads()), LOCK_FLAGS_SEEDED) != imgeOk) return false;
	m_previousTrack = track;

	m_trackBuffer = trackInfo.trackbuf;
	m_densityBuffer = trackInfo.timebuf;
	m_trackBufferLength = trackInfo.tracklen;
	m_densityBufferLength = trackInfo.timelen;
	m_trackType = trackInfo.type;
	m_overlapBit = (trackInfo.overlap >= 0) ? (uint32_t)trackInfo.overlap : 0;

	// Check for flakey/weak-bit tracks (intentional revolution-to-revolution variation)
	m_isFlakey = (trackInfo.type & CTIT_FLAG_FLAKEY) != 0;

	// Get next revolution from library - must be queried AFTER LockTrack
	m_nextRevolution = m_currentRevolution;
	if (m_isFlakey && pCAPSGetInfo) {
		CapsRevolutionInfo revInfo;
		memset(&revInfo, 0, sizeof(revInfo));
		if (pCAPSGetInfo(&revInfo, m_capsImageIndex, track / numHeads(), m_minHead + (track % numHeads()), cgiitRevolution, 0) == imgeOk) {
			if (revInfo.max > 0)
				m_nextRevolution = revInfo.next;
		}
	}

	if (trackInfo.trackbuf) {
		if ((m_trackType & CTIT_MASK_TYPE) == ctitVar) {
			// Variable density track - density buffer contains absolute cell sizes,
			// not relative weights. Use raw values with no compensation.
			// This is critical for copy protection tracks (e.g. Rob Northen Copylock)
			// where cell timing is precisely crafted.
			m_densityCompensation = 1000ULL;
		}
		else {
			// Normal track - compensate density to fit a standard 200ms revolution
			uint64_t timetotal = 0;
			if (trackInfo.timebuf && trackInfo.timelen) {
				for (UDWORD byte = 0; byte < trackInfo.timelen; byte++) timetotal += trackInfo.timebuf[byte];
				timetotal = DensityToNS(timetotal) * 8ULL;
			}
			else timetotal = 200000000ULL;
			m_densityCompensation = timetotal ? (200000000000ULL) / timetotal : 1000ULL;
		}
	}
	else m_densityCompensation = 1000UL;

	return ((m_trackBufferLength > 0) && (m_trackBuffer)) || ((m_trackType & CTIT_MASK_TYPE) == ctitNoise);
}

void IPFFile::_closeFile() {
	m_bufferPos = 0;
	m_fluxTime = 0;
	m_previousTrack = -1;
	m_trackBuffer = NULL;
	m_densityBuffer = NULL;
	m_trackBufferLength = 0;
	m_densityBufferLength = 0;
	m_trackType = 0;
	m_isFlakey = false;
	m_currentRevolution = 0;
	m_nextRevolution = 0;
	if (m_isOpen) {
		pCAPSUnlockAllTracks(m_capsImageIndex);
		pCAPSUnlockImage(m_capsImageIndex);
		m_isOpen = false;
	}

	m_currentTrack = 0xffff;
	m_rewindBuffer.clear();
	m_minHead = 0;
	m_maxHead = 0;
	m_minCylinder = 0;
	m_maxCylinder = 0;
}

bool IPFFile::fluxReady() {
	return (m_currentTrack != 0xFFFF) && (((m_trackBufferLength > 0) && (m_trackBuffer)) || ((m_trackType & CTIT_MASK_TYPE) == ctitNoise));
}

// Fills the buffer with noise
bool IPFFile::fluxDummyRead(uint16_t* outputBuffer, uint32_t numWords) {
	m_rewindBuffer.clear();
	uint16_t d = densityToTicks(1000);

	static uint8_t x = 1;
	while (numWords) {
		x = (x >> 1) ^ (-(x & 1) & 0xB8);
		if (m_fakeCounter) *outputBuffer = d | ((x << 8) & 0xAA); else *outputBuffer = 0;
		outputBuffer++;
		numWords--;
		if (m_fakeCounter++ > 12500) m_fakeCounter = 0;
	}
	return true;
}


// Read some flux data from the file
bool IPFFile::fluxRead(uint16_t* outputBuffer, uint32_t numWords) {
	if (m_currentTrack == 0xFFFF) return false;
	if (((m_trackType & CTIT_MASK_TYPE) == ctitNoise) || (!m_trackBuffer) || (m_trackBufferLength < 1)) {
		m_rewindBuffer.clear();
		return fluxDummyRead(outputBuffer, numWords);
	}
	if (!fluxReady()) return false;

	if (m_currentTrack < firstTrack()) return false;
	if (m_currentTrack >= lastTrack()) return false;

	// Rewind!
	if (m_rewindBuffer.size() == numWords) {
		memcpy(outputBuffer, m_rewindBuffer.data(), numWords * sizeof(uint16_t));
		m_rewindBuffer.clear();
		return true;
	}
	
	while (numWords) {
		// index
		if ((m_bufferPos == m_overlapBit) && (!m_indexSaved)) {
			*outputBuffer = 0;
			outputBuffer++;
			numWords--;
			m_indexSaved = true;
		}

		if (numWords < 1) return true;

		UDWORD density = ((m_densityBuffer) && (m_densityBufferLength) && (m_bufferPos < m_densityBufferLength)) ? m_densityBuffer[m_bufferPos] : 1000ULL;
		
		*outputBuffer = densityToTicks(density)  | (m_trackBuffer[m_bufferPos] << 8);
		outputBuffer++;
		m_bufferPos++;
		if (m_bufferPos >= m_trackBufferLength  / 8) {
			if (m_isFlakey)
				m_currentRevolution = m_nextRevolution;
			if (!decodeTrack(m_previousTrack >= 0 ? m_previousTrack : m_currentTrack)) return false;
			m_bufferPos = 0;
		}
		numWords--;
	}

	return true;
}


IPFFile::IPFFile() {
	m_isOpen = false;
	if (caps_init()) m_capsImageIndex = pCAPSAddImage();
	closeFile();
}

IPFFile::~IPFFile() {
	closeFile();
	if (libHandle) pCAPSRemImage(m_capsImageIndex);
}