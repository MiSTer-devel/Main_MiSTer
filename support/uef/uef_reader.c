/*--------------------------------------------------------------------
 *  Modified for MiSTer by alanswx 2022
 *
 *  Originally from Replay Firmware
*/

/*--------------------------------------------------------------------
 *                       Replay Firmware
 *                      www.fpgaarcade.com
 *                     All rights reserved.
 *
 *                     admin@fpgaarcade.com
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *--------------------------------------------------------------------
 *
 * Copyright (c) 2020, The FPGAArcade community (see AUTHORS.txt)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>
#include <assert.h>


#include <zlib.h>


#define UEF_ChunkHeaderSize (sizeof(uint16_t) + sizeof(uint32_t))
#define UEF_infoID      0x0000
#define UEF_tapeID      0x0100
#define UEF_highToneID  0x0110
#define UEF_highDummyID 0x0111
#define UEF_gapID       0x0112
#define UEF_freqChgID   0x0113
#define UEF_securityID  0x0114
#define UEF_floatGapID  0x0116
#define UEF_startBit    0
#define UEF_stopBit     1
#define UEF_Baud        (1000000.0/(16.0*52.0))

typedef struct {
    // UEF header
    uint16_t    id;
    uint32_t    length;
    // chunk data
    uint32_t    file_offset;
    uint32_t    bit_offset_start;
    uint32_t    bit_offset_end;
    uint32_t    pre_carrier;
} __attribute__((packed)) ChunkInfo;

// TODO - allocate this as part of the desc structure
static ChunkInfo s_ChunkData = { 0 };



static uint16_t ReadChunkHeader(FILE* f, ChunkInfo* chunk)
{
    chunk->id = -1;
    chunk->length = 0;

    if (fread(chunk, 1, UEF_ChunkHeaderSize, f) != UEF_ChunkHeaderSize) {
        return (-1);
    }

    fprintf(stderr, "ReadChunkHeader: chunk->id %d chunk->length %d\n", chunk->id,chunk->length);
    return chunk->id;
}

static ChunkInfo* GetChunkAtPosFile(FILE *f, uint32_t* p_bit_pos)
{
    uint32_t bit_pos = *p_bit_pos;
    uint8_t length_check = (bit_pos == 0xffffffff);
    ChunkInfo* chunk = &s_ChunkData;

    fprintf(stderr, "Find pos : %d\n", bit_pos);

    if (chunk->bit_offset_start <= bit_pos && bit_pos < chunk->bit_offset_end) {
        bit_pos -= chunk->bit_offset_start;
        *p_bit_pos = bit_pos;
        return chunk;
    }

    uint32_t chunk_start = 0;
    uint32_t chunk_bitlen = 0;

    if (chunk->bit_offset_end != 0 && bit_pos >= chunk->bit_offset_end) {
        fseek(f, chunk->file_offset + chunk->length, SEEK_SET);
        chunk_start = chunk->bit_offset_end;
        bit_pos -= chunk_start;

    } else {
        fseek(f, 12, SEEK_SET);     // sizeof(UEF_header)
    }

    chunk->bit_offset_end = 0;

    while (!feof(f)) {
        uint16_t id = ReadChunkHeader(f, chunk);

        if (id == (uint16_t) - 1) {
            break;
        }

        fprintf(stderr, "Parse ChunkID : %04x - Length : %4d bytes (%04x) - Offset = %d\n", chunk->id, chunk->length, chunk->length, (uint32_t)ftell(f));
        chunk->file_offset = ftell(f);

        if (UEF_tapeID == id || UEF_gapID == id || UEF_highToneID == id || UEF_highDummyID == id) {

            if (id == UEF_tapeID) {
                chunk_bitlen = chunk->length * 10;

            } else if (id == UEF_gapID || id == UEF_highToneID) {
                uint16_t ms;

                if (fread(&ms, 1, sizeof(ms), f) != sizeof(ms)) {
                    break;
                }

                chunk_bitlen = ms * (UEF_Baud / 1000.0);
                fseek(f, -sizeof(ms), SEEK_CUR);

            } else if (id == UEF_highDummyID) {
                uint16_t ms;

                if (fread(&ms, 1, sizeof(ms), f) != sizeof(ms)) {
                    break;
                }

                chunk->pre_carrier = ms * (UEF_Baud / 1000.0);

                if (fread(&ms, 1, sizeof(ms), f) != sizeof(ms)) {
                    break;
                }

                uint32_t post_carrier = ms * (UEF_Baud / 1000.0);
                chunk_bitlen = chunk->pre_carrier + 20 + post_carrier;
                fseek(f, -sizeof(ms) * 2, SEEK_CUR);
            }

            if (bit_pos < chunk_bitlen) {
                chunk->bit_offset_start = chunk_start;
                chunk->bit_offset_end = chunk_start + chunk_bitlen;
                break;
            }

            bit_pos -= chunk_bitlen;
            chunk_start += chunk_bitlen;

        } else if (length_check) {

            if (UEF_infoID == id) {
                char buffer[64];
                uint32_t length = chunk->length;

                while (length > 0) {
                    uint32_t read_len = length;

                    if (read_len > sizeof(buffer) - 1) {
                        read_len = sizeof(buffer) - 1;
                    }

                    if (fread(buffer, 1, read_len, f) != read_len) {
                        break;
                    }

                    buffer[read_len] = '\0';
                    fprintf(stderr, "Drv02:UEF Info : '%s'", buffer);

                    length -= read_len;
                }

            } else if (UEF_freqChgID == id) {
                float freq;

                if (fread(&freq, 1, sizeof(freq), f) != sizeof(freq)) {
                    break;
                }

                fprintf(stderr, "Drv02:Ignoring base frequency change : %d", (int)freq);

            } else if (UEF_floatGapID == id) {
                float gap;

                if (fread(&gap, 1, sizeof(gap), f) != sizeof(gap)) {
                    break;
                }

                fprintf(stderr, "Drv02:Ignoring floating point gap : %d ms", (int)(gap * 1000.f));

            } else if (UEF_securityID == id) {

                fprintf(stderr, "Drv02:UEF security block ignored");

            } else {
                fprintf(stderr, "Drv02:Unknown UEF block ID %04x", id);
            }

            fseek(f, chunk->file_offset, SEEK_SET);
        }

        fseek(f, chunk->length, SEEK_CUR);
    }

    /*DEBUG(1, "OK!");*/

    *p_bit_pos = bit_pos;
    return chunk->bit_offset_end ? chunk : 0;
}

