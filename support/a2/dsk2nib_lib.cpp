#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../file_io.h"
#include "../../user_io.h"
#include "../../hardware.h"


#include "dsk2nib_lib.h"

// Constants from original dsk2nib.c
#define TRACKS_PER_DISK     35
#define SECTORS_PER_TRACK   16
#define BYTES_PER_SECTOR    256
#define BYTES_PER_TRACK     4096
#define PRIMARY_BUF_LEN     256
#define SECONDARY_BUF_LEN   86
#define DATA_LEN            (PRIMARY_BUF_LEN+SECONDARY_BUF_LEN)
#define PROLOG_LEN          3
#define EPILOG_LEN          3
#define GAP1_LEN            48
#define GAP2_LEN            5
#define BYTES_PER_NIB_SECTOR 416
#define BYTES_PER_NIB_TRACK  6656
#define DEFAULT_VOLUME      254
#define GAP_BYTE            0xff

// Structures from original
typedef struct {
    uchar prolog[ PROLOG_LEN ];
    uchar volume[ 2 ];
    uchar track[ 2 ];
    uchar sector[ 2 ];
    uchar checksum[ 2 ];
    uchar epilog[ EPILOG_LEN ];
} addr_t;

typedef struct {
    uchar prolog[ PROLOG_LEN ];
    uchar data[ DATA_LEN ];
    uchar data_checksum;
    uchar epilog[ EPILOG_LEN ];
} data_t;

typedef struct {
    uchar gap1[ GAP1_LEN ];
    addr_t addr;
    uchar gap2[ GAP2_LEN ];
    data_t data;
} nib_sector_t;

// Static data from original
static uchar addr_prolog[] = { 0xd5, 0xaa, 0x96 };
static uchar addr_epilog[] = { 0xde, 0xaa, 0xeb };
static uchar data_prolog[] = { 0xd5, 0xaa, 0xad };
static uchar data_epilog[] = { 0xde, 0xaa, 0xeb };
static int soft_interleave[ SECTORS_PER_TRACK ] =
    { 0, 7, 0xE, 6, 0xD, 5, 0xC, 4, 0xB, 3, 0xA, 2, 9, 1, 8, 0xF };
static int phys_interleave[ SECTORS_PER_TRACK ] =
    { 0, 0xD, 0xB, 9, 7, 5, 3, 1, 0xE, 0xC, 0xA, 8, 6, 4, 2, 0xF };

// Translation table for 6+2 encoding
static uchar table[ 0x40 ] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

// Helper functions from original
static uchar translate(uchar byte) {
    return table[byte & 0x3f];
}

static void odd_even_encode(uchar a[], int i) {
    a[0] = (i >> 1) & 0x55;
    a[0] |= 0xaa;
    a[1] = i & 0x55;
    a[1] |= 0xaa;
}

static void nibbilize(uchar *src, data_t *data_field) {
    int i, index, section;
    uchar pair;
    uchar primary_buf[PRIMARY_BUF_LEN];
    uchar secondary_buf[SECONDARY_BUF_LEN];
    uchar *dest = data_field->data;

    // Clear buffers
    memset(primary_buf, 0, PRIMARY_BUF_LEN);
    memset(secondary_buf, 0, SECONDARY_BUF_LEN);

    // Nibbilize data into primary and secondary buffers
    for (i = 0; i < PRIMARY_BUF_LEN; i++) {
        primary_buf[i] = src[i] >> 2;
        
        index = i % SECONDARY_BUF_LEN;
        section = i / SECONDARY_BUF_LEN;
        pair = ((src[i]&2)>>1) | ((src[i]&1)<<1);       // swap the low bits
        secondary_buf[index] |= pair << (section*2);
    }

    // XOR pairs of nibbilized bytes in correct order
    index = 0;
    dest[index++] = translate(secondary_buf[0]);

    for (i = 1; i < SECONDARY_BUF_LEN; i++)
        dest[index++] = translate(secondary_buf[i] ^ secondary_buf[i-1]);

    dest[index++] = translate(primary_buf[0] ^ secondary_buf[SECONDARY_BUF_LEN-1]);

    for (i = 1; i < PRIMARY_BUF_LEN; i++)
        dest[index++] = translate(primary_buf[i] ^ primary_buf[i-1]);

    data_field->data_checksum = translate(primary_buf[PRIMARY_BUF_LEN-1]);
}


