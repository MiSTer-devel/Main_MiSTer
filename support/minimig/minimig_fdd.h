#ifndef __MINIMIG_FDD_H__
#define __MINIMIG_FDD_H__

#include "../../file_io.h"

// floppy disk interface defs
#define CMD_RDTRK 0x01
#define CMD_WRTRK 0x02

// floppy status
#define DSK_INSERTED	0x001   /*disk is inserted*/
#define DSK_WRITABLE	0x010   /*disk is writable*/
#define DSK_FLUXMODE	0x100   /*disk data is in flux mode*/
// ex_status flags
#define DSKEX_FLUXDENSITY 0x001 /*disk data is flux density mode*/

#define FLUXMODE_NONE      0
#define FLUXMODE_RAWFLUX   1
#define FLUXMODE_DENSITY   2

#define MAX_TRACKS (83*2)


#define FLUX_CLOCK_SPEED_HZ  28687500LL
#define FLUX_CLOCK_TICK ((long long)(1000000000/FLUX_CLOCK_SPEED_HZ))
#define FLUX_HALF_CLOCK_TICK ((long long)(FLUX_CLOCK_TICK/2))
#define FLUX_CLOCK_TICK_1_5 ((long long)(FLUX_CLOCK_TICK * 1.5))
#define FLUX_CLOCK_TICK_2_5 ((long long)(FLUX_CLOCK_TICK * 2.5))
#define FLUX_MAX_WAITING ((long long)(255 * FLUX_CLOCK_TICK))

class FluxFile {
private:
	char tmpFilename[32];
protected:
	// Open Flux based file
	virtual bool _openFile(const char* filename) = 0;
	virtual void _closeFile() = 0;
public:
	FluxFile();

	// Open Flux based file
	virtual bool openFile(const char* filename);
	virtual void closeFile();

	// Is this available to be read from
	virtual bool fluxReady() = 0;

	// Flux mode used by this file, FLUXMODE_RAWFLUX or FLUXMODE_DENSITY
	virtual uint8_t fluxMode() = 0;

	// Change active track (this includes the head)
	virtual bool selectTrack(uint32_t track) = 0;

	// Fills the buffer with noise
	virtual bool fluxDummyRead(uint16_t* outputBuffer, uint32_t numWords) = 0;

	// Read some flux data from the file
	virtual bool fluxRead(uint16_t* outputBuffer, uint32_t numWords) = 0;
	// This data just read wasn't used. We dont want to go out of sync.
	virtual void unFluxRead(const uint16_t* inputBuffer, uint32_t numWords) = 0;

	virtual uint32_t firstTrack() = 0;
	virtual uint32_t lastTrack() = 0;
	virtual uint32_t numHeads() = 0;

	virtual ~FluxFile() {  };
};

typedef struct
{
	fileTYPE      file;
	uint16_t      status; /*status of floppy*/
	uint16_t      ex_status; /*extra status of floppy*/
	unsigned char tracks; /*number of tracks*/
	unsigned char sector_offset; /*sector offset to handle tricky loaders*/
	unsigned char track; /*current track*/
	unsigned char track_prev; /*previous track*/
	char          name[1024]; /*floppy name*/
	uint16_t	  lastBit;   /* last mfm word used for clock bit calculation */
	FluxFile*     fluxFile;  /* flux reader class */
} adfTYPE;

extern unsigned char drives;
extern adfTYPE df[4];

void UpdateDriveStatus(void);
void HandleFDD(unsigned char c1, unsigned char c2);
void InsertFloppy(adfTYPE *drive, char* path);

#endif

