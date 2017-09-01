#include <unistd.h>
#include <fcntl.h>
#include <sys\stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "DiskImage.h"
#include "file_io.h"

#define ERR_OPEN        "Error: can't open source file"
#define ERR_GETLEN      "Error: can't get file length!"
#define ERR_NOMEM       "Error: no memory!"
#define ERR_FORMAT      "Error: incorrect format"
#define ERR_FILEVER     "Error: unknown version of"
#define ERR_FILECRC     "Error: bad file CRC"
#define ERR_CANTWRITE   "Error: write to file failed!"
#define ERR_UNKFORMAT   "Error: Unknown format of source file!"
#define ERR_BADCREATE   "Допустимо создавать только TRD, SCL и FDI файлы!"
#define ERR_MANYCYLS    "Error: out of 256 cylinders in opening source file!"
#define ERR_MANYSIDS    "Error: out of 2 surfaces in opening source file!"
#define ERR_IMPOSSIBLE  "Error: impossible format in opening source file!"
#define ERR_CORRUPT     "Error: source file is corrupted!"
#define ERR_TD0DOSALLOC "Error: files TD0 in 'DOS Allocated sectors were copied' format not supported!"

#define ERR_BADOPENED   "Error: file opened in incorrect mode (write only?)"

#define STR_CREATEDISKNAME      "ZXMAK     "

char errsect[] = "ERROR: THIS SECTOR NOT FOUND OR IN NON TR-DOS FORMAT!";

char fdicomment[] = "\r\nCreated by TRX2X converter\r\n(C)2002 Alex Makeev\r\nhttp://zxmak.chat.ru/\r\n\r\n";

unsigned char sbootimage[4];

unsigned char sbootdir[16] = {
	0x62, 0x6F, 0x6F, 0x74, 0x20, 0x20, 0x20, 0x20, 0x42, 0xFC, 0x06, 0xFC, 0x06, 0x07, 0x09, 0x00
};


long CalcCRC32(long CRC, unsigned char Symbol)
{
	long temp;
	CRC ^= -1l ^ Symbol;
	for (int k = 8; k--;)
	{
		temp = -(CRC & 1), CRC >>= 1, CRC ^= 0xEDB88320ul & temp;
	}
	CRC ^= -1l;
	return CRC;
}

long filelength(int hfile)
{
	long ret = lseek(hfile, 0, SEEK_END);
	lseek(hfile, 0, SEEK_SET);
	return ret;
}

//----------------------------------------------------------------------------
TDiskImage::TDiskImage()
{
	AddBOOT = false;

	for (int t = 0; t < 256; t++)
		for (int s = 0; s < 256; s++)
		{
			FTrackLength[t][s] = 0;
			FTracksPtr[t][s][0] = NULL;
			FTracksPtr[t][s][1] = NULL;
		}

	DiskPresent = false;
	ReadOnly = true;
	Changed = false;
	FType = DIT_UNK;
	MaxTrack = 81;
	MaxSide = 0x01;
}
//-----------------------------------------------------------------------------
TDiskImage::~TDiskImage()
{
	FlushImage();
	ReadOnly = true;
	DiskPresent = false;
	Changed = false;
	FType = DIT_UNK;

	for (int t = 0; t < 256; t++)
		for (int s = 0; s < 256; s++)
		{
			FTrackLength[t][s] = 0;
			if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
			FTracksPtr[t][s][0] = NULL;
			if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
			FTracksPtr[t][s][1] = NULL;
		}
}
//-----------------------------------------------------------------------------
unsigned short TDiskImage::MakeVGCRC(unsigned char *data, unsigned long length)
{
	unsigned short CRC = 0xFFFF;
	for (unsigned int i = 0; i < length; i++)
	{
		CRC ^= data[i] << 8;
		for (unsigned int j = 0; j < 8; j++)
		{
			if (CRC & 0x8000) CRC = (CRC << 1) ^ 0x1021;
			else CRC <<= 1;
		}
	}
	return CRC;          // H<-->L !!!
}
//-----------------------------------------------------------------------------
void TDiskImage::ApplySectorCRC(VGFIND_SECTOR vgfs)
{
	unsigned char *TrackPtr = vgfs.vgfa.TrackPointer;
	unsigned int TrackLen = vgfs.vgfa.TrackLength;
	unsigned int len = vgfs.OffsetEndSector - vgfs.MarkedOffsetSector;
	if (vgfs.OffsetEndSector < vgfs.MarkedOffsetSector)
		len = (TrackLen - vgfs.MarkedOffsetSector) + vgfs.OffsetEndSector;

	unsigned int off1 = vgfs.MarkedOffsetSector;
	unsigned int len1 = TrackLen - vgfs.MarkedOffsetSector;
	if (len1 > len) len1 = len;
	unsigned int off2 = 0;
	unsigned int len2 = 0;
	if (len1 < len) len2 = len - len1;

	unsigned int i;
	unsigned short CRC = 0xFFFF;
	for (i = 0; i < len1; i++)
	{
		CRC ^= TrackPtr[off1 + i] << 8;
		for (unsigned int j = 0; j < 8; j++)
		{
			if (CRC & 0x8000) CRC = (CRC << 1) ^ 0x1021;
			else CRC <<= 1;
		}
	}
	for (i = 0; i < len2; i++)
	{
		CRC ^= TrackPtr[off2 + i] << 8;
		for (unsigned int j = 0; j < 8; j++)
		{
			if (CRC & 0x8000) CRC = (CRC << 1) ^ 0x1021;
			else CRC <<= 1;
		}
	}
	unsigned int crcoff = (off1 + len1) % TrackLen;
	if (len2) crcoff = (off2 + len2) % TrackLen;

	TrackPtr[crcoff] = (unsigned char)(CRC >> 8);
	TrackPtr[(crcoff + 1) % TrackLen] = (unsigned char)(CRC & 0xFF);
}
//-----------------------------------------------------------------------------
//
// DANGER!  CRC checking not prepared for track length overflow!
//
bool TDiskImage::FindADMark(unsigned char CYL, unsigned char SIDE,
	unsigned int FromOffset,
	VGFIND_ADM *vgfa)
{
	vgfa->TrackPointer = NULL;
	vgfa->ClkPointer = NULL;
	vgfa->TrackLength = 0;
	vgfa->ADMPointer = NULL;
	vgfa->ADMLength = 0;
	vgfa->FoundADM = false;
	vgfa->CRCOK = false;

	if ((!DiskPresent) |
		((CYL > MaxTrack) || (SIDE > MaxSide)) |
		((!FTracksPtr[CYL][SIDE][0]) || (!FTracksPtr[CYL][SIDE][1])))
	{
		return false;           // ERROR: disk not ready
	}

	unsigned char *track = vgfa->TrackPointer = FTracksPtr[CYL][SIDE][0];
	unsigned char *clks = vgfa->ClkPointer = FTracksPtr[CYL][SIDE][1];
	unsigned int tlen = vgfa->TrackLength = FTrackLength[CYL][SIDE];

	unsigned int off, rc;

	unsigned int pos = FromOffset;
	for (; pos < tlen + FromOffset; pos++)
	{
		off = pos%tlen;
		if ((track[off] == 0xA1) && (clks[off])) // fnd Mark
		{
			off = (off + 1) % tlen;

			rc = tlen;
			while ((track[off] == 0xA1) && (clks[off])) // repeat Mark
			{
				if (!rc) return false;           // ERROR: MFM marks all disk
				off = (off + 1) % tlen;
				rc--;
			}

			if (track[off] != 0xFE) continue;

			off = (off + 1) % tlen;

			vgfa->FoundADM = true;
			vgfa->MarkedOffsetADM = pos%tlen;
			vgfa->OffsetADM = off;
			vgfa->OffsetEndADM = (off + 6) % tlen;
			vgfa->ADMLength = 6;
			vgfa->ADMPointer = track + off;

			unsigned short crc = MakeVGCRC(track + (pos%tlen), (off - (pos%tlen)) + 4);

			vgfa->CRCOK = (track[(off + 4) % tlen] == (crc >> 8)) && (track[(off + 5) % tlen] == (crc & 0xFF));

			return true;
		}
	}

	return false;
}
//-----------------------------------------------------------------------------
//
// DANGER!  CRC checking not prepared for track length overflow!
//
bool TDiskImage::FindSector(unsigned char CYL, unsigned char SIDE,
	unsigned char SECT,
	VGFIND_SECTOR *vgfs, unsigned int FromOffset)
{
	vgfs->SectorPointer = NULL;
	vgfs->SectorLength = 0;
	vgfs->FoundDATA = false;
	vgfs->CRCOK = false;

	if ((!DiskPresent) |
		((CYL > MaxTrack) || (SIDE > MaxSide)) |
		((!FTracksPtr[CYL][SIDE][0]) || (!FTracksPtr[CYL][SIDE][1])))
	{
		vgfs->vgfa.TrackPointer = NULL;
		vgfs->vgfa.ClkPointer = NULL;
		vgfs->vgfa.TrackLength = 0;
		vgfs->vgfa.ADMPointer = NULL;
		vgfs->vgfa.ADMLength = 0;
		vgfs->vgfa.FoundADM = false;
		vgfs->vgfa.CRCOK = false;
		return false;           // ERROR: disk not ready
	}

	unsigned int TrackOffset = FromOffset;

	bool FirstFind = true;
	unsigned int FirstPos;

	// Поиск адресной метки требуемого сектора...
	bool ADFOUND = false;
	for (;;)
	{
		if (!FindADMark(CYL, SIDE, TrackOffset, &(vgfs->vgfa)))
			return false;          // ERROR: No ADMARK found on track

		if (vgfs->vgfa.TrackPointer[(vgfs->vgfa.OffsetADM + 2) % vgfs->vgfa.TrackLength] == SECT)
		{
			ADFOUND = true;
			break;
		}

		if (!FirstFind)
		{
			if (vgfs->vgfa.OffsetEndADM == FirstPos) break;
		}
		else
		{
			FirstPos = vgfs->vgfa.OffsetEndADM;
			FirstFind = false;
		}

		TrackOffset = vgfs->vgfa.OffsetEndADM;
	};

	if (!ADFOUND) return false;

	// ADRMARK нужного найден, поиск массива данных...

	unsigned char *track = vgfs->vgfa.TrackPointer;
	unsigned char *clks = vgfs->vgfa.ClkPointer;
	unsigned int tlen = vgfs->vgfa.TrackLength;

	unsigned int pos = vgfs->vgfa.OffsetEndADM;

	unsigned int off, rc;

	for (; pos < tlen * 2; pos++)
	{
		off = pos%tlen;
		if ((track[off] == 0xA1) && (clks[off])) // fnd Mark
		{
			off = (off + 1) % tlen;

			rc = tlen;
			while ((track[off] == 0xA1) && (clks[off])) // repeat Mark
			{
				if (!rc) return false;       // ERROR: MFM marks all disk
				off = (off + 1) % tlen;
				rc--;
			}

			if ((track[off] < 0xF8) || (track[off] > 0xFB))
			{
				break;                      // ERROR: data array not found
			}
			vgfs->DataMarker = track[off];

			off = (off + 1) % tlen;

			vgfs->FoundDATA = true;

			unsigned char SL = vgfs->vgfa.TrackPointer[(vgfs->vgfa.OffsetADM + 3) % vgfs->vgfa.TrackLength];
			vgfs->SectorLength = 128;
			if (SL) vgfs->SectorLength <<= SL;
			vgfs->SectorPointer = track + off;

			vgfs->MarkedOffsetSector = pos%tlen;
			vgfs->OffsetSector = off;
			vgfs->OffsetEndSector = (off + vgfs->SectorLength) % tlen;

			unsigned short crc = MakeVGCRC(track + (pos%tlen), (off - (pos%tlen)) + vgfs->SectorLength);
			vgfs->CRCOK = (track[(off + vgfs->SectorLength) % tlen] == (crc >> 8)) && (track[(off + vgfs->SectorLength + 1) % tlen] == (crc & 0xFF));

			return true;                         // OK read
		}
	}

	return false;
}
//-----------------------------------------------------------------------------
bool TDiskImage::FindTrack(unsigned char CYL, unsigned char SIDE, VGFIND_TRACK *vgft)
{
	vgft->FoundTrack = false;
	vgft->TrackPointer = NULL;
	vgft->ClkPointer = NULL;
	vgft->TrackLength = 0;

	if ((!DiskPresent) |
		((CYL > MaxTrack) || (SIDE > MaxSide)) |
		((!FTracksPtr[CYL][SIDE][0]) || (!FTracksPtr[CYL][SIDE][1])))
	{
		return false;           // ERROR: disk not ready
	}

	vgft->TrackPointer = FTracksPtr[CYL][SIDE][0];
	vgft->ClkPointer = FTracksPtr[CYL][SIDE][1];
	vgft->TrackLength = FTrackLength[CYL][SIDE];
	vgft->FoundTrack = true;
	return true;
}

