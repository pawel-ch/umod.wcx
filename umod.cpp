// umod.cpp : Defines the entry point for the DLL application.
//

#include "stdafx.h" // niestety VC++ wymaga

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> 
#include "umodcrc.h" // b³êdy

#include "wcxhead.h"

#include <direct.h>
#include <malloc.h>

#define BUFFSZ      32768
#define NAMESZ      256
#define UMODSIGN    0x9fe3c5a3

//tArchiveInfo * readinfo(void);

struct end {
	unsigned long sign;
	unsigned long indexstart;
	unsigned long indexend;
	unsigned long one;
	unsigned long crc;
} end;

typedef struct t_FileList {
	char name[260];
	char pathname[260];
	long offset;
	long size;
	long flag;
	t_FileList *next;
	t_FileList *prev;
} t_FileList;

typedef struct t_ArchiveInfo {
	char name[260];       // nazwa pliku odczytana póŸniej z ArchiveData->ArcName
	FILE *hArchFile;      // uchwyt do pliku ("aktywny" przez ca³y czas)

	t_FileList *filelist; // lista dwukierunkowa niecykliczna strukturek t_FileList

	long totalfiles;      // liczba plików w archiwum odczytana z pocz¹tku sekcji INDEXSTART
	int counter;          // licznik... hmm... odczytanych plików?
	int lastfile;         // sposób na wyœwietlanie ostatniego pliku w archiwum

	unsigned long crc;
} t_ArchiveInfo;

typedef t_ArchiveInfo *myHANDLE;

long fread_index(FILE *fd) {
    long    result = 0;
    unsigned char  b0,
            b1,
            b2,
            b3,
            b4;

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

int CreateFileList(myHANDLE archinfo) {
	// na razie okreœlamy: flag, name, next, offset, prev, size
	// nie okreœlamy: pathname
	t_FileList *nowyplik;
	t_FileList *poprzedni;
	int counter;
	if(fseek(archinfo->hArchFile,end.indexstart,SEEK_SET)<0) { // uszkodzony INDEX
		fclose(archinfo->hArchFile);
		return E_BAD_ARCHIVE;
	}
	archinfo->totalfiles=fread_index(archinfo->hArchFile);
	for (counter=0; archinfo->totalfiles > counter; counter++) {
		if((nowyplik = new t_FileList)==NULL) {
			fclose(archinfo->hArchFile);
			return E_NO_MEMORY;
		}
		nowyplik->prev = NULL; // dla bezpieczeñstwa
		nowyplik->next=NULL;
		if (counter==0) archinfo->filelist=nowyplik;
		if(fread(nowyplik->name,fread_index(archinfo->hArchFile), 1, archinfo->hArchFile)!=1) {
			fclose(archinfo->hArchFile);
			return E_EREAD;
		}
		if(strcmp(nowyplik->name,"System\\Manifest.ini")==0) strcpy(nowyplik->name,"System\\!UMOD.ini");
		if(strcmp(nowyplik->name,"System\\Manifest.int")==0) strcpy(nowyplik->name,"System\\!UMOD.int");
//	  	MessageBox(NULL,(const char *)&(nowyplik->name),(const char *)&(nowyplik->name),MB_OK);
		if(fread(&(nowyplik->offset),4,1,archinfo->hArchFile)!=1) {
			fclose(archinfo->hArchFile);
			return E_EREAD;
		}
		if(fread(&(nowyplik->size),4,1,archinfo->hArchFile)!=1) {
			fclose(archinfo->hArchFile);
			return E_EREAD;
		}
		if(fread(&(nowyplik->flag),4,1,archinfo->hArchFile)!=1) {
			fclose(archinfo->hArchFile);
			return E_EREAD;
		}
		if (counter>0) { poprzedni->next = nowyplik; nowyplik->prev = poprzedni; }
		poprzedni = nowyplik;
	}

	return 0;
	
}

  //-----------------------=[ DLL exports ]=--------------------

// OpenArchive should perform all necessary operations when an archive is to be opened
myHANDLE __stdcall OpenArchive(tOpenArchiveData *ArchiveData) {
	t_ArchiveInfo * archinfo;

	ArchiveData->CmtBuf=0;
	ArchiveData->CmtBufSize=0;
	ArchiveData->CmtSize=0;
	ArchiveData->CmtState=0;

	ArchiveData->OpenResult=E_NO_MEMORY;//domyœlny b³¹d
	if((archinfo = new t_ArchiveInfo)==NULL) {
		return 0;
	}
	
// próbujemy otworzyæ
	memset(archinfo,0,sizeof(t_ArchiveInfo));
	strcpy(archinfo->name,ArchiveData->ArcName);

	archinfo->hArchFile = fopen(ArchiveData->ArcName,"rb");
	if(!archinfo->hArchFile) {
		//goto error
		ArchiveData->OpenResult = E_EOPEN; // b³¹d otwarcia
		//musimy zwolniæ pamiêæ
		if(archinfo->hArchFile!=NULL) fclose(archinfo->hArchFile);
		return 0;
	}

	archinfo->counter=0;
	archinfo->crc=umodcrc(archinfo->hArchFile);

	if(fseek(archinfo->hArchFile, -sizeof(end),SEEK_END)<0)  {
		ArchiveData->OpenResult = E_BAD_DATA;
		if(archinfo->hArchFile!=NULL) fclose(archinfo->hArchFile);
		return 0;
	}

	if(fread(&end, sizeof(end),1,archinfo->hArchFile)!=1) {
		ArchiveData->OpenResult = E_EREAD;
		if(archinfo->hArchFile!=NULL) fclose(archinfo->hArchFile);
		return 0;
	}
//	MessageBox(NULL,(const char *)&(ArchiveData->OpenMode),(const char *)&(archinfo->crc),MB_OK);
	if(archinfo->crc!=end.crc) { // uszkodzone archiwum
		ArchiveData->OpenResult = E_BAD_ARCHIVE;//E_BAD_ARCHIVE
		if(archinfo->hArchFile!=NULL) fclose(archinfo->hArchFile);
		return 0;
	}

	if(end.sign!=UMODSIGN) { // brak identyfikatora archiwum UMOD
		ArchiveData->OpenResult = E_UNKNOWN_FORMAT;
		if(archinfo->hArchFile!=NULL) fclose(archinfo->hArchFile);
		return 0;
	}

	archinfo->lastfile = 0; // sposób na wyœwietlenie ostatniego pliku w archiwum

	archinfo->filelist = NULL;
	ArchiveData->OpenResult = CreateFileList(archinfo);
	if (ArchiveData->OpenResult!=0) {return 0;}
	while (archinfo->filelist->prev != NULL) archinfo->filelist = archinfo->filelist->prev;

// -> tu skoñczyliœmy
//	ArchiveData->OpenResult = 0;
	return archinfo;
}

// Total Commander wywo³uje ReadHeader aby dowiedzieæ siê, jakie pliki znajduj¹ siê w archiwum
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
	HeaderData->FileTime=0;
	HeaderData->Flags=0;
	HeaderData->HostOS=0;
	HeaderData->Method=0;
	HeaderData->PackSize=hArcData->filelist->size;
	HeaderData->UnpSize=hArcData->filelist->size;
	HeaderData->UnpVer=0;
	
	if (hArcData->filelist->next == NULL) { // sprawdzanie, czy wys³ano przed chwil¹ ostatni plik
		hArcData->lastfile=1;
		return 0;
	}
	return 0;
}