static uint8_t GetBitAtPos(FILE *f, uint32_t bit_pos)
{
    ChunkInfo* info = GetChunkAtPosFile(f, &bit_pos);

    if (!info) {
        return 0;
    }

    uint16_t id = info->id;

    if (id == UEF_gapID) {
        return 0;

    } else if (id == UEF_highToneID) {
        return 1;
    }

    if (id == UEF_tapeID) {

        uint32_t byte_offset = bit_pos / 10;
        uint32_t bit_offset = bit_pos - byte_offset * 10;

        if (bit_offset == 0) {
            return UEF_startBit;
        }

        if (bit_offset == 9) {
            return UEF_stopBit;
        }

        uint8_t byte;
        fseek(f, info->file_offset + byte_offset, SEEK_SET);
        fread(&byte, 1, sizeof(byte), f);

        bit_offset -= 1;        // E (0,7)
        assert(bit_offset < 8);

        return (byte & (1 << bit_offset)) ? 1 : 0;
    }

    assert(id == UEF_highDummyID);

    if ((bit_pos < info->pre_carrier) || (bit_pos >= info->pre_carrier + 20)) {
        return 1;
    }

    bit_pos -= info->pre_carrier;
    bit_pos %= 10;

    if (bit_pos == 0) {
        return UEF_startBit;
    }

    if (bit_pos == 9) {
        return UEF_stopBit;
    }

    bit_pos -= 1;       // E (0,7)
    assert(bit_pos < 8);
    uint8_t byte = 'A';

    return (byte & (1 << bit_pos)) ? 1 : 0;
}


