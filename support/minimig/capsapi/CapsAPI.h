#ifndef CAPSAPI_H
#define CAPSAPI_H

#define CAPS_FILEEXT "ipf"
#define CAPS_FILEPFX ".ipf"

// Flags provided for locking, in order:
//  0: re-align data as index synced recording
//  1: decode track to word aligned size
//  2: generate cell density for variable density tracks
//  3: generate density for automatically sized cells
//  4: generate density for unformatted cells
//  5: generate unformatted data
//  6: generate unformatted data, that changes each revolution
//  7: directly use source memory buffer supplied with LockImageMemory
//  8: flakey/weak data is created on one revolution, updated with each lock
//  9: ...Info.type holds the expected structure type
// 10: alternate density map as fractions
// 11: overlap position is in bits
// 12: tracklen is in bits, and the track buffer is bit sized
// 13: track overlap or weak data is never updated, just initialized
// 14: set weak bit generator seed value
#define DI_LOCK_INDEX    DF_0
#define DI_LOCK_ALIGN    DF_1
#define DI_LOCK_DENVAR   DF_2
#define DI_LOCK_DENAUTO  DF_3
#define DI_LOCK_DENNOISE DF_4
#define DI_LOCK_NOISE    DF_5
#define DI_LOCK_NOISEREV DF_6
#define DI_LOCK_MEMREF   DF_7
#define DI_LOCK_UPDATEFD DF_8
#define DI_LOCK_TYPE     DF_9
#define DI_LOCK_DENALT   DF_10
#define DI_LOCK_OVLBIT   DF_11
#define DI_LOCK_TRKBIT   DF_12
#define DI_LOCK_NOUPDATE DF_13
#define DI_LOCK_SETWSEED DF_14

#define CAPS_MAXPLATFORM 4
#define CAPS_MTRS 5

#define CTIT_FLAG_FLAKEY DF_31
#define CTIT_MASK_TYPE 0xff

#pragma pack(push, 1)

// decoded caps date.time
struct CapsDateTimeExt {
	UDWORD year;
	UDWORD month;
	UDWORD day;
	UDWORD hour;
	UDWORD min;
	UDWORD sec;
	UDWORD tick;
};

typedef struct CapsDateTimeExt* PCAPSDATETIMEEXT;

// library version information block
struct CapsVersionInfo {
	UDWORD type;     // library type
	UDWORD release;  // release ID
	UDWORD revision; // revision ID
	UDWORD flag;     // supported flags
};

typedef struct CapsVersionInfo* PCAPSVERSIONINFO;

// disk image information block
struct CapsImageInfo {
	UDWORD type;        // image type
	UDWORD release;     // release ID
	UDWORD revision;    // release revision ID
	UDWORD mincylinder; // lowest cylinder number
	UDWORD maxcylinder; // highest cylinder number
	UDWORD minhead;     // lowest head number
	UDWORD maxhead;     // highest head number
	struct CapsDateTimeExt crdt; // image creation date.time
	UDWORD platform[CAPS_MAXPLATFORM]; // intended platform(s)
};

typedef struct CapsImageInfo* PCAPSIMAGEINFO;

// disk track information block
struct CapsTrackInfo {
	UDWORD type;       // track type
	UDWORD cylinder;   // cylinder#
	UDWORD head;       // head#
	UDWORD sectorcnt;  // available sectors
	UDWORD sectorsize; // sector size
	UDWORD trackcnt;   // track variant count
	PUBYTE trackbuf;   // track buffer memory
	UDWORD tracklen;   // track buffer memory length
	PUBYTE trackdata[CAPS_MTRS]; // track data pointer if available
	UDWORD tracksize[CAPS_MTRS]; // track data size
	UDWORD timelen;  // timing buffer length
	PUDWORD timebuf; // timing buffer
};

typedef struct CapsTrackInfo* PCAPSTRACKINFO;

// disk track information block
struct CapsTrackInfoT1 {
	UDWORD type;       // track type
	UDWORD cylinder;   // cylinder#
	UDWORD head;       // head#
	UDWORD sectorcnt;  // available sectors
	UDWORD sectorsize; // sector size
	PUBYTE trackbuf;   // track buffer memory
	UDWORD tracklen;   // track buffer memory length
	UDWORD timelen;    // timing buffer length
	PUDWORD timebuf;   // timing buffer
	SDWORD overlap;    // overlap position
};

typedef struct CapsTrackInfoT1* PCAPSTRACKINFOT1;

