/**************************************************************
  UMOD.WCX plugin for Total Commander by balver <balver@pf.pl>
***************************************************************

  For credits or some info, see readme-en.txt. I'm from Poland,
  so Polish documentation is also available.

  You can download the newest version at:
  http://square.piwko.pl/index.php?adres=download

  This plugin is licensed under GPL.

**************************************************************/

#include "stdafx.h"            // a file present in every Visual C++ project
                               // we can for example include here some standard libraries
#include "umodcrc.h"           // without this plugin wouldn't be able to count UMOD's CRC
                               // thanks to Luigi Auriemma for publishing it

#define BUFFSZ      32768      // buffer length
#define NAMESZ      260        // file name/path length
#define UMODSIGN    0x9fe3c5a3 // UMOD sign if UMOD archive's identification number

// a little structure, where we store an important info about UMOD's INDEX section :D
struct end {
	unsigned long sign;
	unsigned long indexstart;
	unsigned long indexend;
	unsigned long one;
	unsigned long crc;
} end;

// here we store settings of this plugin
struct settings {
	int CRC;          // 1: count archive's CRC (0: don't do it)
	                  //    default: 0
	int ChangeName;   // 0: don't change manifest.* filenames
                      // 1: change manifest.* -> !UMOD.*
                      // 2: change manifest.* -> !<product>.* (not implemented yet)
	                  //    default: 1
	int Modify;       // 1: add "!make UMOD.bat" file into archive (and modify manifest.ini file in future)
	                  //    default: 1
	char ProductName[NAMESZ]; // cannot be definied in umod.cfg
} settings;

// an simple structure that stores info about one file from archive
typedef struct t_FileList {
	char name[NAMESZ];
	char pathname[NAMESZ];
	long offset;
	long size;
	long flag;
	t_FileList *next;
	t_FileList *prev;
} t_FileList;

// a very important structure, storing all needed info about UMOD archive, we've already opened
typedef struct t_ArchiveInfo {
	char name[NAMESZ];      // UMOD archive name
	FILE *hArchFile;        // HANDLE to file ("active" all the time, when plugin works)
	SYSTEMTIME stLastWrite; // last modification date

	t_FileList *filelist;   // non-cyclic two-way list data structure

	long totalfiles;        // number of files in archive
	int lastfile;           // boolean value (0/1), and a question is: Is this file the last file in archive? :D
	unsigned long crc;      // here we store UMOD's CRC
} t_ArchiveInfo;

// to simplify some declarations I made here new type of data :D
typedef t_ArchiveInfo *myHANDLE;

// function declarations - for info or implementation look at bottom of this page
long fread_index(FILE *fd);
int CreateFileList(myHANDLE hArcData);
void ReadSettings(void);

// plugin path "things"
char szPlugPath[MAX_PATH],
     szConfPath[MAX_PATH],
	 szLogPath[MAX_PATH],
     szDrive[_MAX_DRIVE],
     szPath[MAX_PATH],
     szName[_MAX_FNAME],
     szExt[_MAX_EXT];

// here plugin starts - it is good place to define, where plugin is and from where we will read settings
BOOL APIENTRY DllMain(HANDLE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
	if(ul_reason_for_call==DLL_PROCESS_ATTACH) {
		GetModuleFileName((HINSTANCE)hModule,szPlugPath,MAX_PATH); // path to umod.wcx (with filename)
//		GetModuleFileName(NULL,szFullPath,MAX_PATH);               // path to TC (without file name)
		_splitpath(szPlugPath,szDrive,szPath,szName,szExt); strcpy(szName,"umod");
		strcpy(szExt,"cfg"); _makepath(szConfPath,szDrive,szPath,szName,szExt);
		strcpy(szExt,"log"); _makepath(szLogPath,szDrive,szPath,szName,szExt);
	}
	return TRUE;
}

//#################################[ DLL exports ]#####################################