//-----------------------------------------------------------------------------

void TDiskImage::FlushImage()
{
	if (ReadOnly) return;
	if (!Changed) return;
	if (FType == DIT_HOB) return;

	int hfile = open((char*)FFileName, O_CREAT | O_RDWR | O_TRUNC);
	if (hfile < 0)
	{
		ShowError(ERR_CANTWRITE);
		return;
	}

	if (FType == DIT_TRD) writeTRD(hfile);
	if (FType == DIT_SCL) writeSCL(hfile);
	if (FType == DIT_FDI) writeFDI(hfile);
	if (FType == DIT_UDI) writeUDI(hfile);
	if (FType == DIT_TD0) writeTD0(hfile);
	if (FType == DIT_FDD) writeFDD(hfile);

	close(hfile);
	Changed = false;
}
//-----------------------------------------------------------------------------
void TDiskImage::Open(const char *filename, bool ROnly)
{
	FlushImage();

	const char *ext = "";
	if (strlen(filename) > 4) ext = filename + strlen(filename) - 4;

	TDiskImageType typ = DIT_UNK;
	if (!strcasecmp(ext, ".TRD")) typ = DIT_TRD;
	if (!strcasecmp(ext, ".SCL")) typ = DIT_SCL;
	if (!strcasecmp(ext, ".FDI")) typ = DIT_FDI;
	if (!strcasecmp(ext, ".UDI")) typ = DIT_UDI;
	if (!strcasecmp(ext, ".TD0")) typ = DIT_TD0;
	if (!strcasecmp(ext, ".FDD")) typ = DIT_FDD;
	if (!memcmp(ext + 1, ".$", 2)) typ = DIT_HOB;
	if (!memcmp(ext + 1, ".!", 2)) typ = DIT_HOB;

	if (!((FType == DIT_HOB) && (typ == DIT_HOB)))     // if not hobeta clear disk...
	{
		ReadOnly = true;
		DiskPresent = false;
		Changed = false;
		FType = DIT_UNK;

		for (int t = 0; t < 256; t++)
			for (int s = 0; s < 256; s++)
			{
				FTrackLength[t][s] = 0;
				if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
				FTracksPtr[t][s][0] = NULL;
				if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
				FTracksPtr[t][s][1] = NULL;
			}
	}

	if (typ == DIT_UNK)
	{
		ShowError(ERR_UNKFORMAT);
		return;
	}
	strcpy((char*)FFileName, filename);
	FType = typ;

	int hfile = open(filename, O_RDONLY);

	if (hfile < 0)
	{
		char sbuf[8192];
		sprintf(sbuf, ERR_OPEN" %s", filename);
		ShowError(sbuf);
		return;
	}

	if (typ == DIT_TRD) readTRD(hfile, ROnly);
	if (typ == DIT_SCL) readSCL(hfile, ROnly);
	if (typ == DIT_FDI) readFDI(hfile, ROnly);
	if (typ == DIT_UDI) readUDI(hfile, ROnly);
	if (typ == DIT_TD0) readTD0(hfile, ROnly);
	if (typ == DIT_FDD) readFDD(hfile, ROnly);
	if (typ == DIT_HOB) readHOB(hfile);

	close(hfile);
}
//-----------------------------------------------------------------------------
void TDiskImage::formatTRDOS(unsigned int Tcount, unsigned int Scount)
{
	MaxTrack = Tcount - 1;
	MaxSide = Scount - 1;

	unsigned short TotalSecs = Tcount*Scount * 16 - 16;

	// форматирование нового диска под TR-DOS (16 x 256bytes sector per track)...
	unsigned int ptrcrc;
	unsigned int r;
	unsigned short vgcrc;
	for (unsigned int trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (unsigned int side = 0; side <= unsigned(MaxSide); side++)
		{
			FTrackLength[trk][side] = 6250;

			FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
			FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img

			unsigned int tptr = 0;
			for (int sec = 0; sec < 16; sec++)
			{
				for (r = 0; r < 10; r++)        // Первый пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < 12; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				ptrcrc = tptr;
				for (r = 0; r < 3; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}
				FTracksPtr[trk][side][0][tptr] = 0xFE;   // Метка "Адрес"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				FTracksPtr[trk][side][0][tptr] = (unsigned char)trk; // cyl
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)0x00; // head (TR always 0)
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(sec + 1); // secN
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)0x01; // len=256b
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < 22; r++)        // Второй пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < 12; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				ptrcrc = tptr;
				for (r = 0; r < 3; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}
				FTracksPtr[trk][side][0][tptr] = 0xFB;   // Метка "Данные"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < 256; r++)        // сектор 256байт
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				if ((trk == 0) && (side == 0) && (sec == 8))      // make TR-DOS id
				{
					int ssec = tptr - 256;
					FTracksPtr[trk][side][0][ssec + 0xE1] = 0x00; // first free SECT
					FTracksPtr[trk][side][0][ssec + 0xE2] = 0x01; // first free TRACK
					FTracksPtr[trk][side][0][ssec + 0xE3] = 0x16; // 80trk DS
					FTracksPtr[trk][side][0][ssec + 0xE4] = 0x00; // file count
					*(unsigned short*)(FTracksPtr[trk][side][0] + ssec + 0xE5)
						= TotalSecs; // free SECS count
					FTracksPtr[trk][side][0][ssec + 0xE7] = 0x10; // TR-DOS id
					FTracksPtr[trk][side][0][ssec + 0xF4] = 0x00; // deleted file count

					memcpy(FTracksPtr[trk][side][0] + ssec + 0xF5,
						STR_CREATEDISKNAME"               ", 8); // disk name
					FTracksPtr[trk][side][0][ssec + 0xFD] = 0x00; // zero
					FTracksPtr[trk][side][0][ssec + 0xFE] = 0x00; // zero
					FTracksPtr[trk][side][0][ssec + 0xFF] = 0x00; // zero
				}

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < 60; r++)        // Третий пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
			}
			for (int eoftrk = tptr; eoftrk < 6250; eoftrk++)
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
		}
}
//-----------------------------------------------------------------------------
void TDiskImage::readUDI(int hfile, bool ronly)
{
	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}

	unsigned char *ptr = (unsigned char*)new char[fsize + 1024 * 2048];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}

	unsigned long rsize = read(hfile, ptr, fsize + 1024);
	if (rsize < 16 + 4)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	if (memcmp(ptr, "UDI!", 4) != 0)
	{
		delete ptr;
		ShowError(ERR_FORMAT" UDI!");
		return;
	}

	UDI_HEADER *udi_hdr = (UDI_HEADER*)(ptr);

	if ((udi_hdr->Version != 0x00) || (udi_hdr->_zero != 0x00) || (udi_hdr->ExtHdrLength != 0))
	{
		delete ptr;
		ShowError(ERR_FILEVER" UDI!");
		return;
	}
	if (rsize != (udi_hdr->UnpackedLength + 4))
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	MaxTrack = udi_hdr->MaxCylinder;
	MaxSide = udi_hdr->MaxSide;


	// checking for corrupt...
	unsigned int udiOFF = 0x10;

	unsigned int trk, side;

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			unsigned char frmt = ptr[udiOFF++];
			if (rsize < udiOFF + 4)
			{
				delete ptr;
				ShowError(ERR_CORRUPT);
				return;
			}

			if (frmt)
			{
				udiOFF += *((unsigned long*)(ptr + udiOFF));
				udiOFF += 4;
				continue;
			}

			unsigned ccctlen = *((unsigned short*)(ptr + udiOFF));
			udiOFF += ccctlen;
			udiOFF += 2;
			if (rsize < udiOFF + 4)
			{
				delete ptr;
				ShowError(ERR_CORRUPT);
				return;
			}

			udiOFF += ccctlen / 8 + ((ccctlen - (ccctlen / 8) * 8) ? 1 : 0);
			if (rsize < udiOFF + 4)
			{
				delete ptr;
				ShowError(ERR_CORRUPT);
				return;
			}
		}


	udiOFF = 0x10;

	unsigned int trklen;

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			if (udiOFF >= rsize) break;

			if (ptr[udiOFF++] != 0)        // non MFM track?
			{
				FTrackLength[trk][side] = 6250;
				// make unformatted track...
				FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
				FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img
				for (unsigned ij = 0; ij < 6250; ij++)
				{
					FTracksPtr[trk][side][0][ij] = 0x00;
					FTracksPtr[trk][side][1][ij] = 0x00;
				}

				udiOFF += *((unsigned long*)(ptr + udiOFF));
				udiOFF += 4;
				continue;
			}
			trklen = *((unsigned short*)(ptr + udiOFF));
			udiOFF += 2;
			FTrackLength[trk][side] = trklen;

			// make unformatted track...
			FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
			FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img
			for (unsigned ij = 0; ij < FTrackLength[trk][side]; ij++)
			{
				FTracksPtr[trk][side][0][ij] = 0x00;
				FTracksPtr[trk][side][1][ij] = 0x00;
			}

			memcpy(FTracksPtr[trk][side][0], ptr + udiOFF, FTrackLength[trk][side]);
			udiOFF += trklen;

			unsigned int MFMinfoLen = trklen / 8 + ((trklen - (trklen / 8) * 8) ? 1 : 0);

			unsigned char mask;
			for (unsigned i = 0; i < MFMinfoLen; i++)
			{
				mask = 0x01;
				for (int j = 0; j < 8; j++)
				{
					if (ptr[udiOFF] & mask) FTracksPtr[trk][side][1][i * 8 + j] = 0xFF;
					else FTracksPtr[trk][side][1][i * 8 + j] = 0x00;
					mask <<= 1;
				}
				udiOFF++;
			}
		}
	long CRC = -1l;
	for (unsigned int i = 0; i < udiOFF; i++) CRC = CalcCRC32(CRC, ptr[i]);


	if (udiOFF < rsize)
		if (*((long*)(ptr + udiOFF)) != CRC)
			ShowError(ERR_FILECRC" UDI!");

	delete ptr;
	ReadOnly = ronly;
	FType = DIT_UDI;
	DiskPresent = true;
}
//-----------------------------------------------------------------------------
void TDiskImage::writeUDI(int hfile)
{
	long CRC = -1l;
	unsigned int i;

	UDI_HEADER hudi;
	memcpy(hudi.ID, "UDI!", 4);
	hudi.UnpackedLength = 0;
	hudi.Version = 0x00;
	hudi.MaxCylinder = MaxTrack;
	hudi.MaxSide = MaxSide;
	hudi._zero = 0x00;
	hudi.ExtHdrLength = 0;

	lseek(hfile, 0, SEEK_SET);
	write(hfile, &hudi, 16);
	lseek(hfile, 0, SEEK_SET);
	char testbuf[1024];
	long trs = read(hfile, testbuf, 16);
	if ((trs < 0) || (memcmp(testbuf, &hudi, 16) != 0))
	{
		ShowError(ERR_BADOPENED);
		return;
	}


	unsigned char valB;
	unsigned short valS;
	unsigned char clkbuf[65536];

	for (unsigned int trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (unsigned int side = 0; side <= unsigned(MaxSide); side++)
		{
			valB = 0x00;
			write(hfile, &valB, 1);
			valS = FTrackLength[trk][side];
			write(hfile, &valS, 2);
			write(hfile, FTracksPtr[trk][side][0], valS);

			unsigned int cptr = 0;
			unsigned char mask;
			for (i = 0; i < unsigned(valS / 8 + ((valS - (valS / 8) * 8) ? 1 : 0)); i++)
			{
				clkbuf[cptr] = 0x00;
				mask = 0x01;
				for (int j = 0; j < 8; j++)
				{

					if (FTracksPtr[trk][side][1][i * 8 + j]) clkbuf[cptr] |= mask;
					mask <<= 1;
				}
				cptr++;
			}

			write(hfile, clkbuf, (valS / 8 + ((valS - (valS / 8) * 8) ? 1 : 0)));
		}

	long len = filelength(hfile);
	lseek(hfile, 0, SEEK_SET);
	char *cbuf = new char[len + 0x10000];
	UDI_HEADER *newudi = (UDI_HEADER*)cbuf;
	read(hfile, cbuf, len);
	newudi->UnpackedLength = len;

	CRC = -1l;
	for (i = 0; i < len; i++) CRC = CalcCRC32(CRC, cbuf[i]);

	lseek(hfile, 0, SEEK_SET);
	write(hfile, cbuf, len);
	write(hfile, &CRC, 4);
}
//-----------------------------------------------------------------------------
void TDiskImage::readTRD(int hfile, bool readonly)
{
	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}

	unsigned char *ptr = (unsigned char*)new char[fsize + 1024 * 2048];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}

	unsigned long rsize = read(hfile, ptr, fsize);

	if (!rsize)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	if (rsize % (256 * 16 * 2))
	{
		delete ptr;
		ShowError(ERR_UNKFORMAT);
		return;
	}
	MaxSide = 1;
	MaxTrack = (rsize / (256 * 16 * 2)) - 1;


	// форматирование нового диска под TR-DOS...

	unsigned short vgcrc;
	for (unsigned int trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (unsigned int side = 0; side <= unsigned(MaxSide); side++)
		{
			FTrackLength[trk][side] = 6250;

			FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
			FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img


			unsigned int r;
			unsigned int tptr = 0;
			unsigned int ptrcrc;

			for (int sec = 0; sec < 16; sec++)
			{
				for (r = 0; r < 10; r++)        // Первый пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < 12; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				ptrcrc = tptr;
				for (r = 0; r < 3; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}
				FTracksPtr[trk][side][0][tptr] = 0xFE;   // Метка "Адрес"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				FTracksPtr[trk][side][0][tptr] = (unsigned char)trk; // cyl
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)0x00; // head (TR always 0)
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(sec + 1); // secN
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)0x01; // len=256b
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < 22; r++)        // Второй пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < 12; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				ptrcrc = tptr;
				for (r = 0; r < 3; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}
				FTracksPtr[trk][side][0][tptr] = 0xFB;   // Метка "Данные"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < 256; r++)        // сектор 256байт
				{
					FTracksPtr[trk][side][0][tptr] = ptr[(trk * 2 + side) * 4096 + sec * 256 + r];
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < 60; r++)        // Третий пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
			}
			for (int eoftrk = tptr; eoftrk < 6250; eoftrk++)
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
		}

	delete ptr;
	ReadOnly = readonly;
	FType = DIT_TRD;
	DiskPresent = true;
}

