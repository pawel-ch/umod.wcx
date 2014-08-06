/*******************************************************************************
  UMOD.WCX plugin for Total Commander by Pawel Chojnowski <pawelch@pawelch.info>
********************************************************************************

  Latest stable build:
   * http://pawelch.info/

  Source code:
   * https://github.com/pawel-ch/umod.wcx

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

  http://www.gnu.org/licenses/gpl.txt

*******************************************************************************/

#include "stdafx.h"
#include "umodcrc.h"

#define BUFFSZ      32768
#define UMODSIGN    0x9fe3c5a3

struct end {
    unsigned long sign;
    unsigned long indexstart;
    unsigned long indexend;
    unsigned long one;
    unsigned long crc;
} end;

struct settings {
    int CRC;          // 1: check CRC when opening
                      // 0: skip CRC check
                      //    default: 0
    int ChangeName;   // 0: don't change manifest.* filenames
                      // 1: change manifest.* -> !UMOD.*
                      // 2: change manifest.* -> !<product>.* (not implemented yet)
                      //    default: 1
    int Modify;       // 1: add "! make UMOD.bat" file into archive (and modify manifest.ini file in future)
                      //    default: 1
    int ProductNameLen;
    char ProductName[_MAX_FNAME];
} settings;

typedef struct t_FileList {
    char name[_MAX_FNAME];
    //char pathname[NAMESZ];
    long offset;
    long size;
    long flag;
    t_FileList *next;
    t_FileList *prev;
} t_FileList;

typedef struct t_ArchiveInfo {
    char name[_MAX_FNAME];
    FILE *hArchFile;        // HANDLE to file ("active" all the time, when plugin works)
    SYSTEMTIME stLastWrite;

    t_FileList *filelist;   // non-cyclic two-way list

    long totalfiles;
    int lastfile;           // boolean
    unsigned long crc;
} t_ArchiveInfo;

typedef t_ArchiveInfo *myHANDLE;

long fread_index(FILE *fd);
int CreateFileList(myHANDLE hArcData);
void ReadSettings(void);

char szPlugPath[MAX_PATH],
     szConfPath[MAX_PATH],
     szLogPath[MAX_PATH],
     szDrive[_MAX_DRIVE],
     szPath[MAX_PATH],
     szName[_MAX_FNAME],
     szExt[_MAX_EXT],
     szTempPath[MAX_PATH],
     szTempManifest[MAX_PATH];

LPTSTR Buffer;

BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
  switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
      GetModuleFileName((HINSTANCE)hModule,szPlugPath,MAX_PATH); // path to umod.wcx (with filename)
//    GetModuleFileName(NULL,szFullPath,MAX_PATH);               // path to TC (without file name)
      _splitpath(szPlugPath,szDrive,szPath,szName,szExt); strcpy(szName,"umod");
      strcpy(szExt,"cfg"); _makepath(szConfPath,szDrive,szPath,szName,szExt);
      strcpy(szExt,"log"); _makepath(szLogPath,szDrive,szPath,szName,szExt);
      strcpy(szTempPath,getenv("TEMP")); // temporary path for unpacking manifest.ini file
      strcpy(szTempManifest,szTempPath);
      strcat(szTempManifest,"manifest.ini");
      break;

    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
      break;
  }
  return TRUE;
}

//#################################[ DLL exports ]#####################################