#define BUFLEN      16384
void gz_uncompress(in, out)
    gzFile in;
    FILE   *out;
{
    char buf[BUFLEN];
    int len;
    int err;

    for (;;) {
        len = gzread(in, buf, sizeof(buf));
        if (len < 0) fprintf(stderr,"len < 0 error ungzip\n"); //	(gzerror(in, &err));
        if (len == 0) break;

        if ((int)fwrite(buf, 1, (unsigned)len, out) != len) {
            fprintf(stderr,"failed fwrite\n");
        }
    }

    if (gzclose(in) != Z_OK) fprintf(stderr,"failed gzclose\n");
}



int convert_uef(char *filename)
{
	FILE *infile = fopen(filename,"rb");

	FILE *outfile = fopen("tape.raw","wb");
	fseek(infile, 0L, SEEK_END);
	long int size =ftell(infile);
	fprintf(stderr,"size: %ld\n",size);
	rewind(infile);
#define kBufferSize 4096
        int buf_size=kBufferSize;
	unsigned char fbuf[kBufferSize];
	int addr=0;

        typedef struct {
            char    ueftag[10];
            uint8_t minor_version;
            uint8_t major_version;
        } UEF_header;
        UEF_header header;

        fseek(infile, 0, SEEK_SET);


	// the UAE file might be gzipped, if so we need to ungzip it
	// gzip : 1f 8b
        if (fread(&fbuf, 1, 2,infile) !=2)
	{
		fprintf(stderr,"error reading 2 bytes of file\n");
		return 0;
	}
	// if it isn't gzipped, we need to rewind to the beginning
	rewind(infile);

	// 1f 8b is the gzip magic number
	if (fbuf[0]==0x1f && fbuf[1]==0x8b) {
		gzFile in;
		fprintf(stderr,"we have a gzipped file\n");
		// close the file so we can gzopen it
		fclose(infile);


    		in = gzopen(filename, "rb");
    		if (in == NULL) {
        		fprintf(stderr, "can't gzopen %s\n",  filename);
			return 0;
    		}
 
		//FILE *out = fopen("TEMP","wb");//tmpfile();
		FILE *out = tmpfile();

    		char buf[BUFLEN];
    		int len;
    		int err;

		gz_uncompress(in, out);
		fflush(out);
		infile=out;
		rewind(infile);
		fseek(infile, 0L, SEEK_END);
		size =ftell(infile);
		fprintf(stderr,"size: %ld\n",size);
		rewind(infile);

	}
	else {
		fprintf(stderr,"UEF is not compressed\n");
	}

        if (fread(&header, 1, sizeof(UEF_header), infile) != sizeof(UEF_header)) {
            fprintf(stderr,"Couldn't read file header\n");

        } else if (memcmp(header.ueftag, "UEF File!\0", sizeof(header.ueftag)) != 0) {
            fprintf(stderr,"UEF file header mismatch\n");
            fprintf(stderr,"File compressed?\n");

        } else {
            fprintf(stderr,"UEF: %s %d %d\n",header.ueftag,header.minor_version,header.major_version);
	
    memset(&s_ChunkData, 0x00, sizeof(ChunkInfo));
	while (size>0) {
        // this is a very naive conversion, but it'll have to do for now..
        for (uint32_t pos = 0; pos < buf_size; ++pos) {
            uint8_t val = 0;
            for (uint32_t bit = 0; bit < 8; ++bit) {
                val = val << 1;
                val = val | GetBitAtPos(infile, ((addr + pos) << 3) + bit);
            }

            fbuf[pos] = val;
        }
	fwrite(fbuf,1,buf_size,outfile);
        size -= buf_size;
        addr += buf_size;
	}
	fclose(infile);
	fclose(outfile);
	}
}
int main(int argc, char *argv[]) {
	return convert_uef(argv[1]);
}