void TDiskImage::writeTRD(int hfile)
{
	VGFIND_SECTOR vgfs;

	// prepare nullbuf...
	unsigned char nullbuf[256];
	for (int i = 0; i < 256; i++) nullbuf[i] = '*';
	memcpy(nullbuf, errsect, sizeof(errsect));

	for (unsigned int trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (unsigned int side = 0; side <= unsigned(MaxSide); side++)
			for (unsigned int sec = 0; sec < 16; sec++)
			{
				if (FindSector(trk, side, sec + 1, &vgfs))
				{
					write(hfile, vgfs.SectorPointer, 256);
					if ((!vgfs.CRCOK) || (!vgfs.vgfa.CRCOK)) printf("Warning: sector %d on track %d, side %d with BAD CRC!\n", sec + 1, trk, side);
					if (vgfs.SectorLength != 256) printf("Warning: sector %d on track %d, side %d is non 256 bytes!\n", sec + 1, trk, side);
				}
				else
				{
					write(hfile, nullbuf, 256);
					printf("DANGER! Sector %d on track %d, side %d not found!\n", sec + 1, trk, side);
				}
			}
}

//-----------------------------------------------------------------------------
void TDiskImage::readFDI(int hfile, bool readonly)
{
	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}

	unsigned char *ptr = (unsigned char*)new char[fsize + 1024 * 2048];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}

	unsigned long rsize = read(hfile, ptr, fsize);
	if (rsize < 14)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	if (memcmp(ptr, "FDI", 3) != 0)
	{
		delete ptr;
		ShowError(ERR_FORMAT" FDI!");
		return;
	}

	// ==========================================
	// ********** Analyse FDI header ************...
	// ------------------------------------------
	unsigned short *fdihead = (unsigned short*)(ptr + 4);
	unsigned int fdiCylCount = fdihead[0];      // +4
	unsigned int fdiSideCount = fdihead[1];      // +6
												 //   unsigned int fdiOFFtext   = fdihead[2];      // +8
	unsigned int fdiOFFdata = fdihead[3];      // +A
	unsigned int fdiSIZEext = fdihead[4];      // +C

	if ((fdiCylCount > 256) || (fdiCylCount == 0))
	{
		delete ptr;
		ShowError(ERR_MANYCYLS);
		return;
	}
	if ((fdiSideCount > 256) || (fdiSideCount == 0))
	{
		delete ptr;
		ShowError(ERR_MANYSIDS);
		return;
	}

	MaxTrack = (unsigned char)(fdiCylCount - 1);
	MaxSide = (unsigned char)(fdiSideCount - 1);

	if (rsize < (0x0E + fdiSIZEext + (unsigned(MaxTrack) + 1)*(unsigned(MaxSide) + 1) * 7))
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	struct FDISECINFO
	{
		unsigned char ADAM[5];
		unsigned int SectorOffset;        // относит DataOffset
	};
	struct FDITRACKHDR
	{
		unsigned int DataOffset;          // относит начала файла
		unsigned int SectorCount;
		FDISECINFO SectorsInfo[256];
	};
	FDITRACKHDR *tracksinfo = new FDITRACKHDR[(unsigned(MaxTrack) + 1)*(unsigned(MaxSide) + 1)];

	unsigned int fdiOFF = 0x0E + fdiSIZEext;

	unsigned int trk, side;
	// Анализ области заголовков треков...
	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			if (rsize < fdiOFF)
			{
				delete tracksinfo;
				delete ptr;
				ShowError(ERR_CORRUPT);
				return;
			}

			tracksinfo[trk*(MaxSide + 1) + side].DataOffset = *((unsigned long*)(ptr + fdiOFF));
			fdiOFF += 4;

			if (rsize < fdiOFFdata + tracksinfo[trk*(MaxSide + 1) + side].DataOffset)
			{
				delete tracksinfo;
				delete ptr;
				ShowError(ERR_CORRUPT);
				return;
			}

			fdiOFF += 2;      // "Всегда содержит 0 (резерв для модернизации)"

			tracksinfo[trk*(MaxSide + 1) + side].SectorCount = unsigned(ptr[fdiOFF++]);

			for (unsigned isec = 0; isec < tracksinfo[trk*(MaxSide + 1) + side].SectorCount; isec++)
			{
				memcpy(tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[isec].ADAM, ptr + fdiOFF, 5);
				fdiOFF += 5;
				tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[isec].SectorOffset = unsigned(*((unsigned short*)(ptr + fdiOFF)));
				fdiOFF += 2;

				if (rsize < fdiOFFdata + tracksinfo[trk*(MaxSide + 1) + side].DataOffset + tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[isec].SectorOffset)
				{
					delete tracksinfo;
					delete ptr;
					ShowError(ERR_CORRUPT);
					return;
				}
			}
		}

	// форматирование нового диска и размещение FDI секторов...
	unsigned int ptrcrc;
	unsigned int r;
	unsigned short vgcrc;
	unsigned int trkdatalen;
	unsigned SecCount;
	unsigned SL;

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			FTrackLength[trk][side] = 6250;

			// make unformatted track...
			FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
			FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img
			for (unsigned ij = 0; ij < 6250; ij++)
			{
				FTracksPtr[trk][side][0][ij] = 0x00;
				FTracksPtr[trk][side][1][ij] = 0x00;
			}

			SecCount = tracksinfo[trk*(MaxSide + 1) + side].SectorCount;

			// Вычисляем необходимое число байт под данные:
			trkdatalen = 0;
			for (unsigned int ilsec = 0; ilsec < SecCount; ilsec++)
			{
				trkdatalen += 2 + 6;     // for marks:   0xA1, 0xFE, 6bytes
				SL = unsigned(tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[ilsec].ADAM[3]);
				if (!SL) SL = 128;
				else SL = 128 << SL;

				if (tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[ilsec].ADAM[4] & 0x40)
					SL = 0;          // заголовок без массива данных
				else
					trkdatalen += 4;       // for data header/crc: 0xA1, 0xFB, ...,2bytes

				trkdatalen += SL;
			}

			if (trkdatalen + SecCount*(3 + 2) > 6250)    // 3x4E & 2x00 per sec checking
			{
				delete tracksinfo;
				delete ptr;
				for (int t = 0; t < 256; t++)
					for (int s = 0; s < 256; s++)
					{
						FTrackLength[t][s] = 0;
						if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
						FTracksPtr[t][s][0] = NULL;
						if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
						FTracksPtr[t][s][1] = NULL;
					}
				ShowError(ERR_IMPOSSIBLE);
				return;
			}

			unsigned int FreeSpace = 6250 - (trkdatalen + SecCount*(3 + 2));

			unsigned int SynchroPulseLen = 1; // 1 уже учтен в trkdatalen...
			unsigned int FirstSpaceLen = 1;
			unsigned int SecondSpaceLen = 1;
			unsigned int ThirdSpaceLen = 1;
			unsigned int SynchroSpaceLen = 1;
			FreeSpace -= FirstSpaceLen + SecondSpaceLen + ThirdSpaceLen + SynchroSpaceLen;

			// Распределяем длины пробелов и синхропромежутка:
			while (FreeSpace > 0)
			{
				if (FreeSpace >= (SecCount * 2))
					if (SynchroSpaceLen < 12) { SynchroSpaceLen++; FreeSpace -= SecCount * 2; } // Synchro for ADM & DATA
				if (FreeSpace < SecCount) break;

				if (FirstSpaceLen < 10) { FirstSpaceLen++; FreeSpace -= SecCount; }
				if (FreeSpace < SecCount) break;
				if (SecondSpaceLen < 22) { SecondSpaceLen++; FreeSpace -= SecCount; }
				if (FreeSpace < SecCount) break;
				if (ThirdSpaceLen < 60) { ThirdSpaceLen++; FreeSpace -= SecCount; }
				if (FreeSpace < SecCount) break;

				if ((SynchroSpaceLen >= 12) && (FirstSpaceLen >= 10) && (SecondSpaceLen >= 22) && (ThirdSpaceLen >= 60)) break;
			};
			// по возможности делаем три синхроимпульса...
			if (FreeSpace >(SecCount * 2) + 10) { SynchroPulseLen++; FreeSpace -= SecCount; }
			if (FreeSpace >(SecCount * 2) + 9) SynchroPulseLen++;

			// Форматируем дорожку...

			unsigned int tptr = 0;
			for (unsigned sec = 0; sec < SecCount; sec++)
			{
				for (r = 0; r < FirstSpaceLen; r++)        // Первый пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < SynchroSpaceLen; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				ptrcrc = tptr;
				for (r = 0; r < SynchroPulseLen; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}
				FTracksPtr[trk][side][0][tptr] = 0xFE;   // Метка "Адрес"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				FTracksPtr[trk][side][0][tptr] = tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].ADAM[0]; // cyl
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].ADAM[1]; // head
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].ADAM[2]; // secN
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].ADAM[3]; // len code
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < SecondSpaceLen; r++)        // Второй пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < SynchroSpaceLen; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}

				unsigned char fdiSectorFlags = tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].ADAM[4];

				// !!!!!!!!!
				// !WARNING! this feature of FDI format is NOT FULL DOCUMENTED!!!
				// !!!!!!!!!
				//
				//  Flags::bit6 - Возможно, 1 в данном разряде
				//                будет обозначать адресный маркер без области данных.
				//

				if (!(fdiSectorFlags & 0x40)) // oh-oh, data area not present... ;-)
				{
					ptrcrc = tptr;
					for (r = 0; r < SynchroPulseLen; r++)        // Синхроимпульс
					{
						FTracksPtr[trk][side][0][tptr] = 0xA1;
						FTracksPtr[trk][side][1][tptr++] = 0xFF;
					}

					if (fdiSectorFlags & 0x80)
						FTracksPtr[trk][side][0][tptr] = 0xF8;   // Метка "Удаленные данные"
					else
						FTracksPtr[trk][side][0][tptr] = 0xFB;   // Метка "Данные"
					FTracksPtr[trk][side][1][tptr++] = 0x00;


					SL = unsigned(tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].ADAM[3]);
					if (!SL) SL = 128;
					else SL = 128 << SL;

					unsigned int secDATAOFF = fdiOFFdata + tracksinfo[trk*(MaxSide + 1) + side].DataOffset + tracksinfo[trk*(MaxSide + 1) + side].SectorsInfo[sec].SectorOffset;

					for (r = 0; r < SL; r++)        // сектор SL байт
					{
						FTracksPtr[trk][side][0][tptr] = ptr[secDATAOFF + r];
						FTracksPtr[trk][side][1][tptr++] = 0x00;
					}

					vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);


					if (fdiSectorFlags & 0x3F)        // CRC correct?
					{
						FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
						FTracksPtr[trk][side][1][tptr++] = 0x00;
						FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
						FTracksPtr[trk][side][1][tptr++] = 0x00;
					}
					else     // oh-oh, high technology... CRC bad... ;-)
					{
						FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8) ^ 0xFF; // emulation bad CRC... ;)
						FTracksPtr[trk][side][1][tptr++] = 0x00;
						FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF) ^ 0xFF;  // --//-- ;)
						FTracksPtr[trk][side][1][tptr++] = 0x00;
					}
				}


				for (r = 0; r < ThirdSpaceLen; r++)        // Третий пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
			}
			for (int eoftrk = tptr; eoftrk < 6250; eoftrk++)
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
		}

	delete tracksinfo;
	delete ptr;
	ReadOnly = readonly;
	FType = DIT_FDI;
	DiskPresent = true;
}
void TDiskImage::writeFDI(int hfile)
{
	unsigned char *ptr = (unsigned char*)new char[(MaxTrack + 1)*(MaxSide + 1) * 6250 + 524288];
	unsigned short *headptr = (unsigned short*)(ptr + 4);
	memcpy(ptr, "FDI", 3);
	ptr[3] = 0x00;                                      // write enabled
	headptr[0] = ((unsigned short)MaxTrack) + 1;         // Cyl count
	headptr[1] = ((unsigned short)MaxSide) + 1;          // Head count
	headptr[4] = 0;                                      // extension length

	unsigned int trk, side;
	VGFIND_SECTOR vgfs;
	VGFIND_ADM vgfa;

	unsigned char *htrkSectorCount;
	unsigned int fdiOFF = 0x0E;
	unsigned int trackOFF = 0;
	unsigned int SectorCount;
	unsigned int sectOFF;

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			*((unsigned long*)(ptr + fdiOFF)) = trackOFF;
			fdiOFF += 4;
			*((unsigned short*)(ptr + fdiOFF)) = 0;  // Всегда содержит 0 (резерв)
			fdiOFF += 2;
			htrkSectorCount = ptr + fdiOFF;
			fdiOFF++;
			SectorCount = 0;
			sectOFF = 0;

			unsigned int TrackPtr = 0;
			bool FirstFindAD = true;
			unsigned int FirstOffset;
			for (;;)
			{
				if (!FindADMark(trk, side, TrackPtr, &vgfa)) break;
				if (!FirstFindAD)
				{
					if (vgfa.OffsetADM == FirstOffset) break;
				}
				else
				{
					FirstOffset = vgfa.OffsetADM;
					FirstFindAD = false;
				}
				TrackPtr = vgfa.OffsetEndADM;

				SectorCount++;

				memcpy(ptr + fdiOFF, vgfa.ADMPointer, 4);
				fdiOFF += 4;
				unsigned char flags = 0x00;
				if (!FindSector(trk, side, vgfa.ADMPointer[2], &vgfs, vgfa.MarkedOffsetADM))
					flags |= 0x40;
				else
				{
					if (vgfs.DataMarker == 0xF8) flags |= 0x80;
					if (vgfs.CRCOK) flags |= (vgfs.SectorLength >> 7) & 0x3F;
				}
				ptr[fdiOFF++] = flags;
				*((unsigned short*)(ptr + fdiOFF)) = sectOFF;
				fdiOFF += 2;

				if (!(flags & 0x40)) sectOFF += vgfs.SectorLength;
			}
			*htrkSectorCount = (unsigned char)SectorCount;
			trackOFF += sectOFF;
		}
	headptr[2] = fdiOFF;                                 // Text offset
	memcpy(ptr + fdiOFF, fdicomment, sizeof(fdicomment));
	fdiOFF += sizeof(fdicomment);

	headptr[3] = fdiOFF;                                 // Data offset

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			unsigned int TrackPtr = 0;
			bool FirstFindAD = true;
			unsigned int FirstOffset;
			for (;;)
			{
				if (!FindADMark(trk, side, TrackPtr, &vgfa)) break;
				if (!FirstFindAD)
				{
					if (vgfa.OffsetADM == FirstOffset) break;
				}
				else
				{
					FirstOffset = vgfa.OffsetADM;
					FirstFindAD = false;
				}
				TrackPtr = vgfa.OffsetEndADM;

				if (FindSector(trk, side, vgfa.ADMPointer[2], &vgfs, vgfa.MarkedOffsetADM))
				{
					memcpy(ptr + fdiOFF, vgfs.SectorPointer, vgfs.SectorLength);
					fdiOFF += vgfs.SectorLength;
				}
			}
		}

	write(hfile, ptr, fdiOFF);
	delete ptr;
}
//-----------------------------------------------------------------------------
void TDiskImage::readFDD(int hfile, bool readonly)
{

	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}

	unsigned char *ptr = (unsigned char*)new char[fsize + 1024 * 32];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}

	unsigned long rsize = read(hfile, ptr, fsize);
	if (rsize < sizeof(FDD_MAIN_HEADER))
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	FDD_MAIN_HEADER *fdd_hdr = (FDD_MAIN_HEADER*)ptr;

	int MaxC = fdd_hdr->MaxTracks;
	int MaxH = fdd_hdr->MaxHeads;

	if (MaxH > 2)
	{
		delete ptr;
		ShowError(ERR_MANYSIDS);
		return;
	}


	MaxH = (MaxH - 1) & 0xFF;
	MaxC = (MaxC - 1) & 0xFF;


	MaxTrack = MaxC;
	MaxSide = MaxH;


	// форматирование нового диска и размещение FDD секторов...
	unsigned int ptrcrc;
	unsigned int r;
	unsigned short vgcrc;
	unsigned int trkdatalen;
	unsigned SecCount;
	unsigned SL;

	FDD_TRACK_HEADER *trackinfo;

	unsigned int trk, side;

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			FTrackLength[trk][side] = 6250;

			// make unformatted track...
			FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
			FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img
			for (unsigned ij = 0; ij < 6250; ij++)
			{
				FTracksPtr[trk][side][0][ij] = 0x00;
				FTracksPtr[trk][side][1][ij] = 0x00;
			}

			if ((fdd_hdr->DataOffset[trk*(MaxSide + 1) + side] + 2) > int(rsize))
			{
				delete ptr;
				for (int t = 0; t < 256; t++)
					for (int s = 0; s < 256; s++)
					{
						FTrackLength[t][s] = 0;
						if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
						FTracksPtr[t][s][0] = NULL;
						if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
						FTracksPtr[t][s][1] = NULL;
					}
				ShowError(ERR_CORRUPT);
				return;
			}

			trackinfo = (FDD_TRACK_HEADER *)(ptr + fdd_hdr->DataOffset[trk*(MaxSide + 1) + side]);

			SecCount = trackinfo->SectNum;

			if ((2 + SecCount * 8 + fdd_hdr->DataOffset[trk*(MaxSide + 1) + side]) > rsize)
			{
				delete ptr;
				for (int t = 0; t < 256; t++)
					for (int s = 0; s < 256; s++)
					{
						FTrackLength[t][s] = 0;
						if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
						FTracksPtr[t][s][0] = NULL;
						if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
						FTracksPtr[t][s][1] = NULL;
					}
				ShowError(ERR_CORRUPT);
				return;
			}
			else if (trackinfo->sect[SecCount - 1].SectPos > int(rsize))
			{
				delete ptr;
				for (int t = 0; t < 256; t++)
					for (int s = 0; s < 256; s++)
					{
						FTrackLength[t][s] = 0;
						if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
						FTracksPtr[t][s][0] = NULL;
						if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
						FTracksPtr[t][s][1] = NULL;
					}
				ShowError(ERR_CORRUPT);
				return;
			}



			// Вычисляем необходимое число байт под данные:
			trkdatalen = 0;
			for (unsigned int ilsec = 0; ilsec < SecCount; ilsec++)
			{
				trkdatalen += 2 + 6;     // for marks:   0xA1, 0xFE, 6bytes
				SL = unsigned(trackinfo->sect[ilsec].size);
				if (!SL) SL = 128;
				else SL = 128 << SL;

				trkdatalen += 4;       // for data header/crc: 0xA1, 0xFB, ...,2bytes

				trkdatalen += SL;
			}

			if (trkdatalen + SecCount*(3 + 2) > 6250)    // 3x4E & 2x00 per sec checking
			{
				delete ptr;
				for (int t = 0; t < 256; t++)
					for (int s = 0; s < 256; s++)
					{
						FTrackLength[t][s] = 0;
						if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
						FTracksPtr[t][s][0] = NULL;
						if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
						FTracksPtr[t][s][1] = NULL;
					}
				ShowError(ERR_IMPOSSIBLE);
				return;
			}

			unsigned int FreeSpace = 6250 - (trkdatalen + SecCount*(3 + 2));

			unsigned int SynchroPulseLen = 1; // 1 уже учтен в trkdatalen...
			unsigned int FirstSpaceLen = 1;
			unsigned int SecondSpaceLen = 1;
			unsigned int ThirdSpaceLen = 1;
			unsigned int SynchroSpaceLen = 1;
			FreeSpace -= FirstSpaceLen + SecondSpaceLen + ThirdSpaceLen + SynchroSpaceLen;

			// Распределяем длины пробелов и синхропромежутка:
			while (FreeSpace > 0)
			{
				if (FreeSpace >= (SecCount * 2))
					if (SynchroSpaceLen < 12) { SynchroSpaceLen++; FreeSpace -= SecCount * 2; } // Synchro for ADM & DATA
				if (FreeSpace < SecCount) break;

				if (FirstSpaceLen < 10) { FirstSpaceLen++; FreeSpace -= SecCount; }
				if (FreeSpace < SecCount) break;
				if (SecondSpaceLen < 22) { SecondSpaceLen++; FreeSpace -= SecCount; }
				if (FreeSpace < SecCount) break;
				if (ThirdSpaceLen < 60) { ThirdSpaceLen++; FreeSpace -= SecCount; }
				if (FreeSpace < SecCount) break;

				if ((SynchroSpaceLen >= 12) && (FirstSpaceLen >= 10) && (SecondSpaceLen >= 22) && (ThirdSpaceLen >= 60)) break;
			};
			// по возможности делаем три синхроимпульса...
			if (FreeSpace >(SecCount * 2) + 10) { SynchroPulseLen++; FreeSpace -= SecCount; }
			if (FreeSpace >(SecCount * 2) + 9) SynchroPulseLen++;

			// Форматируем дорожку...

			unsigned int tptr = 0;
			for (unsigned sec = 0; sec < SecCount; sec++)
			{
				for (r = 0; r < FirstSpaceLen; r++)        // Первый пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < SynchroSpaceLen; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				ptrcrc = tptr;
				for (r = 0; r < SynchroPulseLen; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}
				FTracksPtr[trk][side][0][tptr] = 0xFE;   // Метка "Адрес"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				FTracksPtr[trk][side][0][tptr] = trackinfo->sect[sec].trk;  // cyl
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = trackinfo->sect[sec].side; // head
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = trackinfo->sect[sec].sect; // secN
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = trackinfo->sect[sec].size; // len code
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				for (r = 0; r < SecondSpaceLen; r++)        // Второй пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				for (r = 0; r < SynchroSpaceLen; r++)        // Синхропромежуток
				{
					FTracksPtr[trk][side][0][tptr] = 0x00;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}


				// DATA AM
				ptrcrc = tptr;
				for (r = 0; r < SynchroPulseLen; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}

				FTracksPtr[trk][side][0][tptr] = 0xFB;   // Метка "Данные"
				FTracksPtr[trk][side][1][tptr++] = 0x00;


				SL = unsigned(trackinfo->sect[sec].size);
				if (!SL) SL = 128;
				else SL = 128 << SL;

				unsigned int secDATAOFF = trackinfo->sect[sec].SectPos;

				for (r = 0; r < SL; r++)        // сектор SL байт
				{
					FTracksPtr[trk][side][0][tptr] = ptr[secDATAOFF + r];
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);


				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;


				for (r = 0; r < ThirdSpaceLen; r++)        // Третий пробел
				{
					FTracksPtr[trk][side][0][tptr] = 0x4E;
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
			}
			for (int eoftrk = tptr; eoftrk < 6250; eoftrk++)
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
		}

	delete ptr;
	ReadOnly = readonly;
	FType = DIT_FDD;
	DiskPresent = true;

}
//-----------------------------------------------------------------------------
void TDiskImage::writeFDD(int hfile)
{
	char fddid[32] = "SPM DISK (c) 1996 MOA v0.1    ";


	unsigned char *ptr = (unsigned char*)new char[(MaxTrack + 1)*(MaxSide + 1) * 6250 + 524288];

	FDD_MAIN_HEADER *fdd_hdr = (FDD_MAIN_HEADER*)(ptr);

	memcpy(fdd_hdr->ID, fddid, 30);
	fdd_hdr->MaxTracks = (MaxTrack + 1) & 0xFF;
	fdd_hdr->MaxHeads = (MaxSide + 1) & 0xFF;
	fdd_hdr->diskIndex = 0;
	//   fdd_hdr->DataOffset =




	unsigned int trk, side;
	VGFIND_SECTOR vgfs;
	VGFIND_ADM vgfa;

	unsigned char *htrkSectorCount;
	unsigned int fddOFF = 36 + 4 * (MaxTrack + 1)*(MaxSide + 1);

	unsigned int SectorCount;
	unsigned int sectOFF;

	char secbuf[32768];

	for (trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (side = 0; side <= unsigned(MaxSide); side++)
		{
			fdd_hdr->DataOffset[trk*(MaxSide + 1) + side] = fddOFF;
			FDD_TRACK_HEADER *trackinfo = (FDD_TRACK_HEADER*)(ptr + fddOFF);
			fddOFF += 2;

			SectorCount = 0;


			unsigned int TrackPtr = 0;
			bool FirstFindAD = true;
			unsigned int FirstOffset;
			for (;;)
			{
				if (!FindADMark(trk, side, TrackPtr, &vgfa)) break;
				if (!FirstFindAD)
				{
					if (vgfa.OffsetADM == FirstOffset) break;
				}
				else
				{
					FirstOffset = vgfa.OffsetADM;
					FirstFindAD = false;
				}
				TrackPtr = vgfa.OffsetEndADM;

				trackinfo->sect[SectorCount].trk = vgfa.ADMPointer[0];
				trackinfo->sect[SectorCount].side = vgfa.ADMPointer[1];
				trackinfo->sect[SectorCount].sect = vgfa.ADMPointer[2];
				trackinfo->sect[SectorCount].size = vgfa.ADMPointer[3];
				trackinfo->sect[SectorCount].SectPos = vgfa.MarkedOffsetADM;  // tmp

				SectorCount++;
				fddOFF += 8;
			}

			trackinfo->SectNum = SectorCount;
			trackinfo->trkType = 0;


			for (int ilsec = 0; ilsec < int(SectorCount); ilsec++)
			{
				int SL = trackinfo->sect[ilsec].size;
				SL &= 3;
				if (!SL) SL = 128;
				else SL = 128 << SL;


				if (!FindSector(trk, side, trackinfo->sect[ilsec].sect, &vgfs, trackinfo->sect[ilsec].SectPos))
				{
					for (int i = 0; i < sizeof(secbuf); i++) secbuf[i] = 0;
					memcpy(secbuf, "***ERROR: SECTOR NOT FOUND!***", 31);
				}
				else
					memcpy(secbuf, vgfs.SectorPointer, SL);

				trackinfo->sect[ilsec].SectPos = fddOFF;

				memcpy(ptr + fddOFF, secbuf, SL);
				fddOFF += SL;
			}
		}

	write(hfile, ptr, fddOFF);
	delete ptr;
}
//-----------------------------------------------------------------------------

void TDiskImage::readSCL(int hfile, bool readonly)
{
	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}
	unsigned char *ptr = (unsigned char*)new char[fsize + 1024 * 2048];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}
	unsigned long rsize = read(hfile, ptr, fsize);

	if (!rsize)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}
	if (rsize < 9 + 4)      // header
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}
	if (memcmp(ptr, "SINCLAIR", 8) != 0)
	{
		delete ptr;
		ShowError(ERR_FORMAT" SCL!");
		return;
	}

	unsigned int FileCount = ptr[8];
	if (rsize < 9 + 4 + FileCount * 14)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	TRDOS_DIR_ELEMENT *fileinfo[256];

	unsigned short FilesTotalSecs = 0;

	// checking for corrupt + SCL-CRC + FileDIRS parse
	unsigned long SCLCRC = 0;
	unsigned sclOFF = 0;
	unsigned int i, j;
	for (i = 0; i < 9; i++) SCLCRC += unsigned(ptr[sclOFF++]);

	for (i = 0; i < FileCount; i++)
	{
		fileinfo[i] = (TRDOS_DIR_ELEMENT*)(ptr + sclOFF);
		for (j = 0; j < 14; j++) SCLCRC += unsigned(ptr[sclOFF++]);
	}
	for (i = 0; i < FileCount; i++)
	{
		unsigned SL = unsigned(fileinfo[i]->SecLen) * 256;
		FilesTotalSecs += fileinfo[i]->SecLen;
		if (rsize < sclOFF + 4 + SL)
		{
			delete ptr;
			ShowError(ERR_CORRUPT);
			return;
		}
		for (j = 0; j < SL; j++) SCLCRC += unsigned(ptr[sclOFF++]);
	}

	if (*((unsigned long*)(ptr + sclOFF)) != SCLCRC)
		ShowError(ERR_FILECRC" SCL!");


	if (FilesTotalSecs < 2544) FilesTotalSecs = 2544;
	else
	{
		int cyls = ((16 + FilesTotalSecs) / (16 * 2)) + (((16 + FilesTotalSecs) % (16 * 2)) ? 1 : 0);
		FilesTotalSecs = (cyls * 16 * 2) - 16;
	}

	formatTRDOS((FilesTotalSecs + 16) / (16 * 2), 2);
	ReadOnly = true;
	FType = DIT_SCL;
	DiskPresent = true;

	sclOFF = 9 + 14 * FileCount;

	bool BOOTADD = true;

	unsigned char SEC = 0, TRK = 1;
	unsigned short FreeSEC = 2544;
	unsigned char _FileCount = 0;
	unsigned char _DelFileCount = 0;

	TRDOS_DIR_ELEMENT trdosde;
	VGFIND_SECTOR vgfs;
	char sbuf[512];

	for (i = 0; i < FileCount; i++)
	{
		if (TRK >= (MaxTrack + 1)*(MaxSide + 1)) break;      // disk full ?

		memcpy(sbuf, fileinfo[i]->FileName, 9);
		if (sbuf[0] == 0x01) _DelFileCount++;
		_FileCount++;
		for (char *p = sbuf; *p != 0; p++) *p = toupper(*p);
		if (!memcmp(sbuf, "BOOT    B", 9)) BOOTADD = false;

		memcpy(&trdosde, fileinfo[i], 14);

		trdosde.FirstSec = SEC;
		trdosde.FirstTrk = TRK;

		unsigned int dirsec = ((i * 16) / 256) + 1;
		if (FindSector(0, 0, dirsec, &vgfs))         // DIR ELEMENT write
		{
			memcpy(vgfs.SectorPointer + ((i * 16) % 256), &trdosde, 16);
			ApplySectorCRC(vgfs);
		}

		for (j = 0; j < unsigned(trdosde.SecLen); j++)
		{
			if (FindSector(TRK / 2, TRK % 2, SEC + 1, &vgfs))       // SECTOR write
			{
				memcpy(vgfs.SectorPointer, ptr + sclOFF, 256);
				ApplySectorCRC(vgfs);
			}
			sclOFF += 256;
			SEC++;
			FreeSEC--;
			if (SEC > 15) { SEC = 0; TRK++; }

			if (TRK >= (MaxTrack + 1)*(MaxSide + 1)) break;      // disk full ?
		}
	}
	if (!AddBOOT) BOOTADD = false;

	if (BOOTADD && (unsigned(_FileCount) + unsigned(_DelFileCount) < 127))
	{
		unsigned int dirsec = ((_FileCount * 16) / 256) + 1;
		if (FindSector(0, 0, dirsec, &vgfs))         // DIR ELEMENT write
		{
			memcpy(vgfs.SectorPointer + ((i * 16) % 256), sbootdir, 16);
			ApplySectorCRC(vgfs);
		}
		_FileCount++;

		memcpy(&trdosde, sbootdir, 16);

		unsigned char _bSEC = trdosde.FirstSec + 1;
		unsigned int _bOFF = 0;

		for (j = 0; j < unsigned(trdosde.SecLen); j++)
		{
			if (FindSector(0, 0, _bSEC++, &vgfs))       // SECTOR write
			{
				memcpy(vgfs.SectorPointer, sbootimage + _bOFF, 256);
				ApplySectorCRC(vgfs);
			}
			_bOFF += 256;
			if (_bSEC > 16) break;
		}
	}

	if (FindSector(0, 0, 9, &vgfs))         // update disk info
	{
		vgfs.SectorPointer[0xE1] = SEC;
		vgfs.SectorPointer[0xE2] = TRK;
		vgfs.SectorPointer[0xE4] = _FileCount;
		vgfs.SectorPointer[0xF4] = _DelFileCount;
		*((unsigned short*)(vgfs.SectorPointer + 0xE5)) = FreeSEC;
		ApplySectorCRC(vgfs);
	}

	delete ptr;
	ReadOnly = readonly;
}
void TDiskImage::writeSCL(int hfile)
{
	VGFIND_SECTOR vgfs;

	if (!FindSector(0, 0, 9, &vgfs))
	{
		printf("DANGER! Sector 9 on track 0, side 0 not found!\n");
		return;
	}
	else if ((!vgfs.CRCOK) || (!vgfs.vgfa.CRCOK)) printf("Warning: sector 9 on track 0, side 0 with BAD CRC!\n");
	if (vgfs.SectorLength != 256) printf("DANGER! Sector 9 on track 0, side 0 is non 256 bytes!\n");


	if (vgfs.SectorPointer[0xE7] != 0x10)
	{
		printf("Error: source disk image in non TR-DOS format!\n");
		return;
	}
	unsigned int FileCount = unsigned(vgfs.SectorPointer[0xE4]);
	if (FileCount > 144)
	{
		printf("Error: source disk image contain incorrect sector 9, track 0, side 0!\n");
		return;
	}

	unsigned int SideCount = 2;
	if ((vgfs.SectorPointer[0xE3] == 0x18) || (vgfs.SectorPointer[0xE3] == 0x19))
		SideCount = 1;
	if ((vgfs.SectorPointer[0xE3] != 0x16) && (vgfs.SectorPointer[0xE3] != 0x17) &&
		(vgfs.SectorPointer[0xE3] != 0x18) && (vgfs.SectorPointer[0xE3] != 0x19))
	{
		printf("Error: source disk image in non TR-DOS format!\n");
		return;
	}

	unsigned i, j;
	unsigned long SCLCRC = 0;
	unsigned char buf[256 * 16];
	memcpy(buf, "SINCLAIR", 8);
	buf[8] = (unsigned char)FileCount;
	for (i = 0; i < 9; i++) SCLCRC += unsigned(buf[i]);

	write(hfile, buf, 9);

	// prepare nullbuf...
	unsigned char nullbuf[256];
	for (i = 0; i < 256; i++) nullbuf[i] = '*';
	memcpy(nullbuf, errsect, sizeof(errsect));
	TRDOS_DIR_ELEMENT nulldir;
	memcpy(nulldir.FileName, "WRONGSEC", 8);
	nulldir.Type = 'E';
	nulldir.Start = 0xEEEE;
	nulldir.Length = 0xEEEE;
	nulldir.SecLen = 0;
	nulldir.FirstSec = 0x00;
	nulldir.FirstSec = 0x00;

	TRDOS_DIR_ELEMENT fileinfo[256];
	for (i = 0; i < FileCount; i++)
	{
		if (FindSector(0, 0, ((i * 16) / 256) + 1, &vgfs))
		{
			memcpy(&fileinfo[i], vgfs.SectorPointer + (i * 16) % 256, 16);
			if ((!vgfs.CRCOK) || (!vgfs.vgfa.CRCOK)) printf("Warning: sector %d on track 0, side 0 with BAD CRC!\n", (((i * 16) / 256) + 1));
			if (vgfs.SectorLength != 256) printf("Warning: sector %d on track 0, side 0 is non TR-DOS!\n", (((i * 16) / 256) + 1));
		}
		else
		{
			memcpy(&fileinfo[i], &nulldir, 16);
			printf("DANGER! Sector %d on track 0, side 0 size is non 256 bytes!\n", (((i * 16) / 256) + 1));
		}
		memcpy(buf, &fileinfo[i], 16);

		write(hfile, buf, 14);
		for (j = 0; j < 14; j++) SCLCRC += unsigned(buf[j]);
	}

	unsigned char SEC, TRK;
	for (i = 0; i < FileCount; i++)
	{
		SEC = fileinfo[i].FirstSec;
		TRK = fileinfo[i].FirstTrk;

		for (j = 0; j < unsigned(fileinfo[i].SecLen); j++)
		{
			if (FindSector(TRK / SideCount, TRK%SideCount, SEC + 1, &vgfs))
			{
				memcpy(buf, vgfs.SectorPointer, 256);
				if ((!vgfs.CRCOK) || (!vgfs.vgfa.CRCOK)) printf("Warning: sector %d on track %d, side %d with BAD CRC!", SEC + 1, TRK / SideCount, TRK%SideCount);
				if (vgfs.SectorLength != 256) printf("Warning: sector %d on track %d, side %d is non 256 bytes!\n", SEC + 1, TRK / SideCount, TRK%SideCount);
			}
			else
			{
				memcpy(buf, nullbuf, 256);
				printf("DANGER! Sector %d on track %d, side %d not found!\n", SEC + 1, TRK / SideCount, TRK%SideCount);
			}
			write(hfile, buf, 256);
			for (unsigned k = 0; k < 256; k++) SCLCRC += unsigned(buf[k]);
			SEC++;
			if (SEC > 15) { SEC = 0; TRK++; }
		}
	}
	write(hfile, &SCLCRC, 4);
}
//-----------------------------------------------------------------------------
void TDiskImage::readHOB(int hfile)
{
	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}
	unsigned char *ptr = (unsigned char*)new char[fsize + 1024 * 2048];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}
	unsigned long rsize = read(hfile, ptr, fsize);

	if (!rsize)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}
	if (rsize < 17)      // header
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	TRDOS_DIR_ELEMENT dired;
	memcpy(&dired, ptr, 14);

	unsigned short hobRealCRC = *((unsigned short*)(ptr + 0x0F));
	unsigned int DataLength = *((unsigned short*)(ptr + 0x0D));

	if (rsize < 17 + (DataLength & 0xFF00))
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	unsigned int i;
	unsigned short CRC = 0;
	for (i = 0; i < 15; i++) CRC = CRC + ptr[i];
	CRC *= 257;
	CRC += 105;          // сумма чисел от 0 до 14

	if (CRC != hobRealCRC)
		ShowError(ERR_FILECRC" HOBETA!");




	ReadOnly = true;
	if (!DiskPresent)
	{
		formatTRDOS(80, 2);
		FType = DIT_HOB;
		DiskPresent = true;
	}

	// --- read file...
	dired.SecLen = ptr[0x0E];            // число секторов файла

	VGFIND_SECTOR vgfs9;
	if (!FindSector(0, 0, 9, &vgfs9)) { delete ptr; return; }

	dired.FirstSec = vgfs9.SectorPointer[0xE1];
	dired.FirstTrk = vgfs9.SectorPointer[0xE2];

	VGFIND_SECTOR vgfs;

	unsigned char SEC = dired.FirstSec, TRK = dired.FirstTrk;
	unsigned short FreeSEC = *((unsigned short*)(vgfs9.SectorPointer + 0xE5));
	unsigned char FileCount = vgfs9.SectorPointer[0xE4];
	unsigned char DelFileCount = vgfs9.SectorPointer[0xF4];

	if (TRK >= 160)       // disk full ?
	{
		delete ptr;
		return;
	}

	for (unsigned int j = 0; j < unsigned(dired.SecLen); j++)
	{
		if (FindSector(TRK / 2, TRK % 2, SEC + 1, &vgfs))
		{
			memcpy(vgfs.SectorPointer, ptr + 17 + j * 256, 256);
			ApplySectorCRC(vgfs);
		}
		SEC++;
		FreeSEC--;
		if (SEC > 15) { SEC = 0; TRK++; }

		if (TRK >= 160) break;     // disk full?
	}

	if (FindSector(0, 0, ((FileCount * 16) / 256) + 1, &vgfs))
	{
		memcpy(vgfs.SectorPointer + ((FileCount * 16) % 256), &dired, 16);
		ApplySectorCRC(vgfs);
	}

	if (dired.FileName[0] == 0x01) DelFileCount++;
	FileCount++;

	vgfs9.SectorPointer[0xE1] = SEC;
	vgfs9.SectorPointer[0xE2] = TRK;
	*((unsigned short*)(vgfs9.SectorPointer + 0xE5)) = FreeSEC;
	vgfs9.SectorPointer[0xE4] = FileCount;
	vgfs9.SectorPointer[0xF4] = DelFileCount;
	ApplySectorCRC(vgfs9);
}
//-----------------------------------------------------------------------------
bool unpack_td0(unsigned char *data, long &size);
unsigned short TD0CRC(unsigned char *buf, unsigned int len);