myHANDLE __stdcall OpenArchive(tOpenArchiveData *ArchiveData) {
    t_ArchiveInfo * hArcData;

    ReadSettings();

    ArchiveData->CmtBuf=0;
    ArchiveData->CmtBufSize=0;
    ArchiveData->CmtSize=0;
    ArchiveData->CmtState=0;

    ArchiveData->OpenResult=E_NO_MEMORY; // default error
    if((hArcData = new t_ArchiveInfo)==NULL) {
        return 0;
    }
    
    memset(hArcData,0,sizeof(t_ArchiveInfo));
    strcpy(hArcData->name,ArchiveData->ArcName);

// ===================================================================
//    temporary solution for reading UMOD's last modification date/time
    FILETIME ftLastWrite, ftLocal;
    HANDLE arcForTime;
    arcForTime = CreateFile(ArchiveData->ArcName,
        GENERIC_READ,
        FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    GetFileTime(arcForTime, NULL, NULL, &ftLastWrite);
    FileTimeToLocalFileTime(&ftLastWrite,&ftLocal);
    FileTimeToSystemTime(&ftLocal,&(hArcData->stLastWrite));
    CloseHandle(arcForTime);
// ================================================================ */

    hArcData->hArchFile = fopen(ArchiveData->ArcName,"rb");
    if(!hArcData->hArchFile) {
        ArchiveData->OpenResult = E_EOPEN;
        // remember to free memory
        if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
        return 0;
    }

    // CRC calculation
    if(settings.CRC == 1) hArcData->crc=umodcrc(hArcData->hArchFile);

    if(fseek(hArcData->hArchFile, -sizeof(end),SEEK_END)<0)  {
        ArchiveData->OpenResult = E_BAD_DATA;
        if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
        return 0;
    }

    // read UMOD's INDEX section info
    if(fread(&end, sizeof(end),1,hArcData->hArchFile)!=1) {
        ArchiveData->OpenResult = E_EREAD;
        if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
        return 0;
    }

    // CRC calculation continued
    if(settings.CRC == 1) {
        if(hArcData->crc!=end.crc) { // bad archive
            ArchiveData->OpenResult = E_BAD_ARCHIVE;
            if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
            return 0;
        }
    }

    if(end.sign!=UMODSIGN) { // UMOD identification sign missing
        ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
        if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
        return 0;
    }

    // security
    hArcData->lastfile = 0;
    hArcData->filelist = NULL;

    ArchiveData->OpenResult = CreateFileList(hArcData);
    // if CreateFileList changed OpenResult (error code) return 0 (it is bad :D)
    if (ArchiveData->OpenResult!=0) return 0;
    // set hArcData->filelist to pointer to first file record
    while (hArcData->filelist->prev != NULL) hArcData->filelist = hArcData->filelist->prev;

    // TODO: add product name detection here
    return hArcData;
}

int __stdcall ReadHeader(myHANDLE hArcData, tHeaderData *HeaderData) {

    if(hArcData->lastfile == 1) return E_END_ARCHIVE;

    strcpy(HeaderData->ArcName,hArcData->name);
    HeaderData->CmtBuf=0;
    HeaderData->CmtBufSize=0;
    HeaderData->CmtSize=0;
    HeaderData->CmtState=0;
    HeaderData->FileAttr=0;
    HeaderData->FileCRC=0;
    strcpy(HeaderData->FileName,hArcData->filelist->name);
    HeaderData->FileTime=(hArcData->stLastWrite.wYear - 1980) << 25
                        | hArcData->stLastWrite.wMonth << 21
                        | hArcData->stLastWrite.wDay << 16
                        | hArcData->stLastWrite.wHour << 11
                        | hArcData->stLastWrite.wMinute << 5
                        | hArcData->stLastWrite.wSecond/2;
    HeaderData->Flags=0;
    HeaderData->HostOS=0;
    HeaderData->Method=0;
    HeaderData->PackSize=hArcData->filelist->size;
    HeaderData->UnpSize=hArcData->filelist->size;
    HeaderData->UnpVer=0;
    
    if (hArcData->filelist->next == NULL) { // check if last file
        hArcData->lastfile=1;
        return 0;
    }
    return 0;
}

int __stdcall ProcessFile(myHANDLE hArcData, int Operation, char *DestPath, char *DestName)
{
    FILE  *outputfile;
    long  len, size;
    void  *buff;
    size = hArcData->filelist->size;

    if (Operation == PK_EXTRACT) {
        // TODO: use extract function/remove redundant code
        CreateDirectory(DestPath,NULL);

        // "System\! make UMOD.bat" file creation
        if(strcmp(hArcData->filelist->name,"System\\! make UMOD.bat")==0) {
            outputfile = fopen(DestName,"wt");
            if(!outputfile) {
                if(outputfile!=NULL) fclose(outputfile);
                return E_EWRITE;
            }
            fprintf(outputfile,"@echo off\n"
                            "copy Manifest.ini bla.bak > nul\n"
                            "ucc master \"!%s\"\n"
                            "del Manifest.*\n"
                            "move bla.bak Manifest.ini > nul\n"
                            "del \"!%s.ini\"\n"
                            "del \"!%s.int\"\n"
                            "cls\n"
                            "del %%0", settings.ProductName, settings.ProductName, settings.ProductName);
            fclose(outputfile);
            if (hArcData->lastfile!=1) hArcData->filelist=hArcData->filelist->next;
            return 0;
        }

        // standard UMOD processing
        outputfile = fopen(DestName,"wb");
        if(!outputfile) {
            if(outputfile!=NULL) fclose(outputfile);
            return E_EWRITE;
        }

        buff = malloc(BUFFSZ);
        if(!buff) {
            free(buff);
            return E_NO_MEMORY;
        }
        if(fseek(hArcData->hArchFile,hArcData->filelist->offset,SEEK_SET)<0) {
            fclose(hArcData->hArchFile);
            return E_EREAD;
        }

        len = BUFFSZ;

        while(size) {
            if(len>size) len = size;
            if(fread(buff,len,1,hArcData->hArchFile)!=1) {
                if(outputfile!=NULL) fclose(outputfile);
                free(buff);
                return 0;
            }
            if(fwrite(buff,len,1,outputfile)!=1) {
                if(outputfile!=NULL) fclose(outputfile);
                free(buff);
                return 0;
            }
            size-=len;
        }

        // uninit
        fclose(outputfile);
        free (buff);
    }
    if (hArcData->lastfile!=1) hArcData->filelist=hArcData->filelist->next;
    return 0;
}

// TODO: not an export - move below
int extract(long size, long len, long offset, myHANDLE hArcData) {
    FILE  *outputfile;
    void  *buff;

    CreateDirectory(szTempPath,NULL);

    // standard UMOD processing
    outputfile = fopen(szTempManifest,"wb");
    if(!outputfile) {
        if(outputfile!=NULL) fclose(outputfile);
        return E_EWRITE;
    }

    buff = malloc(BUFFSZ);
    if(!buff) {
        free(buff);
        return E_NO_MEMORY;
    }
    if(fseek(hArcData->hArchFile,offset,SEEK_SET)<0) {
        fclose(hArcData->hArchFile);
        return E_EREAD;
    }

    len = BUFFSZ;

    while(size) {
        if(len>size) len = size;
        if(fread(buff,len,1,hArcData->hArchFile)!=1) {
            if(outputfile!=NULL) fclose(outputfile);
            free(buff);
            return 0;
        }
        if(fwrite(buff,len,1,outputfile)!=1) {
            if(outputfile!=NULL) fclose(outputfile);
            free(buff);
            return 0;
        }
        size-=len;
    }

    // uninit
    fclose(outputfile);
    free(buff);
    return 0;
}

int __stdcall CloseArchive(myHANDLE hArcData)
{
    fclose (hArcData->hArchFile);
    return 0;
}

// not used in this plugin but required as export for TC
void __stdcall SetChangeVolProc(myHANDLE hArcData, tChangeVolProc pChangeVolProc) {}

// not used in this plugin but required as export for TC
void __stdcall SetProcessDataProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc) {}

