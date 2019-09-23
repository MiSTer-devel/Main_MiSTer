/*  
This file contains lookup information on known controllers
*/

#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "joymapping.h"

/*****************************************************************************/

static const char * JOYSTICK_ALIAS_NONE    = "";
static const char * JOYSTICK_ALIAS_5200_DAPTOR2   = "5200-daptor";
static const char * JOYSTICK_ALIAS_8BITDO_SFC30   = "8BitDo SFC30";
static const char * JOYSTICK_ALIAS_8BITDO_FC30    = "8BitDo FC30";
static const char * JOYSTICK_ALIAS_8BITDO_SN30PRO = "8BitDo SN30 pro";

static const char * JOYSTICK_ALIAS_ATARI_DAPTOR2  = "2600-daptor II";
static const char * JOYSTICK_ALIAS_BLISSTER       = "BlisSTer";
static const char * JOYSTICK_ALIAS_CHEAP_SNES     = "SNES Generic Pad";
static const char * JOYSTICK_ALIAS_DS3            = "Sony Dual Shock 3";
static const char * JOYSTICK_ALIAS_DS4            = "Sony Dual Shock 4";
static const char * JOYSTICK_ALIAS_HORI_FC4       = "Hori Fighter Commander 4";
static const char * JOYSTICK_ALIAS_HORI_FC4_PS3   = "Hori Fighter Cmdr 4 (PS3)";
static const char * JOYSTICK_ALIAS_HORI_FC4_PS4   = "Hori Fighter Cmdr 4 (PS4)";
static const char * JOYSTICK_ALIAS_HORI_VLX       = "Hori RAP Premium VLX";
static const char * JOYSTICK_ALIAS_IBUFALLO_SNES  = "iBuffalo SFC BSGP801";
static const char * JOYSTICK_ALIAS_IBUFALLO_NES   = "iBuffalo FC BGCFC801";
static const char * JOYSTICK_ALIAS_PS_ADAPTER_    = "PlayStation USB adapter";
static const char * JOYSTICK_ALIAS_QANBA_Q4RAF    = "Qanba Q4RAF";
static const char * JOYSTICK_ALIAS_RETROLINK_GC   = "Retrolink N64/GC";
static const char * JOYSTICK_ALIAS_RETROLINK_NES  = "Retrolink NES";
static const char * JOYSTICK_ALIAS_RETRO_FREAK    = "Retro Freak gamepad";
static const char * JOYSTICK_ALIAS_ROYDS_EX       = "ROYDS Stick.EX";
static const char * JOYSTICK_ALIAS_SPEEDLINK_COMP = "Speedlink Competition Pro";
static const char * JOYSTICK_ALIAS_SWITCH_PRO     = "Nintendo Switch Pro";
static const char * JOYSTICK_ALIAS_NEOGEO_DAPTOR  = "NEOGEO-daptor";
static const char *JOYSTICK_ALIAS_NEOGEO_X       = "NEOGEO X Arcade Stick";
static const char * JOYSTICK_ALIAS_VISION_DAPTOR  = "Vision-daptor";
static const char * JOYSTICK_ALIAS_WIIMOTE        = "Nintendo WiiMote";
static const char * JOYSTICK_ALIAS_WIIU           = "Nintendo Wii U";


/*****************************************************************************/