#define WORD2(a,b) ((a)+(b)*0x100)

void TDiskImage::readTD0(int hfile, bool readonly)
{
	long fsize = filelength(hfile);
	if (fsize < 0)
	{
		ShowError(ERR_GETLEN);
		return;
	}
	unsigned char *ptr = (unsigned char*)new char[fsize + 256 * 20000];
	if (!ptr)
	{
		ShowError(ERR_NOMEM);
		return;
	}
	long rsize = read(hfile, ptr, fsize);

	TD0_MAIN_HEADER *td0hdr = (TD0_MAIN_HEADER*)ptr;
	TD0_INFO_DATA *td0inf = (TD0_INFO_DATA*)(ptr + 12);

	if (!rsize)
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}
	if (rsize < 12)      // header
	{
		delete ptr;
		ShowError(ERR_CORRUPT);
		return;
	}

	if ((*(short*)ptr != WORD2('T', 'D')) && (*(short*)ptr != WORD2('t', 'd')))// non TD0
	{
		delete ptr;
		ShowError(ERR_FORMAT" TD0!");
		return;
	}
	if (TD0CRC(ptr, 10) != td0hdr->CRC) // CRC bad...
	{
		delete ptr;
		ShowError(ERR_FILECRC" TD0!");
		return;
	}
	if ((td0hdr->Ver > 21) || (td0hdr->Ver < 10))           // 1.0 <= version <= 2.1...
	{
		delete ptr;
		ShowError(ERR_FILEVER" TD0!");
		return;
	}
	if (td0hdr->DataDOS != 0)           // if DOS allocated sectors only...
	{
		delete ptr;
		ShowError(ERR_TD0DOSALLOC);
		return;
	}
	if (!unpack_td0(ptr, rsize))
	{
		delete ptr;
		ShowError(ERR_FORMAT" TD0!");
		return;
	}

	// loading unpacked TD0...

	int tdOFF = 12;
	if (ptr[7] & 0x80) tdOFF += sizeof(TD0_INFO_DATA) + td0inf->strLen;

	TD0_TRACK_HEADER *tdtrk;
	TD0_SECT_HEADER *tdsect;

	MaxTrack = 0;
	MaxSide = 0;

	for (; tdOFF < rsize;)
	{
		tdtrk = (TD0_TRACK_HEADER*)(ptr + tdOFF);
		tdOFF += sizeof(TD0_TRACK_HEADER);

		if (tdOFF >= rsize) break;
		if (tdtrk->SectorCount == 0xFF) break;   // EOF marker


		unsigned trk = tdtrk->Track;
		unsigned side = tdtrk->Side;
		FTrackLength[trk][side] = 6250;

		// make unformatted track...
		FTracksPtr[trk][side][0] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // trk img
		FTracksPtr[trk][side][1] = (unsigned char*)new char[FTrackLength[trk][side] + 1024];      // clk img
		for (unsigned ij = 0; ij < 6250; ij++)
		{
			FTracksPtr[trk][side][0][ij] = 0x00;
			FTracksPtr[trk][side][1][ij] = 0x00;
		}


		unsigned SecCount = tdtrk->SectorCount;

		unsigned int tmpOFF = tdOFF;

		// Вычисляем необходимое число байт под данные:
		unsigned int trkdatalen = 0;
		unsigned int SL;
		for (unsigned int ilsec = 0; ilsec < SecCount; ilsec++)
		{
			tdsect = (TD0_SECT_HEADER*)(ptr + tmpOFF);
			tmpOFF += sizeof(TD0_SECT_HEADER) + tdsect->DataLength;

			trkdatalen += 2 + 6;     // for marks:   0xA1, 0xFE, 6bytes
			trkdatalen += 4;       // for data header/crc: 0xA1, 0xFB, ...,2bytes
								   //         SL = unsigned(tdsect->ADRM[3]);
								   //         if(!SL) SL = 128;
								   //         else SL = 128 << SL;
			SL = tdsect->DataLength - 1;
			trkdatalen += SL;
		}

		// проверка на возможность формата...
		if (trkdatalen + SecCount*(3 + 2) > 6250)    // 3x4E & 2x00 per sec checking
		{
			delete ptr;
			for (int t = 0; t < 256; t++)
				for (int s = 0; s < 256; s++)
				{
					FTrackLength[t][s] = 0;
					if (FTracksPtr[t][s][0]) delete FTracksPtr[t][s][0];
					FTracksPtr[t][s][0] = NULL;
					if (FTracksPtr[t][s][1]) delete FTracksPtr[t][s][1];
					FTracksPtr[t][s][1] = NULL;
				}
			ShowError(ERR_IMPOSSIBLE);
			return;
		}

		unsigned int FreeSpace = 6250 - (trkdatalen + SecCount*(3 + 2));

		unsigned int SynchroPulseLen = 1; // 1 уже учтен в trkdatalen...
		unsigned int FirstSpaceLen = 1;
		unsigned int SecondSpaceLen = 1;
		unsigned int ThirdSpaceLen = 1;
		unsigned int SynchroSpaceLen = 1;
		FreeSpace -= FirstSpaceLen + SecondSpaceLen + ThirdSpaceLen + SynchroSpaceLen;

		// Распределяем длины пробелов и синхропромежутка:
		while (FreeSpace > 0)
		{
			if (FreeSpace >= (SecCount * 2))
				if (SynchroSpaceLen < 12) { SynchroSpaceLen++; FreeSpace -= SecCount * 2; } // Synchro for ADM & DATA
			if (FreeSpace < SecCount) break;

			if (FirstSpaceLen < 10) { FirstSpaceLen++; FreeSpace -= SecCount; }
			if (FreeSpace < SecCount) break;
			if (SecondSpaceLen < 22) { SecondSpaceLen++; FreeSpace -= SecCount; }
			if (FreeSpace < SecCount) break;
			if (ThirdSpaceLen < 60) { ThirdSpaceLen++; FreeSpace -= SecCount; }
			if (FreeSpace < SecCount) break;

			if ((SynchroSpaceLen >= 12) && (FirstSpaceLen >= 10) && (SecondSpaceLen >= 22) && (ThirdSpaceLen >= 60)) break;
		};
		// по возможности делаем три синхроимпульса...
		if (FreeSpace >(SecCount * 2) + 10) { SynchroPulseLen++; FreeSpace -= SecCount; }
		if (FreeSpace >(SecCount * 2) + 9) SynchroPulseLen++;

		// Форматируем дорожку...

		unsigned int tptr = 0;
		unsigned int ptrcrc;
		unsigned int r;
		unsigned short vgcrc;
		for (unsigned sec = 0; sec < SecCount; sec++)
		{
			tdsect = (TD0_SECT_HEADER*)(ptr + tdOFF);
			tdOFF += sizeof(TD0_SECT_HEADER) + 1;

			for (r = 0; r < FirstSpaceLen; r++)        // Первый пробел
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
			for (r = 0; r < SynchroSpaceLen; r++)        // Синхропромежуток
			{
				FTracksPtr[trk][side][0][tptr] = 0x00;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
			ptrcrc = tptr;
			for (r = 0; r < SynchroPulseLen; r++)        // Синхроимпульс
			{
				FTracksPtr[trk][side][0][tptr] = 0xA1;
				FTracksPtr[trk][side][1][tptr++] = 0xFF;
			}
			FTracksPtr[trk][side][0][tptr] = 0xFE;   // Метка "Адрес"
			FTracksPtr[trk][side][1][tptr++] = 0x00;

			FTracksPtr[trk][side][0][tptr] = tdsect->ADRM[0]; // cyl
			FTracksPtr[trk][side][1][tptr++] = 0x00;
			FTracksPtr[trk][side][0][tptr] = tdsect->ADRM[1]; // head
			FTracksPtr[trk][side][1][tptr++] = 0x00;
			FTracksPtr[trk][side][0][tptr] = tdsect->ADRM[2]; // secN
			FTracksPtr[trk][side][1][tptr++] = 0x00;
			FTracksPtr[trk][side][0][tptr] = tdsect->ADRM[3]; // len code
			FTracksPtr[trk][side][1][tptr++] = 0x00;

			vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);
			FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
			FTracksPtr[trk][side][1][tptr++] = 0x00;
			FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
			FTracksPtr[trk][side][1][tptr++] = 0x00;

			for (r = 0; r < SecondSpaceLen; r++)        // Второй пробел
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
			for (r = 0; r < SynchroSpaceLen; r++)        // Синхропромежуток
			{
				FTracksPtr[trk][side][0][tptr] = 0x00;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}

			if (tdsect->DataLength - 1) // oh-oh, data area not present... ;-)
			{
				ptrcrc = tptr;
				for (r = 0; r < SynchroPulseLen; r++)        // Синхроимпульс
				{
					FTracksPtr[trk][side][0][tptr] = 0xA1;
					FTracksPtr[trk][side][1][tptr++] = 0xFF;
				}

				FTracksPtr[trk][side][0][tptr] = 0xFB;   // Метка "Данные"
				FTracksPtr[trk][side][1][tptr++] = 0x00;

				//            SL = unsigned(tdsect->ADRM[3]);
				//            if(!SL) SL = 128;
				//            else SL = 128 << SL;
				SL = tdsect->DataLength - 1;

				for (r = 0; r < SL; r++)        // сектор SL байт
				{
					FTracksPtr[trk][side][0][tptr] = ptr[tdOFF + r];
					FTracksPtr[trk][side][1][tptr++] = 0x00;
				}
				tdOFF += SL;

				vgcrc = MakeVGCRC(FTracksPtr[trk][side][0] + ptrcrc, tptr - ptrcrc);


				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc >> 8); // VG93 CRC
				FTracksPtr[trk][side][1][tptr++] = 0x00;
				FTracksPtr[trk][side][0][tptr] = (unsigned char)(vgcrc & 0xFF);
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}

			for (r = 0; r < ThirdSpaceLen; r++)        // Третий пробел
			{
				FTracksPtr[trk][side][0][tptr] = 0x4E;
				FTracksPtr[trk][side][1][tptr++] = 0x00;
			}
		}
		for (int eoftrk = tptr; eoftrk < 6250; eoftrk++)
		{
			FTracksPtr[trk][side][0][tptr] = 0x4E;
			FTracksPtr[trk][side][1][tptr++] = 0x00;
		}

		if (unsigned(MaxTrack) < trk) MaxTrack = trk;
		if (unsigned(MaxSide) < side) MaxSide = side;
	}

	delete ptr;
	ReadOnly = readonly;
	FType = DIT_TD0;
	DiskPresent = true;
}
void TDiskImage::writeTD0(int hfile)
{
	TD0_MAIN_HEADER td0hdr;

	td0hdr.ID[0] = 'T'; td0hdr.ID[1] = 'D'; td0hdr.__t = 0x00;
	td0hdr.__1 = 0x00;
	td0hdr.Ver = 21;
	td0hdr.__2 = 0x00;
	td0hdr.DiskType = 0x03;
	td0hdr.Info = 0x00;
	td0hdr.DataDOS = 0x00;
	td0hdr.ChkdSides = 0x02;
	td0hdr.CRC = TD0CRC((unsigned char*)&td0hdr, 10);
	write(hfile, &td0hdr, sizeof(td0hdr));

	//   TD0_INFO_DATA td0inf;

	VGFIND_SECTOR vgfs;
	VGFIND_ADM vgfa;

	TD0_TRACK_HEADER tdtrk;
	TD0_SECT_HEADER tdsect;

	// prepare nullbuf...
	unsigned char nullbuf[256];
	for (int i = 0; i < 256; i++) nullbuf[i] = '*';
	memcpy(nullbuf, errsect, sizeof(errsect));

	unsigned char secbuf[32768];

	unsigned char sectorsMap[256];

	unsigned int SectorCount;
	for (unsigned int trk = 0; trk <= unsigned(MaxTrack); trk++)
		for (unsigned int side = 0; side <= unsigned(MaxSide); side++)
		{
			SectorCount = 0;

			unsigned int TrackPtr = 0;
			bool FirstFindAD = true;
			unsigned int FirstOffset;
			for (;;)
			{
				if (!FindADMark(trk, side, TrackPtr, &vgfa)) break;
				if (!FirstFindAD)
				{
					if (vgfa.OffsetADM == FirstOffset) break;
				}
				else
				{
					FirstOffset = vgfa.OffsetADM;
					FirstFindAD = false;
				}
				TrackPtr = vgfa.OffsetEndADM;

				sectorsMap[SectorCount] = vgfa.ADMPointer[2];

				SectorCount++;
			}
			if (SectorCount > 0xFE) SectorCount = 0xFE;

			tdtrk.SectorCount = (unsigned char)SectorCount;
			tdtrk.Track = (unsigned char)trk;
			tdtrk.Side = (unsigned char)side;
			tdtrk.CRCL = (unsigned char)(TD0CRC((unsigned char*)&tdtrk, 3) & 0xFF);

			write(hfile, &tdtrk, sizeof(TD0_TRACK_HEADER));

			for (unsigned int sec = 0; sec < SectorCount; sec++)
			{
				if (FindSector(trk, side, sectorsMap[sec], &vgfs))
					memcpy(secbuf + 1, vgfs.SectorPointer, vgfs.SectorLength);
				else memcpy(secbuf + 1, nullbuf, vgfs.SectorLength);
				secbuf[0] = 0x00;
				tdsect.ADRM[5] = (unsigned char)(TD0CRC(secbuf, vgfs.SectorLength + 1) & 0xFF);
				tdsect.DataLength = vgfs.SectorLength + 1;
				if (vgfs.vgfa.FoundADM) memcpy(tdsect.ADRM, vgfs.vgfa.ADMPointer, 4);
				tdsect.ADRM[4] = 0x00;

				write(hfile, &tdsect, sizeof(TD0_SECT_HEADER));
				write(hfile, secbuf, tdsect.DataLength);
			}
		}

	tdtrk.SectorCount = 0xFF;
	tdtrk.Track = 0x00;
	tdtrk.Side = 0x00;
	tdtrk.CRCL = 0x00;
	write(hfile, &tdtrk, sizeof(TD0_TRACK_HEADER));

}
//-----------------------------------------------------------------------------