// see wcxhead.h for details
int __stdcall GetPackerCaps() {
    return PK_CAPS_HIDE;
}

//###############################[ implementations ]####################################

int CreateFileList(myHANDLE hArcData) {

    t_FileList *newfile, *previous;
    int counter;
    long PointerPosition;

    // store INDEX section of UMOD (it contains all info about files in archive)
    if(fseek(hArcData->hArchFile,end.indexstart,SEEK_SET)<0) { // check if bad INDEX
        fclose(hArcData->hArchFile);
        return E_BAD_ARCHIVE;
    }

    // store total number of files in UMOD
    hArcData->totalfiles=fread_index(hArcData->hArchFile);
    // increase for virtual files
    if((settings.ChangeName==1)&&(settings.Modify==1)) hArcData->totalfiles++;

    for (counter=0; hArcData->totalfiles > counter; counter++) {
        if((newfile = new t_FileList)==NULL) {
            fclose(hArcData->hArchFile);
            return E_NO_MEMORY;
        }
        newfile->prev = NULL; newfile->next=NULL;

        // add virtual "System\! make UMOD.bat" file (depending on settings)
        if (counter==0) {
            hArcData->filelist=newfile;
            if((settings.ChangeName==1)&&(settings.Modify==1)) {
              strcpy(newfile->name,"System\\! make UMOD.bat");
              newfile->size=146+3*settings.ProductNameLen;
              previous = newfile;
              continue;
            }
        }

        // file name, long fread_index(FILE*) returns file name length
        if(fread(newfile->name,fread_index(hArcData->hArchFile), 1, hArcData->hArchFile)!=1) {
            fclose(hArcData->hArchFile);
            return E_EREAD;
        }

        // file offset in UMOD
        if(fread(&(newfile->offset),4,1,hArcData->hArchFile)!=1) {
            fclose(hArcData->hArchFile);
            return E_EREAD;
        }
        // file size
        if(fread(&(newfile->size),4,1,hArcData->hArchFile)!=1) {
            fclose(hArcData->hArchFile);
            return E_EREAD;
        }
        // file flag from UMOD - not used at the moment
        if(fread(&(newfile->flag),4,1,hArcData->hArchFile)!=1) {
            fclose(hArcData->hArchFile);
            return E_EREAD;
        }

        // get product name from Manifest.ini
        if((settings.ChangeName==1)&&(strcmp(newfile->name,"System\\Manifest.ini")==0)) { // needed an modification below for settings.ProductName
            PointerPosition = ftell(hArcData->hArchFile);
            extract(newfile->size,newfile->size,newfile->offset,hArcData);
            settings.ProductNameLen = (int)GetPrivateProfileString("Setup","Product","UMOD",settings.ProductName,_MAX_FNAME,szTempManifest);
            DeleteFile(szTempManifest);
            fseek(hArcData->hArchFile,PointerPosition,SEEK_SET);
        }

        // optionally rename Manifest.*
        if(settings.ChangeName==1) {
            if(strcmp(newfile->name,"System\\Manifest.ini")==0) {
                strcpy(newfile->name,"System\\!");
                strcat(newfile->name,settings.ProductName);
                strcat(newfile->name,".ini");
            }

            if(strcmp(newfile->name,"System\\Manifest.int")==0) {
                strcpy(newfile->name,"System\\!");
                strcat(newfile->name,settings.ProductName);
                strcat(newfile->name,".int");
            }
        }

        if (counter>0) { previous->next = newfile; newfile->prev = previous; }
        previous = newfile;
    }

    return 0;
}