const char *get_joystick_alias( uint16_t vid, uint16_t pid ) {

    switch(vid) {
        // ----
        case VID_DAPTOR:
            switch(pid) {
                case 0xF947: return JOYSTICK_ALIAS_ATARI_DAPTOR2;
                case 0xF421: return JOYSTICK_ALIAS_NEOGEO_DAPTOR;
                case 0xF6EC: return JOYSTICK_ALIAS_5200_DAPTOR2;
                case 0xF672: return JOYSTICK_ALIAS_VISION_DAPTOR;
            }
            break;
        // ----
        case VID_RETROLINK:
            switch(pid) {
                case 0x0006: return JOYSTICK_ALIAS_RETROLINK_GC;
                case 0x0011: return JOYSTICK_ALIAS_RETROLINK_NES;
            }
            break;
        // ----
        case VID_NINTENDO:
            switch(pid) {
                case 0x2009: return JOYSTICK_ALIAS_SWITCH_PRO;
                case 0x0300: return JOYSTICK_ALIAS_WIIU;
                case 0x0306: return JOYSTICK_ALIAS_WIIMOTE;
            }
            break;
        // ----
        case VID_SONY:
            switch(pid) {
                case 0x0268: return JOYSTICK_ALIAS_DS3;
                case 0x05c4: return JOYSTICK_ALIAS_DS4;
                case 0x054c: return JOYSTICK_ALIAS_8BITDO_SN30PRO; //8bitdo being sneaky sneaky
            }
            break;
        // ----
        case VID_HORI:
            switch(pid) {
                case 0x0084: return JOYSTICK_ALIAS_HORI_FC4_PS4;
                case 0x0085: return JOYSTICK_ALIAS_HORI_FC4_PS3;
                case 0x0086: return JOYSTICK_ALIAS_HORI_FC4;
                case 0x0070: return JOYSTICK_ALIAS_HORI_VLX;
            }
            break;
        // ----
        case 0x040b:
            if(pid==0x6533) return JOYSTICK_ALIAS_SPEEDLINK_COMP;
            break;
        case 0x0411:
            if(pid==0x00C6) return JOYSTICK_ALIAS_IBUFALLO_NES;
            break;
        case 0x0583:
            if(pid==0x2060) return JOYSTICK_ALIAS_IBUFALLO_SNES;
            break;
        case 0x0738:
            if(pid==0x2217) return JOYSTICK_ALIAS_SPEEDLINK_COMP;
            break;
        case 0x0810:
            if(pid==0x0003) return JOYSTICK_ALIAS_PS_ADAPTER_;
            break;
        case 0x081F:
            if(pid==0xE401) return JOYSTICK_ALIAS_CHEAP_SNES;
            break;
        case 0x0F30:
            if(pid==0x1012) return JOYSTICK_ALIAS_QANBA_Q4RAF;
            break;
        case 0x1002:
            if(pid==0x9000) return JOYSTICK_ALIAS_8BITDO_FC30;
            break;
        case 0x1235:
            if(pid==0xab11 || pid==0xab21) return JOYSTICK_ALIAS_8BITDO_SFC30;
            break;
        case 0x1292:
            if(pid==0x4e47) return JOYSTICK_ALIAS_NEOGEO_X;
            break;
        case 0x1345:
            if(pid==0x1030) return JOYSTICK_ALIAS_RETRO_FREAK;
            break;
        case 0x16d0:
            if(pid==0x2217) return JOYSTICK_ALIAS_BLISSTER;
            break;
        case 0x1F4F:
            if(pid==0x0003) return JOYSTICK_ALIAS_ROYDS_EX;
            break;
        case 0x2dc8:
            if(pid==0xab21) return JOYSTICK_ALIAS_8BITDO_FC30;
            break;
        default:
            break;
    }
    if(vid==0x0583 and 0x2060)
        return JOYSTICK_ALIAS_IBUFALLO_SNES;
    return JOYSTICK_ALIAS_NONE;
}


int map_snes2neogeo(devInput (&input)[NUMDEV], int dev) {
    /*
       converts a SNES USB gamepad map into a NG map
       we try to keep the same physical layout between SNES and NG gamepads
       i.e.:
              SNES              X         NG:            D
                     Sel Sta  Y   A              Sel   C   B
                                B                Sta     A
    # SNES
    SNES_A = 4
    SNES_B = 5
    SNES_X = 6
    SNES_Y = 7
    SNES_L = 8
    SNES_R = 9
    SNES_SELECT = 10
    SNES_START = 11
    # NEOGEO gamepad
    NG_A = 4
    NG_B = 5
    NG_C = 6
    NG_D = 7
    NG_SELECT = 8
    NG_START = 9
    */
    uint32_t val;
    // switch A and B
    val = input[dev].map[4];
    input[dev].map[4] = input[dev].map[5];
    input[dev].map[5] = val;
    // switch X and Y
    val = input[dev].map[6];
    input[dev].map[6] = input[dev].map[7];
    input[dev].map[7] = val;
    // reassign Start and Select
    input[dev].map[8] = input[dev].map[11];
    input[dev].map[9] = input[dev].map[10];
    //blank out the rest
    input[dev].map[10] = 0;
    input[dev].map[11] = 0;
    return 1;
}

/*****************************************************************************/