void TDiskImage::ShowError(const char *str)
{
	printf("DiskImage Error: %s\n", str);
}

//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// convert packed td0 to unpacked

unsigned short TD0CRC(unsigned char *buf, unsigned int len);
unsigned unpack_lzh(unsigned char *src, unsigned size, unsigned char *buf);
unsigned char *td0_dst, *td0_src;


void td0_move(unsigned size)
{
	memcpy(td0_dst, td0_src, size);
	td0_dst += size;
	td0_src += size;
}

//----------------------------------------------------------------------------
bool unpack_td0(unsigned char *data, long &size)
{
	if (size < 12) return false;
	if ((*(short*)data != WORD2('T', 'D')) && (*(short*)data != WORD2('t', 'd')))
		return false;             // non TD0
	if (TD0CRC(data, 10) != *((unsigned short*)(data + 0x0A)))
		return false;             // CRC bad...
	if (data[4] > 21)
		return false;             // version > 2.1...

	unsigned char *snbuf = (unsigned char*)new char[size * 2 + 1500000];  // if compressed then UUUUFF ;-/
	if (!snbuf) return false;

	memcpy(snbuf, data, size);
	if (*(short*)snbuf == WORD2('t', 'd')) // packed disk
	{
		if (snbuf[4] < 20)    // unsupported Old Advanced compression
		{
			delete snbuf;
			return false;
		}
		unpack_lzh((unsigned char*)data + 12, size - 12, (unsigned char*)snbuf + 12), *(short*)snbuf = WORD2('T', 'D');
	}

	td0_src = snbuf, td0_dst = data;
	td0_move(12);

	if (snbuf[7] & 0x80)  // additional info...
	{
		unsigned short *cs = (unsigned short*)(snbuf + 12 + 2);

		if (TD0CRC(snbuf + 12 + 2, 8 + *cs) != cs[-1])
		{
			delete snbuf;
			return false;
		}
		td0_move(10);
		td0_move(*((unsigned short*)(snbuf + 12 + 2)));
	}

	for (;;)
	{
		unsigned char s = *td0_src;
		td0_move(4);
		if (s == 0xFF) break;
		for (; s; s--)
		{
			//         unsigned char *sec = td0_src;
			unsigned size = 128; if (td0_src[3]) size <<= td0_src[3];
			td0_move(6);
			*(unsigned short*)td0_dst = size + 1; td0_dst += 2;
			*td0_dst++ = 0;
			unsigned char *dst = td0_dst;
			unsigned src_size = *(unsigned short*)td0_src; td0_src += 2;
			unsigned char *end_packed_data = td0_src + src_size;
			memset(td0_dst, 0, size);
			switch (*td0_src++)
			{
			case 0:
				memcpy(dst, td0_src, src_size - 1); break;
			case 1:
			{
				unsigned n = *(unsigned short*)td0_src;
				td0_src += 2;
				unsigned short data = *(unsigned short*)td0_src;
				for (; n; n--) *(unsigned short*)dst = data, dst += 2;
				break;
			}
			case 2:
			{
				unsigned short data;
				unsigned char s;
				do
				{
					switch (*td0_src++)
					{
					case 0:
						for (s = *td0_src++; s; s--) *dst++ = *td0_src++;
						break;
					case 1:
						s = *td0_src++;
						data = *(unsigned short*)td0_src;
						td0_src += 2;
						for (; s; s--) *(unsigned short*)dst = data, dst += 2;
						break;
					default: shit:
						delete snbuf;
						return false;  // "bad TD0 file"
					}
				} while (td0_src < end_packed_data);
				break;
			}
			default: goto shit;
			}
			td0_dst += size;
			td0_src = end_packed_data;
		}
	}
	size = unsigned(td0_dst) - unsigned(data);
	delete snbuf;
	return true;
}
//----------------------------------------------------------------------------
//
// TD0 CRC - table&proc grabed from TDCHECK.EXE by Alex Makeev
//
unsigned char tbltd0crc[512] = {
	0x00,0x00,0xA0,0x97,0xE1,0xB9,0x41,0x2E,0x63,0xE5,0xC3,0x72,0x82,0x5C,0x22,0xCB,
	0xC7,0xCA,0x67,0x5D,0x26,0x73,0x86,0xE4,0xA4,0x2F,0x04,0xB8,0x45,0x96,0xE5,0x01,
	0x2F,0x03,0x8F,0x94,0xCE,0xBA,0x6E,0x2D,0x4C,0xE6,0xEC,0x71,0xAD,0x5F,0x0D,0xC8,
	0xE8,0xC9,0x48,0x5E,0x09,0x70,0xA9,0xE7,0x8B,0x2C,0x2B,0xBB,0x6A,0x95,0xCA,0x02,
	0x5E,0x06,0xFE,0x91,0xBF,0xBF,0x1F,0x28,0x3D,0xE3,0x9D,0x74,0xDC,0x5A,0x7C,0xCD,
	0x99,0xCC,0x39,0x5B,0x78,0x75,0xD8,0xE2,0xFA,0x29,0x5A,0xBE,0x1B,0x90,0xBB,0x07,
	0x71,0x05,0xD1,0x92,0x90,0xBC,0x30,0x2B,0x12,0xE0,0xB2,0x77,0xF3,0x59,0x53,0xCE,
	0xB6,0xCF,0x16,0x58,0x57,0x76,0xF7,0xE1,0xD5,0x2A,0x75,0xBD,0x34,0x93,0x94,0x04,
	0xBC,0x0C,0x1C,0x9B,0x5D,0xB5,0xFD,0x22,0xDF,0xE9,0x7F,0x7E,0x3E,0x50,0x9E,0xC7,
	0x7B,0xC6,0xDB,0x51,0x9A,0x7F,0x3A,0xE8,0x18,0x23,0xB8,0xB4,0xF9,0x9A,0x59,0x0D,
	0x93,0x0F,0x33,0x98,0x72,0xB6,0xD2,0x21,0xF0,0xEA,0x50,0x7D,0x11,0x53,0xB1,0xC4,
	0x54,0xC5,0xF4,0x52,0xB5,0x7C,0x15,0xEB,0x37,0x20,0x97,0xB7,0xD6,0x99,0x76,0x0E,
	0xE2,0x0A,0x42,0x9D,0x03,0xB3,0xA3,0x24,0x81,0xEF,0x21,0x78,0x60,0x56,0xC0,0xC1,
	0x25,0xC0,0x85,0x57,0xC4,0x79,0x64,0xEE,0x46,0x25,0xE6,0xB2,0xA7,0x9C,0x07,0x0B,
	0xCD,0x09,0x6D,0x9E,0x2C,0xB0,0x8C,0x27,0xAE,0xEC,0x0E,0x7B,0x4F,0x55,0xEF,0xC2,
	0x0A,0xC3,0xAA,0x54,0xEB,0x7A,0x4B,0xED,0x69,0x26,0xC9,0xB1,0x88,0x9F,0x28,0x08,
	0xD8,0x8F,0x78,0x18,0x39,0x36,0x99,0xA1,0xBB,0x6A,0x1B,0xFD,0x5A,0xD3,0xFA,0x44,
	0x1F,0x45,0xBF,0xD2,0xFE,0xFC,0x5E,0x6B,0x7C,0xA0,0xDC,0x37,0x9D,0x19,0x3D,0x8E,
	0xF7,0x8C,0x57,0x1B,0x16,0x35,0xB6,0xA2,0x94,0x69,0x34,0xFE,0x75,0xD0,0xD5,0x47,
	0x30,0x46,0x90,0xD1,0xD1,0xFF,0x71,0x68,0x53,0xA3,0xF3,0x34,0xB2,0x1A,0x12,0x8D,
	0x86,0x89,0x26,0x1E,0x67,0x30,0xC7,0xA7,0xE5,0x6C,0x45,0xFB,0x04,0xD5,0xA4,0x42,
	0x41,0x43,0xE1,0xD4,0xA0,0xFA,0x00,0x6D,0x22,0xA6,0x82,0x31,0xC3,0x1F,0x63,0x88,
	0xA9,0x8A,0x09,0x1D,0x48,0x33,0xE8,0xA4,0xCA,0x6F,0x6A,0xF8,0x2B,0xD6,0x8B,0x41,
	0x6E,0x40,0xCE,0xD7,0x8F,0xF9,0x2F,0x6E,0x0D,0xA5,0xAD,0x32,0xEC,0x1C,0x4C,0x8B,
	0x64,0x83,0xC4,0x14,0x85,0x3A,0x25,0xAD,0x07,0x66,0xA7,0xF1,0xE6,0xDF,0x46,0x48,
	0xA3,0x49,0x03,0xDE,0x42,0xF0,0xE2,0x67,0xC0,0xAC,0x60,0x3B,0x21,0x15,0x81,0x82,
	0x4B,0x80,0xEB,0x17,0xAA,0x39,0x0A,0xAE,0x28,0x65,0x88,0xF2,0xC9,0xDC,0x69,0x4B,
	0x8C,0x4A,0x2C,0xDD,0x6D,0xF3,0xCD,0x64,0xEF,0xAF,0x4F,0x38,0x0E,0x16,0xAE,0x81,
	0x3A,0x85,0x9A,0x12,0xDB,0x3C,0x7B,0xAB,0x59,0x60,0xF9,0xF7,0xB8,0xD9,0x18,0x4E,
	0xFD,0x4F,0x5D,0xD8,0x1C,0xF6,0xBC,0x61,0x9E,0xAA,0x3E,0x3D,0x7F,0x13,0xDF,0x84,
	0x15,0x86,0xB5,0x11,0xF4,0x3F,0x54,0xA8,0x76,0x63,0xD6,0xF4,0x97,0xDA,0x37,0x4D,
	0xD2,0x4C,0x72,0xDB,0x33,0xF5,0x93,0x62,0xB1,0xA9,0x11,0x3E,0x50,0x10,0xF0,0x87,
};
unsigned short TD0CRC(unsigned char *buf, unsigned int len)
{
	unsigned short CRC = 0;
	int j;
	for (unsigned int i = 0; i < len; i++)
	{
		CRC ^= *buf++;
		j = CRC & 0xFF;
		CRC &= 0xFF00;
		CRC = (CRC << 8) | (CRC >> 8);
		CRC ^= ((unsigned short*)tbltd0crc)[j];
	}
	return (CRC << 8) | (CRC >> 8);
}
// ----------------------------------------------------------------------------





