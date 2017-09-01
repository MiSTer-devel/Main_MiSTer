#ifndef __DISKIMAGE_H
#define __DISKIMAGE_H
//-----------------------------------------------------------------------------

#ifdef __cplusplus

enum TDiskImageType { DIT_UNK, DIT_SCL, DIT_FDI, DIT_TD0, DIT_UDI, DIT_HOB, DIT_FDD };

struct VGFIND_TRACK
{
   unsigned char *TrackPointer;
   unsigned char *ClkPointer;
   unsigned int TrackLength;
   bool FoundTrack;
};


struct VGFIND_ADM
{
   unsigned char* TrackPointer;
   unsigned char* ClkPointer;
   unsigned int TrackLength;

   unsigned char *ADMPointer;
   unsigned int ADMLength;

   unsigned int MarkedOffsetADM;
   unsigned int OffsetADM;
   unsigned int OffsetEndADM;
   bool FoundADM;
   bool CRCOK;
};


struct VGFIND_SECTOR
{
   VGFIND_ADM vgfa;

   unsigned char *SectorPointer;
   unsigned int SectorLength;

   unsigned int MarkedOffsetSector;
   unsigned int OffsetSector;
   unsigned int OffsetEndSector;
   bool FoundDATA;
   bool CRCOK;
   unsigned char DataMarker;
};


class TDiskImage
{
   unsigned int FTrackLength[256][256];
   unsigned char* FTracksPtr[256][256][2];

   TDiskImageType FType;

   unsigned short MakeVGCRC(unsigned char *data, unsigned long length);
public:
   bool Changed;

   bool ReadOnly;
   bool DiskPresent;
   unsigned char MaxTrack;
   unsigned char MaxSide;

   TDiskImage();
   ~TDiskImage();

   bool FindTrack(unsigned char CYL, unsigned char SIDE, VGFIND_TRACK *vgft);
   bool FindADMark(unsigned char CYL, unsigned char SIDE,
                   unsigned int FromOffset, 
                   VGFIND_ADM *vgfa);
   bool FindSector(unsigned char CYL, unsigned char SIDE,
                   unsigned char SECT, 
                   VGFIND_SECTOR *vgfs, unsigned int FromOffset=0);
   void ApplySectorCRC(VGFIND_SECTOR vgfs);


   void Open(const char *filename, bool ReadOnly);

   void writeTRD(int hfile);

   void readSCL(int hfile, bool readonly);
   void readFDI(int hfile, bool readonly);
   void readUDI(int hfile, bool readonly);
   void readTD0(int hfile, bool readonly);
   void readFDD(int hfile, bool readonly);
   void readHOB(int hfile);

   void formatTRDOS(unsigned int tracks, unsigned int sides);

   void ShowError(const char *str);
};

#pragma pack(1)
struct UDI_HEADER               // 16 bytes
{
   unsigned char ID[4];
   unsigned long UnpackedLength;
   unsigned char Version;
   unsigned char MaxCylinder;
   unsigned char MaxSide;
   unsigned char _zero;
   unsigned long ExtHdrLength;
};

struct TD0_MAIN_HEADER          // 12 bytes
{
   char ID[2];                  // +0:  "TD" - 'Normal'; "td" - packed LZH ('New Advanced data compression')
   unsigned char __t;           // +2:  = 0x00
   unsigned char __1;           // +3:  ???
   unsigned char Ver;           // +4:  Source version  (1.0 -> 10, ..., 2.1 -> 21)
   unsigned char __2;           // +5:  ???
   unsigned char DiskType;      // +6:  Source disk type
   unsigned char Info;          // +7:  D7-наличие image info
   unsigned char DataDOS;       // +8:  if(=0)'All sectors were copied', else'DOS Allocated sectors were copied'
   unsigned char ChkdSides;     // +9:  if(=1)'One side was checked', else'Both sides were checked'
   unsigned short CRC;          // +A:  CRC хидера TD0_MAIN_HEADER (кроме байт с CRC)
};

struct TD0_INFO_DATA             // 10 байт без строки коментария...
{
   unsigned short CRC;          // +0:  CRC для структуры COMMENT_DATA (без байтов CRC)
   unsigned short strLen;       // +2:  Длина строки коментария 
   unsigned char Year;          // +4:  Дата создания - год (1900 + X)
   unsigned char Month;         // +5:  Дата создания - месяц (Январь=0, Февраль=1,...)
   unsigned char Day;           // +6:  Дата создания - число
   unsigned char Hours;         // +7:  Время создания - часы
   unsigned char Minutes;       // +8:  Время создания - минуты
   unsigned char Seconds;       // +9:  Время создания - секунды
};

struct TD0_TRACK_HEADER         // 4 bytes
{
   unsigned char SectorCount;
   unsigned char Track;
   unsigned char Side;
   unsigned char CRCL;
};

struct TD0_SECT_HEADER          // 8 bytes
{
   unsigned char ADRM[6];
   unsigned short DataLength;
};

struct FDD_MAIN_HEADER 
{
    char ID[30];                /* сигнатура */
    unsigned char MaxTracks;    /* число треков (цилиндров) */
    unsigned char MaxHeads;     /* число головок (1 или 2) */
    long diskIndex;             /* unused */
    long DataOffset[512*2];     /* смещение в файле к структурам заголовков */
                                /* треков       */
};

struct FDD_TRACK_HEADER
{
    unsigned char trkType;      /* unused */
    unsigned char SectNum;      /* число секторов на треке */
    struct
    {
        /* заголовок сектора */
         unsigned char trk;     /* номер трека */
         unsigned char side;    /* номер стороны */
                                /* 7 бит этого байта указывает бит a */
         unsigned char sect;    /* номер сектора */
         unsigned char size;    /* размер сектора (код) */
         long SectPos;          /* смещение в файле к данным сектора */
    } sect[256];
};


struct TRDOS_DIR_ELEMENT        // 16 bytes
{
   char FileName[8];
   char Type;
   unsigned short Start;
   unsigned short Length;
   unsigned char SecLen;
   unsigned char FirstSec;
   unsigned char FirstTrk;
};
#pragma pack()

#else

int x2trd(char *name, fileTYPE *f);
int x2trd_ext_supp(char *name);

#endif

//-----------------------------------------------------------------------------
#endif