// Convert NIB physical sector to logical sector using interleave tables
static int phys_to_logical_sector(int phys_sector) {
    // Find which logical sector maps to this physical sector
    for (int i = 0; i < SECTORS_PER_TRACK; i++) {
        if (phys_interleave[i] == phys_sector) {
            return i;
        }
    }
    return 0; // fallback
}

void a2_readDsk2Nib(fileTYPE*fd, uint64_t offset, uchar *byte) {
    int nib_track = offset / BYTES_PER_NIB_TRACK;
    uint64_t track_offset = offset % BYTES_PER_NIB_TRACK;
    int volume = DEFAULT_VOLUME;
    
    // Bounds check
    if (nib_track >= TRACKS_PER_DISK) {
        memset(byte, 0, 512);
        return;
    }
    
    // Build entire NIB track in memory
    uchar nib_track_data[BYTES_PER_NIB_TRACK];
    
    // Process all 16 sectors in this track
    for (int phys_sector = 0; phys_sector < SECTORS_PER_TRACK; phys_sector++) {
        // Convert physical sector to logical sector
        int logical_sector = phys_to_logical_sector(phys_sector);
        
        // Get corresponding DSK soft sector
        int dsk_soft_sector = soft_interleave[logical_sector];
        
        // Read DSK sector data
        uchar dsk_sector[BYTES_PER_SECTOR];
        off_t dsk_offset = (off_t)nib_track * BYTES_PER_TRACK + (off_t)dsk_soft_sector * BYTES_PER_SECTOR;

	if (FileSeek(fd, dsk_offset, SEEK_SET))
        {
             if (FileReadAdv(fd, dsk_sector, BYTES_PER_SECTOR))
             {
		     // good
             }
	     else {
            	memset(dsk_sector, 0, BYTES_PER_SECTOR);
	     }
        } else {
            memset(dsk_sector, 0, BYTES_PER_SECTOR);
	}

        
        //if (lseek(fd, dsk_offset, SEEK_SET) == -1) {
        //} else if (read(fd, dsk_sector, BYTES_PER_SECTOR) != BYTES_PER_SECTOR) {
        //    memset(dsk_sector, 0, BYTES_PER_SECTOR);
        //}
        
        // Build NIB sector structure
        nib_sector_t nib_sector;
        
        // Initialize gaps
        memset(nib_sector.gap1, GAP_BYTE, GAP1_LEN);
        memset(nib_sector.gap2, GAP_BYTE, GAP2_LEN);
        
        // Set address field
        memcpy(nib_sector.addr.prolog, addr_prolog, 3);
        memcpy(nib_sector.addr.epilog, addr_epilog, 3);
        odd_even_encode(nib_sector.addr.volume, volume);
        odd_even_encode(nib_sector.addr.track, nib_track);
        odd_even_encode(nib_sector.addr.sector, logical_sector);
        int csum = volume ^ nib_track ^ logical_sector;
        odd_even_encode(nib_sector.addr.checksum, csum);
        
        // Set data field
        memcpy(nib_sector.data.prolog, data_prolog, 3);
        memcpy(nib_sector.data.epilog, data_epilog, 3);
        nibbilize(dsk_sector, &nib_sector.data);
        
        // Copy this sector to the track buffer
        memcpy(nib_track_data + phys_sector * BYTES_PER_NIB_SECTOR, &nib_sector, sizeof(nib_sector));
    }
    
    // Copy requested 512 bytes from the track
    int bytes_to_copy = 512;
    int available_bytes = BYTES_PER_NIB_TRACK - track_offset;
    
    if (available_bytes <= 0) {
        memset(byte, 0, 512);
        return;
    }
    
    if (bytes_to_copy > available_bytes) {
        bytes_to_copy = available_bytes;
    }
    
    memcpy(byte, nib_track_data + track_offset, bytes_to_copy);
    
    // Fill remaining bytes with zeros if needed
    if (bytes_to_copy < 512) {
        memset(byte + bytes_to_copy, 0, 512 - bytes_to_copy);
    }
}

