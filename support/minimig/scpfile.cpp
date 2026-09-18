
#include "scpfile.h"
#include <algorithm>
#include <utility>
#include <cstring>
#include <stdio.h>
#include "../../debug.h"

#define BITFLAG_INDEX		0
#define BITFLAG_96TPI		1
#define BITFLAG_NORMALISED  3
#define BITFLAG_EXTENDED    6
#define BITFLAG_FLUXCREATOR 7

#define BUFFER_SIZE_IN_WORDS 2048

// RobSmithDev

// Actually read the file
bool SCPFile::readSCPFile() {
	if (!m_file.filp) return false;

	SCPFileHeader header;
	if (!FileReadAdv(&m_file, &header, sizeof(header))) return false;
	if ((header.headerSCP[0] != 'S') || (header.headerSCP[1] != 'C') || (header.headerSCP[2] != 'P')) return false;

	m_fluxMultiplier = (header.timeBase + 1) * 25;
	m_firstTrack = header.startTrack;
	m_lastTrack = header.endTrack;
	m_numRevolutions = header.numRevolutions;
	m_numHeads = header.numHeads;
	m_currentRevolution = 0;
	if (m_lastTrack >= 168) return false;

	// only support 16-bit data
	if (!((header.bitcellEncoding == 0) || (header.bitcellEncoding == 16))) return false;
	m_revolutions.resize(m_numRevolutions);

	// Read the offsets table	
	uint32_t trackOffsets[168];

	if (!FileReadAdv(&m_file, trackOffsets, sizeof(uint32_t) * 168)) return false;	

	// Prepare track areas
	for (uint32_t trk = m_firstTrack; trk <= m_lastTrack; trk++) {
		m_tracks.insert(std::make_pair(trk, trackOffsets[trk]));
	}
	
	m_currentTrack = m_lastTrack + 1;
	return selectTrack(m_firstTrack);
} 

// Fills the buffer with noise
bool SCPFile::fluxDummyRead(uint16_t* outputBuffer, uint32_t numWords) {
	m_rewindBuffer.clear();
	static uint8_t x = 1;
	while (numWords) {
		x = (x >> 1) ^ (-(x & 1) & 0xB8);
		*outputBuffer = (m_fakeCounter ? convertTime(2000+((x & 7) * 1000)) : 0) | (convertTime(2000+(((x>>4) & 7) * 1000))<<8);
		outputBuffer++;
		numWords--;
		if (m_fakeCounter++ > 40000) m_fakeCounter = 0;
	}
	return true;
}


// Change active track (this includes the head)
bool SCPFile::selectTrack(uint32_t track) {
	if (!fluxReady()) return false;
	if (track < m_firstTrack) return false;
	if (track > m_lastTrack) return false;
	if (track == m_currentTrack) return true;
	m_currentTrack = 0xffff;
	m_rewindBuffer.clear();

	auto trk = m_tracks.find(track);
	if (trk == m_tracks.end()) return false;

	// This means no data
	if (trk->second == 0) return false;

	// Goto the track data
	if (!FileSeek(&m_file, trk->second, SEEK_SET)) return false;

	SCPTrackHeader header;
	if (!FileReadAdv(&m_file, &header, sizeof(header))) return false;
	if (header.trackNumber != track) return false;
	if (m_revolutions.size() < 1) return false;

	const uint64_t oldDataLength = m_revolutions[m_currentRevolution].trackLength;

	if (!FileReadAdv(&m_file, m_revolutions.data(), sizeof(SCPTrackRevolution) * m_numRevolutions)) return false;	
	m_currentTrackOffset = trk->second;
	m_bufferPos = 0;

	// Try to roughly get this positioned where it should be so spinning the disk is roughly right
	if ((m_samplesRemainingInRevolution) && (oldDataLength) && (oldDataLength > m_samplesRemainingInRevolution)) {
		const uint64_t samplesUsed = oldDataLength - m_samplesRemainingInRevolution;
		uint64_t newSamplePos = (samplesUsed * m_revolutions[m_currentRevolution].trackLength) / oldDataLength;
		if (newSamplePos >= m_revolutions[m_currentRevolution].trackLength - 1) {
			newSamplePos = 0;
			m_currentRevolution = (m_currentRevolution + 1) % m_numRevolutions;
		}		
		
		// Accurate looping at revolution boundary requires exact total of flux transitions
		m_timeRemainingInRevolution = m_revolutions[m_currentRevolution].indexTime;
		m_samplesRemainingInRevolution = m_revolutions[m_currentRevolution].trackLength - newSamplePos;
		m_bufferRemaining = 0;
		
		// It's slower but more accurate
		if (!FileSeek(&m_file, m_currentTrackOffset + m_revolutions[m_currentRevolution].dataOffset, SEEK_SET)) return false;
		
		uint16_t* dataRead = m_buffer;

		while (newSamplePos--) {
			// Check buffering
			if (m_bufferRemaining < 2) {
				m_bufferRemaining = FileReadAdv(&m_file, m_buffer, sizeof(uint16_t) * BUFFER_SIZE_IN_WORDS);
				if (m_bufferRemaining < 2) return false;
				m_bufferPos = 0;
				dataRead = m_buffer;
			}
			const uint16_t value = (*dataRead << 8) | (*dataRead >> 8);   // endian swap
			dataRead++;
			m_bufferPos++;
			m_bufferRemaining -= 2;
			if (value == 0) m_timeRemainingInRevolution -= 65536LL; else m_timeRemainingInRevolution -= (uint64_t)value;
		}		
		m_timeRemainingInRevolution *= (uint64_t)m_fluxMultiplier;
	} else {
		m_currentRevolution = m_numRevolutions - 1;
		m_samplesRemainingInRevolution = 0;
		m_timeRemainingInRevolution = 0;
		m_bufferRemaining = 0;
	}

	m_fluxDelayStillWaiting = 0;
	m_lastTime = 0LL;	
	m_currentTrack = track;
	return true;
}

