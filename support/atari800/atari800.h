#ifndef __ATARI800_H__
#define __ATARI800_H__

// #include "../../file_io.h"

#define LOAD_CHUNK_SIZE 4096 // 4096 is the minimum! TODO solve this better

int atari800_get_match_cart_count();
const char *atari800_get_cart_match_name(int match_index);
void atari800_init();
void atari800_reset();
void atari800_poll();
void atari800_umount_cartridge(uint8_t stacked);
int atari800_check_cartridge_file(const char* name, unsigned char index);
void atari800_open_cartridge_file(const char* name, int match_index);
void atari800_open_bios_file(const char* name, unsigned char index);

// Cart modes from the original ZCPU firmware

// 8k modes (0xA000-$BFFF)
#define TC_MODE_OFF             0x00           // cart disabled
#define TC_MODE_8K              0x01           // 8k banks at $A000
#define TC_MODE_ATARIMAX1       0x02           // 8k using Atarimax 1MBit compatible banking
#define TC_MODE_ATARIMAX8       0x03           // 8k using Atarimax 8MBit compatible banking
#define TC_MODE_ATARIMAX8_2     0x10           // 8k using Atarimax 8MBit compatible banking (new type)
#define TC_MODE_DCART           0x11           // 512K DCart
#define TC_MODE_OSS_16          0x04           // 16k OSS cart, M091 banking
#define TC_MODE_OSS_8           0x05           // 8k OSS cart, M091 banking
#define TC_MODE_OSS_043M        0x06           // 16k OSS cart, 043M banking

#define TC_MODE_SDX64           0x08           // SDX 64k cart, $D5Ex banking
#define TC_MODE_SDX128          0x09           // SDX 128k cart, $D5Ex banking
#define TC_MODE_DIAMOND64       0x0A           // Diamond GOS 64k cart, $D5Dx banking
#define TC_MODE_EXPRESS64       0x0B           // Express 64k cart, $D57x banking

#define TC_MODE_ATRAX128        0x0C           // Atrax 128k cart
#define TC_MODE_WILLIAMS64      0x0D           // Williams 64k cart
#define TC_MODE_WILLIAMS32      0x0E           // Williams 32k cart
#define TC_MODE_WILLIAMS16      0x0F           // Williams 16k cart

// 16k modes (0x8000-$BFFF)
//#define TC_MODE_FLEXI           0x20           // flexi mode
#define TC_MODE_16K             0x21           // 16k banks at $8000-$BFFF
#define TC_MODE_MEGAMAX16       0x22           // MegaMax 16k mode (up to 2MB)
#define TC_MODE_BLIZZARD        0x23           // Blizzard 16k
#define TC_MODE_SIC_128         0x24           // Sic!Cart 128k
#define TC_MODE_SIC_256         0x25           // Sic!Cart 256k
#define TC_MODE_SIC_512         0x26           // Sic!Cart 512k
#define TC_MODE_SIC_1024        0x27           // Sic!Cart+ 1024k

#define TC_MODE_BLIZZARD_4      0x12           // Blizzard 4k
#define TC_MODE_BLIZZARD_32     0x13           // Blizzard 32k
#define TC_MODE_RIGHT_8K	0x14
#define TC_MODE_RIGHT_4K	0x15
#define TC_MODE_2K		0x16
#define TC_MODE_4K		0x17

// J(atari)Cart versions
#define TC_MODE_JATARI_8	0x18
#define TC_MODE_JATARI_16	0x19
#define TC_MODE_JATARI_32	0x1A
#define TC_MODE_JATARI_64	0x1B
#define TC_MODE_JATARI_128	0x1C
#define TC_MODE_JATARI_256	0x1D
#define TC_MODE_JATARI_512	0x1E
#define TC_MODE_JATARI_1024	0x1F

#define TC_MODE_MEGA_16         0x28           // switchable MegaCarts
#define TC_MODE_MEGA_32         0x29
#define TC_MODE_MEGA_64         0x2A
#define TC_MODE_MEGA_128        0x2B
#define TC_MODE_MEGA_256        0x2C
#define TC_MODE_MEGA_512        0x2D
#define TC_MODE_MEGA_1024       0x2E
#define TC_MODE_MEGA_2048       0x2F
#define TC_MODE_MEGA_4096       0x20

#define TC_MODE_XEGS_32         0x30           // non-switchable XEGS carts
#define TC_MODE_XEGS_64         0x31
#define TC_MODE_XEGS_128        0x32
#define TC_MODE_XEGS_256        0x33
#define TC_MODE_XEGS_512        0x34
#define TC_MODE_XEGS_1024       0x35
#define TC_MODE_XEGS_64_2       0x36

#define TC_MODE_SXEGS_32        0x38           // switchable XEGS carts
#define TC_MODE_SXEGS_64        0x39
#define TC_MODE_SXEGS_128       0x3A
#define TC_MODE_SXEGS_256       0x3B
#define TC_MODE_SXEGS_512       0x3C
#define TC_MODE_SXEGS_1024      0x3D

// XE Multicart versions
#define TC_MODE_XEMULTI_8	0x68
#define TC_MODE_XEMULTI_16	0x69
#define TC_MODE_XEMULTI_32	0x6A
#define TC_MODE_XEMULTI_64	0x6B
#define TC_MODE_XEMULTI_128	0x6C
#define TC_MODE_XEMULTI_256	0x6D
#define TC_MODE_XEMULTI_512	0x6E
#define TC_MODE_XEMULTI_1024	0x6F

#define TC_MODE_PHOENIX		0x40
#define TC_MODE_AST_32		0x41
#define TC_MODE_ATRAX_INT128	0x42
#define TC_MODE_ATRAX_SDX64	0x43
#define TC_MODE_ATRAX_SDX128	0x44
#define TC_MODE_TSOFT_64	0x45
#define TC_MODE_TSOFT_128	0x46
#define TC_MODE_ULTRA_32	0x47
#define TC_MODE_DAWLI_32	0x48
#define TC_MODE_DAWLI_64	0x49
#define TC_MODE_JRC_LIN_64	0x4A
#define TC_MODE_JRC_INT_64	0x4B
#define TC_MODE_SDX_SIDE2	0x4C
#define TC_MODE_SDX_U1MB	0x4D
#define TC_MODE_DB_32		0x70
#define TC_MODE_BOUNTY_40	0x73

#endif
