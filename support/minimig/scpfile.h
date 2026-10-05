#ifndef __MINIMIG_SCP_H__
#define __MINIMIG_SCP_H__

#include "../../file_io.h"
#include "minimig_fdd.h"
#include <stdint.h>
#include <vector>
#include <unordered_map>

// RobSmithDev

class SCPFile : public FluxFile {
private:

	/* Taken from https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt
	This information is copyright(C) 2012 - 2020 By Jim Drew. Permission is granted
	for inclusion with any source code when keeping this copyright notice.
	*/
	struct SCPFileHeader {
		int8_t	headerSCP[3];
		uint8_t	version;
		uint8_t	diskType;
		uint8_t	numRevolutions;
		uint8_t	startTrack;
		uint8_t endTrack;
		uint8_t	flags;
		uint8_t	bitcellEncoding;   // 0=16 bits per sample, 
		uint8_t	numHeads;
		uint8_t timeBase;          // Resolution. Time in ns = (timeBase+1)*25
		uint32_t checksum;
	}  __attribute__((packed));

	struct SCPTrackHeader {
		int8_t headerTRK[3];
		uint8_t	trackNumber;
	} __attribute__((packed));

	struct SCPTrackRevolution {
		uint32_t indexTime;		// Time in NS/25 for this revolution
		uint32_t trackLength;	// Number of bit-cells in this revolution
		uint32_t dataOffset;	// From the start of SCPTrackHeader 
	} __attribute__((packed));

	std::vector<uint16_t> m_rewindBuffer;

	uint32_t m_firstTrack;
	uint32_t m_lastTrack;
	uint32_t m_fluxMultiplier;
	uint32_t m_numHeads;
	uint32_t m_numRevolutions;
	
	// Data for the currently selected track
	std::vector<SCPTrackRevolution> m_revolutions;
	
	fileTYPE m_file;

    std::unordered_map<uint32_t, uint32_t> m_tracks;

	uint32_t m_samplesRemainingInRevolution;
	uint32_t m_currentRevolution;
	uint32_t m_currentTrack;
	uint16_t* m_buffer;
	uint32_t m_bufferPos;
	uint32_t m_bufferRemaining;
	uint32_t m_currentTrackOffset;
	int64_t m_lastTime;
	int64_t m_timeRemainingInRevolution;
	int64_t m_fluxDelayStillWaiting;
	uint16_t m_fakeCounter = 0;

    // Actually read the file
    bool readSCPFile();

	uint8_t convertTime(int64_t time);

protected:
	// Open SCP file
	virtual bool _openFile(const char* filename) override;
	virtual void _closeFile() override;

public:
	SCPFile();
    virtual ~SCPFile();


	virtual bool fluxReady() override;

	// Flux mode used by this file, FLUXMODE_RAWFLUX or FLUXMODE_DENSITY
	virtual uint8_t fluxMode() { return FLUXMODE_RAWFLUX; };
		
	// Change active track (this includes the head)
	virtual bool selectTrack(uint32_t track) override;

	// Fills the buffer with noise
	bool fluxDummyRead(uint16_t* outputBuffer, uint32_t numWords) override;

	// Read some flux data from the file
	virtual bool fluxRead(uint16_t* outputBuffer, uint32_t numWords) override;

	// This data just read wasn't used. We dont want to go out of sync.
	virtual void unFluxRead(const uint16_t* inputBuffer, uint32_t numWords) override;
	
	virtual uint32_t firstTrack() override { return m_firstTrack; };
	virtual uint32_t lastTrack() override { return m_lastTrack; };
	virtual uint32_t numHeads() override { return m_numHeads; };
};


#endif