// reads INDEX type (by Luigi Auriemma)
long fread_index(FILE *fd) {
    long    result = 0;
    unsigned char  b0, b1, b2, b3, b4;

    b0 = fgetc(fd);
    if(b0 & 0x40) {
        b1 = fgetc(fd);
        if(b1 & 0x80) {
            b2 = fgetc(fd);
            if(b2 & 0x80) {
                b3 = fgetc(fd);
                if(b3 & 0x80) {
                    b4 = fgetc(fd);
                    result = b4;
                }
                result = (result << 7) | (b3 & 0x7f);
            }
            result = (result << 7) | (b2 & 0x7f);
        }
        result = (result << 7) | (b1 & 0x7f);
    }
    result = (result << 6) | (b0 & 0x3f);
    if(b0 & 0x80) result = -result;

    return(result);
}

// read umod.cfg (INI-format) from plugin's directory
void ReadSettings(void) {
    settings.CRC = GetPrivateProfileInt("Settings","CRC",0,(char*)&szConfPath);
    settings.ChangeName = GetPrivateProfileInt("Settings","ChangeName",1,(char*)&szConfPath);
    settings.Modify = GetPrivateProfileInt("Settings","Modify",1,(char*)&szConfPath);
    strcpy(settings.ProductName,"UMOD");
    settings.ProductNameLen=4;
}