int map_snes2md(devInput (&input)[NUMDEV], int dev) {
    /*
       convert a SNES USB gamepad map into a MD/Genesis map
       we try to keep the same physical layout between SNES and MD gamepads
       i.e.:
                                                                 Z
              SNES          L   X   R      MD:        Mode     Y   C
                     Sel Sta  Y   A              Sta         X   B
                                B                              A
      to support fighting games out of the box, we will map C to R and Z to L
    # Genesis/Megadrive gamepad
    MD_A = 4
    MD_B = 5
    MD_C = 6
    MD_START = 7
    MD_MODE = 8
    MD_X = 9
    MD_Y = 10
    MD_Z = 11
    */
    uint32_t val;
    // switch A and B
    val = input[dev].map[4];
    input[dev].map[4] = input[dev].map[5];
    input[dev].map[5] = val;
    // set C to R
    val = input[dev].map[6];
    input[dev].map[6] = input[dev].map[9];  // C      <- SNES_R
    input[dev].map[9] = input[dev].map[7];  // X      <- SNES_Y
    input[dev].map[7] = input[dev].map[11]; // START  <- SNES_START
    input[dev].map[11] = input[dev].map[8]; // Z      <- SNES_L
    input[dev].map[8] = input[dev].map[10]; // MODE   <- SNES_SELECT
    input[dev].map[10] = val;               // Y      <- SNES_X
    return 1;
}

/*****************************************************************************/

int map_snes2gb(devInput (&input)[NUMDEV], int dev) {
    /*convert a SNES map into a GB map
       we try to keep the same physical layout between SNES and GB gamepads
       i.e.:
              SNES          L   X   R      NES/GB: 
                     Sel Sta  Y   A              Sel Sta     A
                                B                          B
    */
    // A and B keep current positions
    // assign select and start
    input[dev].map[6] = input[dev].map[10]; // SELECT   <- SNES_SELECT
    input[dev].map[7] = input[dev].map[11]; // START  <- SNES_START
    // blank rest of the map
    for(int i=1; i < NUMBUTTONS-7; i++)
        input[dev].map[7+i] = 0;
    return 1;
}

/*****************************************************************************/

int map_snes2pce(devInput (&input)[NUMDEV], int dev) {
    /*convert a SNES map into a PCE map
       we try to keep the same physical layout between SNES and PCE gamepads
       i.e.:
                                                                 VI
              SNES          L   X   R      PCE:                V    I
                     Sel Sta  Y   A              Sel  Run   IV   II
                                B                             III
      this layout conversion is awkward as we need to support 2 buttons; we will assign III to R and IV to L
      in  other words, we make the front diamond correspond to the top buttons instead of the lower buttons as in the GEN case
    # PCE gamepad
    PCE_I = 4
    PCE_II = 5
    PCE_SELECT = 6
    PCE_RUN = 7
    PCE_III= 8
    PCE_IV = 9
    PCE_V = 10
    PCE_VI = 11
    */
    uint32_t val;
    // PCE_I is same as SNES_A and PCE_II same as SNES_B
    val = input[dev].map[6];
    input[dev].map[6] = input[dev].map[10]; //  PCE_SELECT <- SNES_SELECT
    input[dev].map[10] = input[dev].map[7]; //  PCE_V      <- SNES_Y
    input[dev].map[7] = input[dev].map[11]; //  PCE_RUN    <- SNES_START
    input[dev].map[11] = val;              //  PCE_VI     <- SNES_X
    //input[dev].map[8] - remains same
    return 1;
}

/*****************************************************************************/

int map_snes2sms(devInput (&input)[NUMDEV], int dev) {
    // SNES to SMS conversion, simple pad with 3 buttons, 
    // but differnet layout to SNES:  Pause  I  II
    uint32_t val;
    val = input[dev].map[4];
    input[dev].map[4] = input[dev].map[5];  // SMS_II     <- SNES_B
    input[dev].map[5] = val;                // SMS_I      <- SNES A
    input[dev].map[6] = input[dev].map[11]; // SMS_PAUSE <- SNES_START
    // blank rest of the map
    for(int i=1; i < NUMBUTTONS-6; i++)
        input[dev].map[6+i] = 0;
    return 1;
}

/*****************************************************************************/

int map_snes2c64(devInput (&input)[NUMDEV], int dev) {
    // conversion with 3 buttons, also works for many cores
    uint32_t val;
    val = input[dev].map[4];
    input[dev].map[4] = input[dev].map[5]; // SNES_B
    input[dev].map[5] = val;               // SNES A
    input[dev].map[6] = input[dev].map[7]; // SNES_Y
    // blank rest of the map
    for(int i=1; i < NUMBUTTONS-6; i++)
        input[dev].map[6+i] = 0;
    return 1;
}

/*****************************************************************************/

int map_snes2apple2(devInput (&input)[NUMDEV], int dev) {
    // conversion with 2 buttons, also works for many cores
    uint32_t val;
    val = input[dev].map[4];
    input[dev].map[4] = input[dev].map[5]; // SNES_B
    input[dev].map[5] = val;               // SNES A
    // blank rest of the map
    for(int i=1; i < NUMBUTTONS-5; i++)
        input[dev].map[5+i] = 0;
    return 1;
}