// OpenArchive should perform all necessary operations when an archive is to be opened
myHANDLE __stdcall OpenArchive(tOpenArchiveData *ArchiveData) {
	t_ArchiveInfo * hArcData;
	settings.CRC=0;
	settings.ChangeName=1;
	settings.Modify=1;
	strcpy(settings.ProductName,"UMOD");

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
//	temporary solution for reading UMOD's last modification date/time
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

//	try to open
	hArcData->hArchFile = fopen(ArchiveData->ArcName,"rb");
	if(!hArcData->hArchFile) {
		ArchiveData->OpenResult = E_EOPEN; // error when opening
		// remember to free memory
		if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
		return 0;
	}

	// UMOD's CRC calculation
	if(settings.CRC == 1) hArcData->crc=umodcrc(hArcData->hArchFile);


	if(fseek(hArcData->hArchFile, -sizeof(end),SEEK_END)<0)  {
		ArchiveData->OpenResult = E_BAD_DATA;
		if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
		return 0;
	}

	// we're going to read UMOD's INDEX section info
	if(fread(&end, sizeof(end),1,hArcData->hArchFile)!=1) {
		ArchiveData->OpenResult = E_EREAD;
		if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
		return 0;
	}

	// UMOD's CRC calculation again
	if(settings.CRC == 1) {
		if(hArcData->crc!=end.crc) { // bad archive
			ArchiveData->OpenResult = E_BAD_ARCHIVE;
			if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
			return 0;
		}
	}

	if(end.sign!=UMODSIGN) { // UMOD identification sign missed
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		if(hArcData->hArchFile!=NULL) fclose(hArcData->hArchFile);
		return 0;
	}

	// security
	hArcData->lastfile = 0;
	hArcData->filelist = NULL;

	// no comment :)
	ArchiveData->OpenResult = CreateFileList(hArcData);
	// if CreateFileList changed OpenResult (error code) return 0 (it is bad :D)
	if (ArchiveData->OpenResult!=0) return 0;
	// set hArcData->filelist to pointer to first file record
	while (hArcData->filelist->prev != NULL) hArcData->filelist = hArcData->filelist->prev;

	return hArcData;
}

// Total Commander calls ReadHeader to find out what files are in the archive 
int __stdcall ReadHeader(myHANDLE hArcData, tHeaderData *HeaderData)
{

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
	
	if (hArcData->filelist->next == NULL) { // checking, if the last file in archive were just send
		hArcData->lastfile=1;
		return 0;
	}
	return 0;
}

// ProcessFile should unpack the specified file or test the integrity of the archive
int __stdcall ProcessFile(myHANDLE hArcData, int Operation, char *DestPath, char *DestName)
{
	FILE  *outputfile;
	long  len, size;
	void  *buff;
	size = hArcData->filelist->size;

	if (Operation == PK_EXTRACT) {
		// important part of plugin begins here
		CreateDirectory(DestPath,NULL);

		// "System\!make UMOD.bat" file creation
		if(strcmp(hArcData->filelist->name,"System\\!make UMOD.bat")==0) {
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

		// standard UMOD files process here
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

		// let's do it... :D
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

		// uninitialize stuff... :P
		fclose(outputfile);
		free (buff);
	}
	if (hArcData->lastfile!=1) hArcData->filelist=hArcData->filelist->next;
	return 0;
}

// CloseArchive should perform all necessary operations when an archive is about to be closed
int __stdcall CloseArchive(myHANDLE hArcData)
{
	fclose (hArcData->hArchFile);
	return 0;
}

// This function allows you to notify user about changing a volume when packing files
// NOT USED IN MY PLUGIN BUT NEEDED AS EXPORT FOR TC
void __stdcall SetChangeVolProc(myHANDLE hArcData, tChangeVolProc pChangeVolProc) {}

// This function allows you to notify user about the progress when you un/pack files
// NOT USED IN MY PLUGIN BUT NEEDED AS EXPORT FOR TC
void __stdcall SetProcessDataProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc) {}

// see wcxhead.h for info
int __stdcall GetPackerCaps() {
	return PK_CAPS_HIDE;
}

//###############################[ implementations ]####################################