unsigned char *packed_ptr, *packed_end;

int readChar(void)
{
	if (packed_ptr < packed_end) return *packed_ptr++;
	else return -1;
}

// ------------------------------------------------------ LZH unpacker

unsigned char d_code[256] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
	0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
	0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
	0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
	0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
	0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
	0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
	0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
	0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
	0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
	0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

unsigned char d_len[256] = {
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};


const int N = 4096;     // buffer size
const int F = 60;       // lookahead buffer size
const int THRESHOLD = 2;
const int NIL = N;      // leaf of tree

unsigned char text_buf[N + F - 1];

const int N_CHAR = (256 - THRESHOLD + F);       // kinds of characters (character code = 0..N_CHAR-1)
const int T = (N_CHAR * 2 - 1);       // size of table
const int R = (T - 1);                  // position of root
const int MAX_FREQ = 0x8000;            // updates tree when the
										// root frequency comes to this value.

unsigned short freq[T + 1];        // frequency table

short prnt[T + N_CHAR]; // pointers to parent nodes, except for the
						// elements [T..T + N_CHAR - 1] which are used to get
						// the positions of leaves corresponding to the codes.
short son[T];           // pointers to child nodes (son[], son[] + 1)


int r;

unsigned getbuf;
unsigned char getlen;

int GetBit(void)      /* get one bit */
{
	int i;

	while (getlen <= 8)
	{
		if ((i = readChar()) == -1) i = 0;
		getbuf |= i << (8 - getlen);
		getlen += 8;
	}
	i = getbuf;
	getbuf <<= 1;
	getlen--;
	return ((i >> 15) & 1);
}

