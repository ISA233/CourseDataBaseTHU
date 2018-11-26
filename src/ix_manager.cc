#include "pf.h"
#include "ix.h"
#include "ix_internal.h"
#include <iostream>

IX_Manager::IX_Manager (PF_Manager &pfm) {
	this->pfm = &pfm;
}

IX_Manager::~IX_Manager () {

} 

RC IX_Manager::CreateIndex (const char *fileName, int indexNo, 
					AttrType   attrType, int attrLength) {
	char FileName[20];
	memset(FileName, 0, sizeof(FileName));
	sprintf(FileName, "%s.%d", fileName, indexNo);
	TRY(pfm->CreateFile(FileName));

	PF_FileHandle fileHandle;
	PF_PageHandle pageHandle;

	char *pageData;

	TRY(pfm->OpenFile(FileName, fileHandle));
	TRY(fileHandle.AllocatePage(pageHandle));
	TRY(pageHandle.GetData(pageData));

	auto p = (IX_FileHeader *)pageData;
	p->attrType = attrType;
	p->attrLength = attrLength;
	p->entrySize = attrLength + sizeof(RID) + sizeof(PageNum);

//  num * (p->entrySize) + [(num + 7) / 8] <= (PF_PAGE_SIZE - sizeof(IX_PageHeader) + 1)
//  IX_HEADER_SIZE = sizeof(IX_PageHeader) + [(num + 7) / 8]
//     int num = (PF_PAGE_SIZE - sizeof(IX_PageHeader) + 1) / (p->entrySize + 1.0 / 8.0);
//     while (num * p->entrySize + ((num + 7) / 8) > PF_PAGE_SIZE - (int)sizeof(IX_PageHeader) + 1)
//         num --;
//     p->entryNumPerPage = num;
	p->entryNumPerPage = (PF_PAGE_SIZE - sizeof(IX_PageHeader)) / p->entrySize;

	PageNum rootPageNum;
	PF_PageHandle rootPageHandle;
	TRY(fileHandle.AllocatePage(rootPageHandle));
	TRY(rootPageHandle.GetPageNum(rootPageNum));
	char *rootPageData;
	TRY(rootPageHandle.GetData(rootPageData));
	IX_PageHeader *rootPageHeader = (IX_PageHeader *)rootPageData;
	rootPageHeader->numIndex = 0;
	rootPageHeader->nextPage = IX_PAGE_LIST_END;
	TRY(fileHandle.UnpinPage(rootPageNum));

	p->rootPage = rootPageNum;
	PageNum pageNum;
	TRY(pageHandle.GetPageNum(pageNum));
	TRY(fileHandle.UnpinPage(pageNum));
	TRY(pfm->CloseFile(fileHandle));
	return 0;
}

RC IX_Manager::DestroyIndex (const char *fileName, int indexNo) {
	char FileName[20];
	memset(FileName, 0, sizeof(FileName));
	sprintf(FileName, "%s.%d", fileName, indexNo);
	TRY(pfm->CreateFile(FileName));
	return pfm->DestroyFile(FileName);
}

RC IX_Manager::OpenIndex (const char *fileName, int indexNo, 
				IX_IndexHandle &indexHandle) {
	char FileName[20];
	memset(FileName, 0, sizeof(FileName));
	sprintf(FileName, "%s.%d", fileName, indexNo);
	TRY(pfm->CreateFile(FileName));
	indexHandle.fileHandle = new PF_FileHandle();
    PF_PageHandle pageHandle;
    char *pData;

    TRY(pfm->OpenFile(FileName, *indexHandle.fileHandle));
    TRY(indexHandle.fileHandle->GetFirstPage(pageHandle));
    TRY(pageHandle.GetData(pData));

    indexHandle.bFileOpen = true;
    indexHandle.fileHeader = *(IX_FileHeader *)pData;
    indexHandle.indexData = (char *)malloc(indexHandle.fileHeader.attrLength);

    PageNum pageNum;
    TRY(pageHandle.GetPageNum(pageNum));
    TRY(indexHandle.fileHandle->UnpinPage(pageNum));
	return 0;
}

RC IX_Manager::CloseIndex (IX_IndexHandle &indexHandle) {
	if (indexHandle.bHeaderDirty){
    	PF_PageHandle pageHandle;
    	PageNum pageNum;
		char *pData;
		TRY(indexHandle.fileHandle->GetFirstPage(pageHandle));
		TRY(pageHandle.GetPageNum(pageNum));
		TRY(pageHandle.GetData(pData));
		memcpy(pData, &indexHandle.fileHeader, sizeof(IX_FileHeader));
		TRY(indexHandle.fileHandle->UnpinPage(pageNum));
	}
    pfm->CloseFile(*(indexHandle.fileHandle));
    free(indexHandle.indexData);
    free(indexHandle.Data);
    indexHandle.fileHandle = NULL;
    indexHandle.bFileOpen = false;
	return 0;
}