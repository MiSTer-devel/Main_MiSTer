/*  
This file contains lookup information on known controllers
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "timer.h"
#include "debug.h"
#include "joymapping.h"

/*****************************************************************************/

char* get_joystick_alias( uint16_t vid, uint16_t pid ) {

    if(vid==0x0F30 && pid==0x1012)
        return JOYSTICK_ALIAS_QANBA_Q4RAF;

    if(vid==0x081F && pid==0xE401)
        return JOYSTICK_ALIAS_CHEAP_SNES;

    if(vid==0x0583 && pid==0x2060)
        return JOYSTICK_ALIAS_IBUFALLO_SNES;

    if(vid==0x0411 && pid==0x00C6) 
        return JOYSTICK_ALIAS_IBUFALLO_SNES;

    if (vid==VID_RETROLINK && pid==0x0006)
        return JOYSTICK_ALIAS_RETROLINK_GC;

    if (vid==VID_RETROLINK && pid==0x0011)
        return JOYSTICK_ALIAS_RETROLINK_NES;

    if(vid==0x1F4F && pid==0x0003) 
        return JOYSTICK_ALIAS_ROYDS_EX;

    if(vid==VID_DAPTOR && pid==0xF947)
        return JOYSTICK_ALIAS_ATARI_DAPTOR2;

    if(vid==VID_DAPTOR && pid==0xF421)
        return JOYSTICK_ALIAS_NEOGEO_DAPTOR;

    if(vid==VID_DAPTOR && pid==0xF6EC)
        return JOYSTICK_ALIAS_NEOGEO_DAPTOR;

    if(vid==VID_DAPTOR && pid==0xF672)
        return JOYSTICK_ALIAS_VISION_DAPTOR;
        
    if(vid==0x1345 && pid==0x1030)
        return JOYSTICK_ALIAS_RETRO_FREAK;

    if(vid==0x1235 &&  (pid==0xab11 || pid==0xab21))
        return JOYSTICK_ALIAS_8BITDO_SFC30;

    if(vid==0x1002 &&  pid==0x9000)
        return JOYSTICK_ALIAS_8BITDO_FC30;

    if(vid==0x040b && pid==0x6533)
        return JOYSTICK_ALIAS_SPEEDLINK_COMP;

    if(vid==0x0738 && pid==0x2217)
        return JOYSTICK_ALIAS_SPEEDLINK_COMP;
    
    if(vid==0x0F0D && pid==0x0086)
        return JOYSTICK_ALIAS_HORI_FC4;
    
    if(vid==0x16d0 && pid==0x0D04)
        return JOYSTICK_ALIAS_BLISSTER;
    
    if(vid==VID_NINTENDO) {
        if (pid==0x0306)
            return JOYSTICK_ALIAS_WIIMOTE;
        if (pid==0x0300)
            return JOYSTICK_ALIAS_WIIU;
        if (pid==0x2009)
            return JOYSTICK_ALIAS_SWITCH_PRO;
    }
    
    if(vid==VID_SONY) {
        if (pid==0x0268)
            return JOYSTICK_ALIAS_DS3;
        if (pid==0x05c4)
            return JOYSTICK_ALIAS_DS4;
    }
    
    return JOYSTICK_ALIAS_NONE;
        
    }