int GetByte(void)     /* get one byte */
{
	unsigned i;

	while (getlen <= 8)
	{
		if ((i = readChar()) == -1) i = 0;
		getbuf |= i << (8 - getlen);
		getlen += 8;
	}
	i = getbuf;
	getbuf <<= 8;
	getlen -= 8;
	return (i >> 8) & 0xFF;
}

void StartHuff(void)
{
	int i, j;

	getbuf = 0, getlen = 0;
	for (i = 0; i < N_CHAR; i++) {
		freq[i] = 1;
		son[i] = i + T;
		prnt[i + T] = i;
	}
	i = 0; j = N_CHAR;
	while (j <= R) {
		freq[j] = freq[i] + freq[i + 1];
		son[j] = i;
		prnt[i] = prnt[i + 1] = j;
		i += 2; j++;
	}
	freq[T] = 0xffff;
	prnt[R] = 0;

	for (i = 0; i < N - F; i++) text_buf[i] = ' ';
	r = N - F;
}

/* reconstruction of tree */
void reconst(void)
{
	int i, j, k;
	int f, l;

	/* collect leaf nodes in the first half of the table */
	/* and replace the freq by (freq + 1) / 2. */
	j = 0;
	for (i = 0; i < T; i++)
	{
		if (son[i] >= T)
		{
			freq[j] = (freq[i] + 1) / 2;
			son[j] = son[i];
			j++;
		}
	}
	/* begin constructing tree by connecting sons */
	for (i = 0, j = N_CHAR; j < T; i += 2, j++)
	{
		k = i + 1;
		f = freq[j] = freq[i] + freq[k];
		for (k = j - 1; f < freq[k]; k--);
		k++;
		l = (j - k) * sizeof(*freq);
		memmove(&freq[k + 1], &freq[k], l);
		freq[k] = f;
		memmove(&son[k + 1], &son[k], l);
		son[k] = i;
	}
	/* connect prnt */
	for (i = 0; i < T; i++)
		if ((k = son[i]) >= T) prnt[k] = i;
		else prnt[k] = prnt[k + 1] = i;
}