// This data just read wasn't used. We dont want to go out of sync.
void SCPFile::unFluxRead(const uint16_t* inputBuffer, uint32_t numWords) {
	if (!fluxReady()) return;
	m_rewindBuffer.resize(numWords);
	memcpy(m_rewindBuffer.data(), inputBuffer, numWords * sizeof(uint16_t));
}

inline uint8_t SCPFile::convertTime(int64_t time) {
	return (uint8_t)std::min(std::max(3LL, ((time + FLUX_HALF_CLOCK_TICK) * FLUX_CLOCK_SPEED_HZ) / 1000000000LL), 255LL);
}

// Read some flux data from the file
bool SCPFile::fluxRead(uint16_t* outputBuffer, uint32_t numWords) {
	if (m_currentTrack < m_firstTrack) return false;
	if (m_currentTrack > m_lastTrack) return false;
	if (m_numRevolutions < 1) return false;
	if (!fluxReady()) return false;

	// Rewind!
	if (m_rewindBuffer.size() == numWords) {
		memcpy(outputBuffer, m_rewindBuffer.data(), numWords * sizeof(uint16_t));
		m_rewindBuffer.clear();
		return true;
	}

	uint16_t* dataRead = m_buffer + m_bufferPos;
	uint8_t* bytesOut = (uint8_t*)outputBuffer;
	uint32_t bytesLeft = numWords * 2UL;
	uint32_t errorCount = 0;

	while (bytesLeft) {		
		// Encode extra delays
		if (m_fluxDelayStillWaiting) {
			while (m_fluxDelayStillWaiting > FLUX_MAX_WAITING) {  // approx 255				
				if (!fluxReady()) return false;
				// 1 byte left, encode a single tick delay
				if (bytesLeft == 1) {
					*bytesOut = 1; bytesLeft--; bytesOut++;
					m_fluxDelayStillWaiting -= FLUX_CLOCK_TICK;
					if (m_fluxDelayStillWaiting < 1) m_fluxDelayStillWaiting = 1;
					return true;
				}
				// 2 bytes left
				*bytesOut = 2;   // signal the next byte is a delay only, not transition
				bytesLeft--; bytesOut++;
				m_fluxDelayStillWaiting -= FLUX_CLOCK_TICK;    // even this uses one tick
				uint8_t convertedTime = convertTime(m_fluxDelayStillWaiting);
				*bytesOut = convertedTime;
				bytesOut++; bytesLeft--;
				m_fluxDelayStillWaiting -= ((int64_t)convertedTime) * FLUX_CLOCK_TICK;
				if (m_fluxDelayStillWaiting < 1) m_fluxDelayStillWaiting = 1; 
				if (bytesLeft < 1) return true;
			}
			// Encode flux transition			
			if (bytesLeft) {
				uint8_t convertedTime = convertTime(m_fluxDelayStillWaiting);
				m_fluxDelayStillWaiting -= ((int64_t)convertedTime) * FLUX_CLOCK_TICK;
				*bytesOut = convertedTime;
				bytesOut++; bytesLeft--;
				if (m_fluxDelayStillWaiting < 0) m_fluxDelayStillWaiting = 0;
			}
			else return true;
			m_lastTime = 0;// m_fluxDelayStillWaiting;
            m_fluxDelayStillWaiting = 0;
		}
		
		// Buffer some data from disk?
		if (m_samplesRemainingInRevolution < 1) {			
			// Handle residual timing - FLUX_CLOCK_TICK is our minimum duration, but INDEX will eat up one tick
			while (m_timeRemainingInRevolution > FLUX_CLOCK_TICK) {
				if (!fluxReady()) return false;						
				// 1 byte left, encode a single tick delay
				if (bytesLeft == 1) {
					*bytesOut = 1; bytesLeft--; bytesOut++;
					m_timeRemainingInRevolution -= FLUX_CLOCK_TICK;
					return true;
				}
				// More than 1 byte remaining!
				// Less than 2 ticks (1.5) worth of time remaining?
				if (m_timeRemainingInRevolution < FLUX_CLOCK_TICK_1_5) {
					*bytesOut = 1;   // Single delay
					bytesLeft--; bytesOut++;
					m_timeRemainingInRevolution = 0;
				}
				else {
					// Less than 3 ticks (2.5 worth)
					if (m_timeRemainingInRevolution < FLUX_CLOCK_TICK_2_5) {
						*bytesOut = 1;    bytesOut++;
						*bytesOut = 1;    bytesOut++;
						bytesLeft -= 2;
						m_timeRemainingInRevolution = 0;
					}
					else {
						// 3 or more ticks
						*bytesOut = 2;   // signal the next byte is a delay only, not transition
						bytesLeft--; bytesOut++;
						m_timeRemainingInRevolution -= FLUX_CLOCK_TICK;    // even this uses one tick
						uint8_t convertedTime = convertTime(m_timeRemainingInRevolution);
						*bytesOut = convertedTime;
						bytesOut++; bytesLeft--;
						m_timeRemainingInRevolution -= ((int64_t)convertedTime) * FLUX_CLOCK_TICK;
						if (convertedTime <=4) m_timeRemainingInRevolution = 0; // just incase
					}
				}
			
				// Stop if out of data space
				if (bytesLeft < 1) return true;
			}
			// Just incase.
			if (bytesLeft < 1) return true;
			//
			if (!fluxReady()) return false;
			// Skip to next revolution
			m_currentRevolution = (m_currentRevolution + 1) % m_numRevolutions;
			fdd_debugf("Changing to revolution %i of %i on track %i\n",m_currentRevolution,m_numRevolutions,m_currentTrack); 
			if (!FileSeek(&m_file, m_currentTrackOffset + m_revolutions[m_currentRevolution].dataOffset, SEEK_SET)) {
				fdd_debugf("Failed to seek to revolution\n");
				return false;
			}
			m_samplesRemainingInRevolution = m_revolutions[m_currentRevolution].trackLength;
			m_timeRemainingInRevolution += m_revolutions[m_currentRevolution].indexTime * m_fluxMultiplier;
			m_bufferRemaining = 0;
			
			*bytesOut = 0; // 0 means index
			bytesOut++;
			bytesLeft--;
			if (bytesLeft < 1) return true;
			m_lastTime = -FLUX_CLOCK_TICK;   //nS - Account for the delay caused by this 
		}
		// Check buffering
		if (m_bufferRemaining < 2) {
			if (!fluxReady()) return false;
			m_bufferRemaining = FileReadAdv(&m_file, m_buffer, sizeof(uint16_t) * BUFFER_SIZE_IN_WORDS);
			if (m_bufferRemaining < 2) {
				m_samplesRemainingInRevolution = 0;
				errorCount++;
				if (errorCount > m_numRevolutions) return false;
				continue;
			}
			m_bufferPos = 0;
			dataRead = m_buffer;
		}

		// Output flux transition
		const uint16_t value = (*dataRead << 8) | (*dataRead >> 8);   // endian swap
		dataRead++;
		m_bufferPos++;
		m_bufferRemaining -= 2;
		m_samplesRemainingInRevolution--;

		if (value == 0) {
			const int64_t t = 65536LL * m_fluxMultiplier;
			m_lastTime += t;
			m_timeRemainingInRevolution -= t;
		}
		else {
			const int64_t t = (uint64_t)value * m_fluxMultiplier;
			m_timeRemainingInRevolution -= t;
			m_lastTime += t;
			m_fluxDelayStillWaiting = std::max(1LL,m_lastTime);			
			m_lastTime = 0;			
		}
	} 

	return true;
}

// Open SCP file
bool SCPFile::_openFile(const char* filename) {
	if (!m_buffer) return false;

	if (!FileOpen(&m_file, filename, 0)) return false;

	if (!readSCPFile()) {
		closeFile();
		return false;
	}

	if (m_tracks.size()) return true;
	closeFile();
	return false;
}

bool SCPFile::fluxReady() {
	return m_currentTrack != 0xFFFF;
}

void SCPFile::_closeFile() {
	m_currentTrack = 0xFFFF;
	FileClose(&m_file);
	memset(&m_file, 0, sizeof(m_file));
	m_firstTrack = 0;
	m_lastTrack = 80 * 2;
	m_samplesRemainingInRevolution = 0;
	m_fluxMultiplier = 25;
	m_numRevolutions = 1;
	m_revolutions.clear();
	m_tracks.clear();
	m_rewindBuffer.clear();
}

SCPFile::SCPFile() {
	memset(&m_file, 0, sizeof(m_file));	
	closeFile();
	m_buffer = (uint16_t*)calloc(BUFFER_SIZE_IN_WORDS, sizeof(uint16_t));
}

SCPFile::~SCPFile() {
	closeFile();
	if (m_buffer) free(m_buffer);
}
