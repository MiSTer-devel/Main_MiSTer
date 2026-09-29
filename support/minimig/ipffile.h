#ifndef __MINIMIG_IPF_H__
#define __MINIMIG_IPF_H__

#ifdef WINDOWS
#include "file_io.h"
#else
#include "../../file_io.h"
#include "minimig_fdd.h"
#endif
#include <stdint.h>
#include <vector>
#include <unordered_map>

// Notes on the CAPSAPI library: Nothing wrong with including it. Also, according to their licence, the compiled binary can be distributed freely as long as no money exchanges hands.
// RobSmithDev

class IPFFile : public FluxFile {
private:
	signed long m_capsImageIndex;
	bool m_isOpen;

	uint32_t m_minHead, m_maxHead, m_minCylinder, m_maxCylinder;	
	std::vector<uint16_t> m_rewindBuffer;	
	uint16_t m_currentTrack;    	
	uint64_t m_bufferPos;	
	uint8_t* m_trackBuffer;
	unsigned long* m_densityBuffer;
	uint32_t m_trackBufferLength;
	uint32_t m_densityBufferLength;
	uint64_t m_densityCompensation;
	int32_t m_previousTrack;
	int64_t m_fluxTime;
	uint32_t m_overlapBit;	
	bool m_indexSaved = false;
	bool m_isFlakey;	
	int m_currentRevolution;
	int m_nextRevolution;
	uint32_t m_trackType;
	uint16_t m_fakeCounter = 0;

	// Provides a basic decoding. just enough
	bool decodeTrack(uint32_t track);

protected:
	// Open SCP file
	virtual bool _openFile(const char* filename) override;
	virtual void _closeFile() override;

public:
	IPFFile();
    virtual ~IPFFile();


	virtual bool fluxReady() override;

	// Flux mode used by this file, FLUXMODE_RAWFLUX or FLUXMODE_DENSITY
	virtual uint8_t fluxMode() { return FLUXMODE_DENSITY; };
		
	// Change active track (this includes the head)
	virtual bool selectTrack(uint32_t track) override;

	// Fills the buffer with noise
	virtual bool fluxDummyRead(uint16_t* outputBuffer, uint32_t numWords) override;

	virtual bool fluxRead(uint16_t* outputBuffer, uint32_t numWords) override;

	// This data just read wasn't used. We dont want to go out of sync.
	virtual void unFluxRead(const uint16_t* inputBuffer, uint32_t numWords) override;
	
	virtual uint32_t firstTrack() override;
	virtual uint32_t lastTrack() override;
	virtual uint32_t numHeads() override;
};


#endif // __MINIMIG_IPF_H__