// ProcessFile should unpack the specified file or test the integrity of the archive
int __stdcall ProcessFile(myHANDLE hArcData, int Operation, char *DestPath, char *DestName)
{
	FILE  *wyjscie;
	long  len, size;
	void  *buff;
	size = hArcData->filelist->size;

	if (Operation == PK_EXTRACT) {
		// tutaj znajduje siê to, co najwa¿niejsze
		// inicjalizacja takich tam
		mkdir(DestPath);
		wyjscie = fopen(DestName,"wb");
		if(!wyjscie) {
			if(wyjscie!=NULL) fclose(wyjscie);
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

		// do rzeczy...
		len = BUFFSZ;

		while(size) {
			if(len>size) len = size;
			if(fread(buff,len,1,hArcData->hArchFile)!=1) {
				if(wyjscie!=NULL) fclose(wyjscie);
				free(buff);
				//return E_EREAD;
				return 0;
			}
			if(fwrite(buff,len,1,wyjscie)!=1) {
				if(wyjscie!=NULL) fclose(wyjscie);
				free(buff);
				//return E_EWRITE;
				return 0;
			}
			size-=len;
		}

		// deinicjalizacja takich tam
		fclose(wyjscie);
		free (buff);
		// dalej s¹ tylko g³upoty
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
void __stdcall SetChangeVolProc(myHANDLE hArcData, tChangeVolProc pChangeVolProc) {}

// This function allows you to notify user about the progress when you un/pack files
void __stdcall SetProcessDataProc(myHANDLE hArcData, tProcessDataProc pProcessDataProc) {}
//---------------------------------------------------------------------------