// Helper functions for NIB to DSK conversion
static uchar odd_even_decode(uchar byte1, uchar byte2) {
    uchar byte;
    byte = (byte1 << 1) & 0xaa;
    byte |= byte2 & 0x55;
    return byte;
}

static uchar untranslate(uchar x) {
    uchar *ptr;
    int index;
    if ((ptr = (uchar*)memchr(table, x, 0x40)) == NULL) {
        return 0; // Invalid byte, return 0 instead of fatal error
    }
    index = ptr - table;
    return index;
}

// Parse a NIB sector from byte stream and extract DSK data
static int parse_nib_sector(uchar *nib_data, int data_len, uchar *dsk_sector, int *track, int *sector) {
    int pos = 0;
    int state = 0;
    uchar primary_buf[PRIMARY_BUF_LEN];
    uchar secondary_buf[SECONDARY_BUF_LEN];
    uchar checksum;
    int i;
    
    // State machine to parse NIB sector
    while (pos < data_len) {
        uchar byte = nib_data[pos++];
        
        switch (state) {
            case 0: // Looking for address prolog D5
                if (byte == 0xd5) state = 1;
                break;
                
            case 1: // Looking for address prolog AA
                if (byte == 0xaa) state = 2;
                else state = 0;
                break;
                
            case 2: // Looking for address prolog 96
                if (byte == 0x96) state = 3;
                else state = 0;
                break;
                
            case 3: // Read volume (first byte)
                if (pos >= data_len) return 0;
                odd_even_decode(byte, nib_data[pos++]); // Read volume but don't store
                state = 4;
                break;
                
            case 4: // Read track (first byte)
                if (pos >= data_len) return 0;
                *track = odd_even_decode(byte, nib_data[pos++]);
                state = 5;
                break;
                
            case 5: // Read sector (first byte)
                if (pos >= data_len) return 0;
                *sector = odd_even_decode(byte, nib_data[pos++]);
                state = 6;
                break;
                
            case 6: // Read checksum (first byte)
                if (pos >= data_len) return 0;
                pos++; // Skip checksum, we don't validate it
                state = 7;
                break;
                
            case 7: // Skip address epilog DE
                if (byte == 0xde) state = 8;
                break;
                
            case 8: // Skip address epilog AA
                if (byte == 0xaa) state = 9;
                else state = 7;
                break;
                
            case 9: // Skip address epilog EB, look for data prolog D5
                if (byte == 0xd5) state = 10;
                break;
                
            case 10: // Looking for data prolog AA
                if (byte == 0xaa) state = 11;
                else state = 9;
                break;
                
            case 11: // Looking for data prolog AD
                if (byte == 0xad) state = 12;
                else state = 9;
                break;
                
            case 12: // Process data field
                // Read and decode the 342 data bytes plus checksum
                checksum = untranslate(byte);
                secondary_buf[0] = checksum;
                
                // Read secondary buffer (85 more bytes)
                for (i = 1; i < SECONDARY_BUF_LEN; i++) {
                    if (pos >= data_len) return 0;
                    checksum ^= untranslate(nib_data[pos++]);
                    secondary_buf[i] = checksum;
                }
                
                // Read primary buffer (256 bytes)
                for (i = 0; i < PRIMARY_BUF_LEN; i++) {
                    if (pos >= data_len) return 0;
                    checksum ^= untranslate(nib_data[pos++]);
                    primary_buf[i] = checksum;
                }
                
                // Read and validate checksum
                if (pos >= data_len) return 0;
                checksum ^= untranslate(nib_data[pos++]);
                // Ignore checksum validation for now
                
                // Denibbilize - reconstruct the 256-byte sector
                for (i = 0; i < PRIMARY_BUF_LEN; i++) {
                    int index = i % SECONDARY_BUF_LEN;
                    uchar bit0, bit1;
                    
                    switch (i / SECONDARY_BUF_LEN) {
                        case 0:
                            bit0 = (secondary_buf[index] & 2) > 0;
                            bit1 = (secondary_buf[index] & 1) > 0;
                            break;
                        case 1:
                            bit0 = (secondary_buf[index] & 8) > 0;
                            bit1 = (secondary_buf[index] & 4) > 0;
                            break;
                        case 2:
                            bit0 = (secondary_buf[index] & 0x20) > 0;
                            bit1 = (secondary_buf[index] & 0x10) > 0;
                            break;
                        default:
                            bit0 = bit1 = 0;
                            break;
                    }
                    dsk_sector[i] = (primary_buf[i] << 2) | (bit1 << 1) | bit0;
                }
                return 1; // Success
                
            default:
                state = 0;
                break;
        }
    }
    return 0; // Failed to parse
}

