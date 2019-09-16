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
const char * JOYSTICK_ALIAS_8BITDO_SFC30   = "8BitDo SFC30";
const char * JOYSTICK_ALIAS_8BITDO_FC30    = "8BitDo FC30";
const char * JOYSTICK_ALIAS_8BITDO_SN30PRO = "8BitDo SN30 pro";

const char * JOYSTICK_ALIAS_ATARI_DAPTOR2  = "2600-daptor II";
const char * JOYSTICK_ALIAS_BLISSTER       = "BlisSTer";
const char * JOYSTICK_ALIAS_CHEAP_SNES     = "SNES Generic Pad";
const char * JOYSTICK_ALIAS_DS3            = "Sony Dual Shock 3";
const char * JOYSTICK_ALIAS_DS4            = "Sony Dual Shock 4";
const char * JOYSTICK_ALIAS_HORI_FC4       = "Hori Fighter Commander 4";
const char * JOYSTICK_ALIAS_HORI_FC4_PS3   = "Hori Fighter Cmdr 4 (PS3)";
const char * JOYSTICK_ALIAS_HORI_FC4_PS4   = "Hori Fighter Cmdr 4 (PS4)";
const char * JOYSTICK_ALIAS_HORI_VLX       = "Hori RAP Premium VLX";
const char * JOYSTICK_ALIAS_IBUFALLO_SNES  = "iBuffalo SFC BSGP801";
const char * JOYSTICK_ALIAS_IBUFALLO_NES   = "iBuffalo FC BGCFC801";
const char * JOYSTICK_ALIAS_PS_ADAPTER_    = "PlayStation USB adapter";
const char * JOYSTICK_ALIAS_QANBA_Q4RAF    = "Qanba Q4RAF";
const char * JOYSTICK_ALIAS_RETROLINK_GC   = "Retrolink N64/GC";
const char * JOYSTICK_ALIAS_RETROLINK_NES  = "Retrolink NES";
const char * JOYSTICK_ALIAS_RETRO_FREAK    = "Retro Freak gamepad";
const char * JOYSTICK_ALIAS_ROYDS_EX       = "ROYDS Stick.EX";
const char * JOYSTICK_ALIAS_SPEEDLINK_COMP = "Speedlink Competition Pro";
const char * JOYSTICK_ALIAS_SWITCH_PRO     = "Nintendo Switch Pro";
const char * JOYSTICK_ALIAS_NEOGEO_DAPTOR  = "NEOGEO-daptor";
const char *JOYSTICK_ALIAS_NEOGEO_X       = "NEOGEO X Arcade Stick";
const char * JOYSTICK_ALIAS_VISION_DAPTOR  = "Vision-daptor";
const char * JOYSTICK_ALIAS_WIIMOTE        = "Nintendo WiiMote";
const char * JOYSTICK_ALIAS_WIIU           = "Nintendo Wii U";


/*****************************************************************************/

/*const char *get_joystick_name(uint16_t vid, uint16_t pid) {
    // return "TEST OUTPUT";                        // <- works
    // return JOYSTICK_ALIAS_HORI_FC4.c_str();      // <- works
    //return get_joystick_alias(vid, pid).c_str();  // <- doesn't woprk if returns a std::string
    return get_joystick_alias(vid, pid);            // <- works if returns a const char *
}*/

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
                
                case 0x05c4: return JOYSTICK_ALIAS_DS4;
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
        default:
            break;
    }
    if(vid==0x0583 and 0x2060)
        return JOYSTICK_ALIAS_IBUFALLO_SNES;
    return JOYSTICK_ALIAS_NONE;
}