/* increment frequency of given code by one, and update tree */

void update(int c)
{
	int i, j, k, l;

	if (freq[R] == MAX_FREQ) reconst();

	c = prnt[c + T];
	do {
		k = ++freq[c];

		/* if the order is disturbed, exchange nodes */
		if (k > freq[l = c + 1])
		{
			while (k > freq[++l]);
			l--;
			freq[c] = freq[l];
			freq[l] = k;

			i = son[c];
			prnt[i] = l;
			if (i < T) prnt[i + 1] = l;

			j = son[l];
			son[l] = i;

			prnt[j] = c;
			if (j < T) prnt[j + 1] = c;
			son[c] = j;

			c = l;
		}
	} while ((c = prnt[c]) != 0);  /* repeat up to root */
}

int DecodeChar(void)
{
	int c;

	c = son[R];

	/* travel from root to leaf, */
	/* choosing the smaller child node (son[]) if the read bit is 0, */
	/* the bigger (son[]+1} if 1 */
	while (c < T) c = son[c + GetBit()];
	c -= T;
	update(c);
	return c;
}

int DecodePosition(void)
{
	int i, j, c;

	/* recover upper 6 bits from table */
	i = GetByte();
	c = (int)d_code[i] << 6;
	j = d_len[i];
	/* read lower 6 bits verbatim */
	j -= 2;
	while (j--) i = (i << 1) + GetBit();
	return c | (i & 0x3f);
}

unsigned unpack_lzh(unsigned char *src, unsigned size, unsigned char *buf)
{
	packed_ptr = src;
	packed_end = src + size;
	int  i, j, k, c;
	unsigned count = 0;
	StartHuff();

	//  while (count < textsize)  // textsize - sizeof unpacked data
	while (packed_ptr < packed_end)
	{
		c = DecodeChar();
		if (c < 256)
		{
			*buf++ = c;
			text_buf[r++] = c;
			r &= (N - 1);
			count++;
		}
		else {
			i = (r - DecodePosition() - 1) & (N - 1);
			j = c - 255 + THRESHOLD;
			for (k = 0; k < j; k++)
			{
				c = text_buf[(i + k) & (N - 1)];
				*buf++ = c;
				text_buf[r++] = c;
				r &= (N - 1);
				count++;
			}
		}
	}
	return count;
}

//--------------------------------------------------------------------------

extern "C" int x2trd(char *name, fileTYPE *f)
{
	TDiskImage *img = new TDiskImage;
	img->Open(getFullPath(name), true);

	if (!FileOpenEx(f, "vtrd", -1))
	{
		delete img;
		printf("ERROR: fail to create /vtrd\n");
		return 0;
	}

	img->writeTRD(f->fd);
	delete(img);

	struct stat64 st;
	int ret = fstat64(f->fd, &st);
	if (ret < 0)
	{
		printf("x2trd(fstat) error: %d.\n", ret);
		FileClose(f);
		return 0;
	}

	f->size = st.st_size;
	FileSeekLBA(f, 0);
	printf("x2trd: vtrd size=%llu.\n", f->size);

	return 1;
}

extern "C" int x2trd_ext_supp(char *name)
{
	const char *ext = "";
	if (strlen(name) > 4) ext = name + strlen(name) - 4;
	return (!strcasecmp(ext, ".scl") || !strcasecmp(ext, ".fdi") || !strcasecmp(ext, ".udi"));
}