int CreateFileList(myHANDLE hArcData) {

	t_FileList *newfile, *previous;
	int counter;

	// store INDEX section of UMOD (it contains all info about files in archive)
	if(fseek(hArcData->hArchFile,end.indexstart,SEEK_SET)<0) { // PL: uszkodzony INDEX / ENG: bad INDEX
		fclose(hArcData->hArchFile);
		return E_BAD_ARCHIVE;
	}

	// store total number of files in UMOD
	hArcData->totalfiles=fread_index(hArcData->hArchFile);
	// if we want to add some files into UMOD, here is good place to inform about it
	if((settings.ChangeName==1)&&(settings.Modify==1)) hArcData->totalfiles++;

	for (counter=0; hArcData->totalfiles > counter; counter++) {
		// initialize new record
		if((newfile = new t_FileList)==NULL) {
			fclose(hArcData->hArchFile);
			return E_NO_MEMORY;
		}
		// security
		newfile->prev = NULL; newfile->next=NULL;

		// the beginning of list is good place to inform TC about files not existing in UMOD
		// for now plugin adds "System\!make UMOD.bat" file (depending on two settings - see below)
		if (counter==0) {
			hArcData->filelist=newfile;
			if((settings.ChangeName==1)&&(settings.Modify==1)) {
			  strcpy(newfile->name,"System\\!make UMOD.bat");
			  newfile->size=158;
			  previous = newfile;
			  continue;
			}
		}

		// store file name, long fread_index(FILE*) returns file name length
		if(fread(newfile->name,fread_index(hArcData->hArchFile), 1, hArcData->hArchFile)!=1) {
			fclose(hArcData->hArchFile);
			return E_EREAD;
		}

		// code IMHO is easy to understand (if not: it changes names in archive - it doesn't
		// modify UMOD, but it modify what we will see when we open archive in TC)
		if(settings.ChangeName==1) { // needed an modification below for settings.ProductName
		  if(strcmp(newfile->name,"System\\Manifest.ini")==0) strcpy(newfile->name,"System\\!UMOD.ini");
		  if(strcmp(newfile->name,"System\\Manifest.int")==0) strcpy(newfile->name,"System\\!UMOD.int");
		}

		// store file offset in UMOD archive
		if(fread(&(newfile->offset),4,1,hArcData->hArchFile)!=1) {
			fclose(hArcData->hArchFile);
			return E_EREAD;
		}
		// store file size
		if(fread(&(newfile->size),4,1,hArcData->hArchFile)!=1) {
			fclose(hArcData->hArchFile);
			return E_EREAD;
		}
		// store file flag from UMOD - not used in plugin
		if(fread(&(newfile->flag),4,1,hArcData->hArchFile)!=1) {
			fclose(hArcData->hArchFile);
			return E_EREAD;
		}
		// set pointers to next and previous recors
		if (counter>0) { previous->next = newfile; newfile->prev = previous; }
		// remember current "fileinfo record" as previous
		previous = newfile;
	}

	return 0;
}

// a simple function that reads INDEX type (made by Luigi Auriemma)
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

// this function should open umod.cfg (INI-format) file in plugin directory and read settings from it.
// unfortunately it cannot do it
void ReadSettings(void) {
// ==============================================================================================
/* 1st attempt:
	FILE*settings;
	settings=fopen(szConfPath,"rb");
	if(!settings) {
		MessageBox(NULL,"nie wczytany","B³¹d",MB_OK);
		return;
	}
	if(fscanf(settings,"CRC=")!=0) MessageBox(NULL,"CRC=","B³¹d",MB_OK);
	if(fscanf(settings,"CRC2=")!=0) MessageBox(NULL,"CRC2=","B³¹d",MB_OK);
	fclose(settings);
// =========================================================================================== */
/* 2nd attempt:
	char buff[10];
	settings.CRC = CbGetIniKeyString((char*)&szConfPath,"Settings","CRC",buff,10);
	settings.ChangeName = CbGetIniKeyString((char*)&szConfPath,"Settings","ChangeName",buff,10);
	settings.Modify = CbGetIniKeyString((char*)&szConfPath,"Settings","Modify",buff,10);

// =========================================================================================== */
/* 3rd attempt:
	HANDLE SettingsFile;
	char buff[10];
	SettingsFile = CreateFile((char*)&szConfPath,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL);

	CloseHandle(SettingsFile);
// =========================================================================================== */
}