void a2_writeDSK(fileTYPE* idx, uint64_t lba, int ack) {
   //printf("a2_writeDSK(lba:%lld ack:%d\n",lba,ack);
	// Fetch sector data from FPGA ...
        uchar chunk[512];
	EnableIO();
        spi_w(UIO_SECTOR_WR | ack);
        spi_block_read(chunk, user_io_get_width(), 512);
        DisableIO();

	a2_writeNib2Dsk(idx, lba*512, chunk);
}
	
void a2_readDSK(fileTYPE* idx, uint64_t lba, int ack) {
   //printf("a2_readDSK(lba:%lld ack:%d\n",lba,ack);
        uchar chunk[512];
        
	a2_readDsk2Nib(idx, lba*512, chunk);

	//printf("%x %x %x %x %x\n",chunk[0],chunk[1],chunk[2],chunk[3],chunk[4]);

        EnableIO();
        spi_w(UIO_SECTOR_RD | ack);
        spi_block_write(chunk, user_io_get_width(), 512);
        DisableIO();
}


void a2_writeNib2Dsk(fileTYPE*fd, uint64_t offset, uchar *byte) {
    int nib_track = offset / BYTES_PER_NIB_TRACK;
    uint64_t track_offset = offset % BYTES_PER_NIB_TRACK;
    
    // Bounds check
    if (nib_track >= TRACKS_PER_DISK) {
        return;
    }
    
    // We need to accumulate a full track's worth of NIB data to properly decode
    // This is a simplified approach - we'll try to parse sectors from the given 512 bytes
    static uchar track_buffer[BYTES_PER_NIB_TRACK];
    static int current_track = -1;
    static int bytes_accumulated = 0;
    
    // If this is a new track, reset the buffer
    if (current_track != nib_track) {
        current_track = nib_track;
        bytes_accumulated = 0;
        memset(track_buffer, 0, BYTES_PER_NIB_TRACK);
    }
    
    // Copy the 512 bytes into our track buffer at the appropriate offset
    int copy_len = 512;
    if (track_offset + copy_len > BYTES_PER_NIB_TRACK) {
        copy_len = BYTES_PER_NIB_TRACK - track_offset;
    }
    
    if (copy_len > 0) {
        memcpy(track_buffer + track_offset, byte, copy_len);
        bytes_accumulated += copy_len;
    }
    
    // Try to parse and write any complete sectors we can find
    // Look for sectors in the accumulated data
    int pos = 0;
    while (pos < bytes_accumulated - 400) { // Need at least 400 bytes for a sector
        uchar dsk_sector[BYTES_PER_SECTOR];
        int track_num, sector_num;
        
        if (parse_nib_sector(track_buffer + pos, bytes_accumulated - pos, dsk_sector, &track_num, &sector_num)) {
            // Successfully parsed a sector
            if (track_num == nib_track && sector_num < SECTORS_PER_TRACK) {
                // Map logical sector to soft sector using interleave
                int soft_sector = soft_interleave[sector_num];
                
                // Calculate DSK file offset
                off_t dsk_offset = (off_t)track_num * BYTES_PER_TRACK + (off_t)soft_sector * BYTES_PER_SECTOR;
                
                // Write sector to DSK file
                //if (lseek(fd, dsk_offset, SEEK_SET) != -1) {
                if (FileSeek(fd,dsk_offset, SEEK_SET))
	//			if (lseek(fd, dsk_offset, SEEK_SET) != -1) {
		    FileWriteAdv(fd, dsk_sector,BYTES_PER_SECTOR);
                    //write(fd, dsk_sector, BYTES_PER_SECTOR);
                //}
            }
            pos += BYTES_PER_NIB_SECTOR; // Move to next sector
        } else {
            pos++; // Try next byte position
        }
    }
}