// disk track information block
struct CapsTrackInfoT2 {
	UDWORD type;       // track type
	UDWORD cylinder;   // cylinder#
	UDWORD head;       // head#
	UDWORD sectorcnt;  // available sectors
	UDWORD sectorsize; // sector size, unused
	PUBYTE trackbuf;   // track buffer memory
	UDWORD tracklen;   // track buffer memory length
	UDWORD timelen;    // timing buffer length
	PUDWORD timebuf;   // timing buffer
	SDWORD overlap;    // overlap position
	UDWORD startbit;   // start position of the decoding
	UDWORD wseed;      // weak bit generator data
	UDWORD weakcnt;    // number of weak data areas
};

typedef struct CapsTrackInfoT2* PCAPSTRACKINFOT2;

// disk sector information block
struct CapsSectorInfo {
	UDWORD descdatasize; // data size in bits from IPF descriptor
	UDWORD descgapsize;  // gap size in bits from IPF descriptor
	UDWORD datasize;     // data size in bits from decoder
	UDWORD gapsize;      // gap size in bits from decoder
	UDWORD datastart;    // data start position in bits from decoder
	UDWORD gapstart;     // gap start position in bits from decoder
	UDWORD gapsizews0;   // gap size before write splice
	UDWORD gapsizews1;   // gap size after write splice
	UDWORD gapws0mode;   // gap size mode before write splice
	UDWORD gapws1mode;   // gap size mode after write splice
	UDWORD celltype;     // bitcell type
	UDWORD enctype;      // encoder type
};

typedef struct CapsSectorInfo* PCAPSSECTORINFO;

// disk data information block
struct CapsDataInfo {
	UDWORD type;  // data type
	UDWORD start; // start position
	UDWORD size;  // size in bits
};

typedef struct CapsDataInfo* PCAPSDATAINFO;

// disk data information block
struct CapsRevolutionInfo {
	SDWORD next; // the revolution number used by the next track lock call
	SDWORD last; // the revolution number used by the lack track lock call
	SDWORD real; // the real revolution number used by the last track lock call
	SDWORD max; // the maximum revolution available for the selected track, <0 unlimited/randomized, 0 empty
};

typedef struct CapsRevolutionInfo* PCAPSREVOLUTIONINFO;

#pragma pack(pop)

// CapsImageInfo.image type
enum {
	ciitNA = 0, // invalid image type
	ciitFDD   // floppy disk
};

// CapsImageInfo.platform IDs, not about configuration, but intended use
enum {
	ciipNA = 0, // invalid platform (dummy entry)
	ciipAmiga,
	ciipAtariST,
	ciipPC,
	ciipAmstradCPC,
	ciipSpectrum,
	ciipSamCoupe,
	ciipArchimedes,
	ciipC64,
	ciipAtari8
};

// CapsTrackInfo.track type
enum {
	ctitNA = 0,  // invalid type
	ctitNoise, // cells are unformatted (random size)
	ctitAuto,  // automatic cell size, according to track size
	ctitVar    // variable density
};

// CapsSectorInfo.bitcell type
enum {
	csicNA = 0, // invalid cell type
	csic2us   // 2us cells
};

// CapsSectorInfo.encoder type
enum {
	csieNA = 0, // undefined encoder
	csieMFM,  // MFM
	csieRaw   // no encoder used, test data only
};

// CapsSectorInfo.gap size mode
enum {
	csiegmFixed = 0,  // fixed size, can't be changed
	csiegmAuto,     // size can be changed, resize information calculated automatically
	csiegmResize    // size can be changed, resize information is scripted
};

// CapsDataInfo.data type
enum {
	cditNA = 0, // undefined
	cditWeak  // weak bits
};

// CAPSGetInfo inftype
enum {
	cgiitNA = 0,      // illegal
	cgiitSector,    // CapsSectorInfo
	cgiitWeak,      // CapsDataInfo, weak bits
	cgiitRevolution // CapsRevolutionInfo
};

// recognized image types
enum {
	citError = 0,     // error preventing the type identification
	citUnknown,     // unknown image type
	citIPF,         // IPF image
	citCTRaw,       // CT Raw image
	citKFStream,    // KryoFlux stream files
	citKFStreamCue, // KryoFlux stream cue file
	citDraft        // Draft image
};

// image error status
enum {
	imgeOk = 0,
	imgeUnsupported,
	imgeGeneric,
	imgeOutOfRange,
	imgeReadOnly,
	imgeOpen,
	imgeType,
	imgeShort,
	imgeTrackHeader,
	imgeTrackStream,
	imgeTrackData,
	imgeDensityHeader,
	imgeDensityStream,
	imgeDensityData,
	imgeIncompatible,
	imgeUnsupportedType,
	imgeBadBlockType,
	imgeBadBlockSize,
	imgeBadDataStart,
	imgeBufferShort
};

#endif
