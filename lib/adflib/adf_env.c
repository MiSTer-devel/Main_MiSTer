/*
 * ADF Library
 *
 * adf_env.c
 *
 *  $Id$
 *
 * library context and customization code
 *
 *  This file is part of ADFLib.
 *
 *  ADFLib is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  ADFLib is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Foobar; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include<stdio.h>
#include<stdlib.h>

#include"adf_defs.h"
#include"adf_str.h"
#include"adf_nativ.h"
#include"adf_env.h"

#include"defendian.h"

union u{
    int32_t l;
    char c[4];
    };

ENV_DECLARATION;

void rwHeadAccess(SECTNUM physical, SECTNUM logical, BOOL write)
{
    /* display the physical sector, the logical block, and if the access is read or write */

    fprintf(stderr, "phy %d / log %d : %c\n", physical, logical, write ? 'W' : 'R');
}

void progressBar(int perCentDone)
{
    fprintf(stderr,"%d %% done\n",perCentDone);
}

void Warning(char* msg) {
    fprintf(stderr,"Warning <%s>\n",msg);
}

void ADFLibError(char* msg) {
    fprintf(stderr,"Error <%s>\n",msg);
/*    exit(1);*/
}

void Verbose(char* msg) {
    fprintf(stderr,"Verbose <%s>\n",msg);
}

void Changed(SECTNUM nSect, int changedType)
{
/*    switch(changedType) {
    case ST_FILE:
        fprintf(stderr,"Notification : sector %ld (FILE)\n",nSect);
        break;
    case ST_DIR:
        fprintf(stderr,"Notification : sector %ld (DIR)\n",nSect);
        break;
    case ST_ROOT:
        fprintf(stderr,"Notification : sector %ld (ROOT)\n",nSect);
        break;
    default:
        fprintf(stderr,"Notification : sector %ld (???)\n",nSect);
    }
*/}

/*
 * adfInitEnv
 *
 */
void adfEnvInitDefault()
{
/*    char str[80];*/
    union u val;

    /* internal checking */

    if (sizeof(short)!=2) 
        { fprintf(stderr,"Compilation error : sizeof(short)!=2\n"); exit(1); }
    if (sizeof(int32_t)!=4) 
        { fprintf(stderr,"Compilation error : sizeof(short)!=2\n"); exit(1); }
    if (sizeof(struct bEntryBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bEntryBlock)!=512\n"); exit(1); }
    if (sizeof(struct bRootBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bRootBlock)!=512\n"); exit(1); }
    if (sizeof(struct bDirBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bDirBlock)!=512\n"); exit(1); }
    if (sizeof(struct bBootBlock)!=1024)
        { fprintf(stderr,"Internal error : sizeof(struct bBootBlock)!=1024\n"); exit(1); }
    if (sizeof(struct bFileHeaderBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bFileHeaderBlock)!=512\n"); exit(1); }
    if (sizeof(struct bFileExtBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bFileExtBlock)!=512\n"); exit(1); }
    if (sizeof(struct bOFSDataBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bOFSDataBlock)!=512\n"); exit(1); }
    if (sizeof(struct bBitmapBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bBitmapBlock)!=512\n"); exit(1); }
    if (sizeof(struct bBitmapExtBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bBitmapExtBlock)!=512\n"); exit(1); }
    if (sizeof(struct bLinkBlock)!=512)
        { fprintf(stderr,"Internal error : sizeof(struct bLinkBlock)!=512\n"); exit(1); }

    val.l=1L;
/* if LITT_ENDIAN not defined : must be BIG endian */
#ifndef LITT_ENDIAN
    if (val.c[3]!=1) /* little endian : LITT_ENDIAN must be defined ! */
        { fprintf(stderr,"Compilation error : #define LITT_ENDIAN must exist\n"); exit(1); }
#else
    if (val.c[3]==1) /* big endian : LITT_ENDIAN must not be defined ! */
        { fprintf(stderr,"Compilation error : #define LITT_ENDIAN must not exist\n"); exit(1); }
#endif

    adfEnv.wFct = Warning;
    adfEnv.eFct = ADFLibError;
    adfEnv.vFct = Verbose;
    adfEnv.notifyFct = Changed;
    adfEnv.rwhAccess = rwHeadAccess;
    adfEnv.progressBar = progressBar;
	
    adfEnv.useDirCache = FALSE;
    adfEnv.useRWAccess = FALSE;
    adfEnv.useNotify = FALSE;
    adfEnv.useProgressBar = FALSE;

/*    sprintf(str,"ADFlib %s (%s)",adfGetVersionNumber(),adfGetVersionDate());
    (*adfEnv.vFct)(str);
*/
    adfEnv.nativeFct=(struct nativeFunctions*)malloc(sizeof(struct nativeFunctions));
    if (!adfEnv.nativeFct) (*adfEnv.wFct)("adfInitDefaultEnv : malloc");

    adfInitNativeFct();
}


/*
 * adfEnvCleanUp
 *
 */
void adfEnvCleanUp()
{
    free(adfEnv.nativeFct);
}


/*
 * adfChgEnvProp
 *
 * compilation warnings
 * adf_env.c: In function adfChgEnvProp:
 * adf_env.c:176: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:179: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:182: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:185: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:192: warning: ISO C forbids conversion of object pointer to function pointer type
 * adf_env.c:203: warning: ISO C forbids conversion of object pointer to function pointer type
 *
 */
void adfChgEnvProp(int prop, void *new)
{
	BOOL *newBool;
/*    int *newInt;*/

    switch(prop) {
    case PR_VFCT:
        adfEnv.vFct = (void(*)(char*))new;
        break;
    case PR_WFCT:
        adfEnv.wFct = (void(*)(char*))new;
        break;
    case PR_EFCT:
        adfEnv.eFct = (void(*)(char*))new;
        break;
    case PR_NOTFCT:
        adfEnv.notifyFct = (void(*)(SECTNUM,int))new;
        break;
    case PR_USE_NOTFCT:
        newBool = (BOOL*)new;
		adfEnv.useNotify = *newBool;        
        break;
    case PR_PROGBAR:
        adfEnv.progressBar = (void(*)(int))new;
        break;
    case PR_USE_PROGBAR:
        newBool = (BOOL*)new;
		adfEnv.useProgressBar = *newBool;        
        break;
    case PR_USE_RWACCESS:
        newBool = (BOOL*)new;
		adfEnv.useRWAccess = *newBool;        
        break;
    case PR_RWACCESS:
        adfEnv.rwhAccess = (void(*)(SECTNUM,SECTNUM,BOOL))new;
        break;
    case PR_USEDIRC:
        newBool = (BOOL*)new;
		adfEnv.useDirCache = *newBool;
        break;
    }
}

/*
 *  adfSetEnv
 *
 */
void adfSetEnvFct( void(*eFct)(char*), void(*wFct)(char*), void(*vFct)(char*),
    void(*notFct)(SECTNUM,int)  )
{
    if (*eFct!=0)
		adfEnv.eFct = *eFct;
    if (*wFct!=0)
		adfEnv.wFct = *wFct;
    if (*vFct!=0)
		adfEnv.vFct = *vFct;
    if (*notFct!=0)
        adfEnv.notifyFct = *notFct;
}


/*
 * adfGetVersionNumber
 *
 */
char* adfGetVersionNumber()
{
	return(ADFLIB_VERSION);
}


/*
 * adfGetVersionDate
 *
 */
char* adfGetVersionDate()
{
	return(ADFLIB_DATE);
}




/*##################################################################################*/
