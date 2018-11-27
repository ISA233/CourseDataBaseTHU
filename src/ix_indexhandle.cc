#include "pf.h"
#include "ix.h"
#include "ix_internal.h"
#include <iostream>
#include <cmath>
#include <cstring>

IX_IndexHandle::IX_IndexHandle(){
	this->bFileOpen = false;
	this->fileHandle = NULL;
	this->bHeaderDirty = false;
	this->Data = NULL;
	this->indexData = NULL;
}

IX_IndexHandle::~IX_IndexHandle(){
}

int IX_IndexHandle::Compare(char *str) {
	std::memcpy(indexData, str, this->fileHeader.attrLength);
	RID *rid = (RID *)(str + this->fileHeader.attrLength);
	int ret;
	if (this->fileHeader.attrType == STRING) {
		if ((ret = strcmp(Data, indexData))){
			return ret;
		}
	} else if (this->fileHeader.attrType == FLOAT) {
		double t = *(double *)this->Data - *(double *)indexData;
		if (t < 0 && std::fabs(t) > 1e-9) return -1;
		if (t > 0 && std::fabs(t) > 1e-9) return 1;
	} else {
		int A = *(int *)this->Data;
		int B = *(int *)indexData;
		if (A < B) return -1;
		if (A > B) return 1;
	}
	if (DataRid.GetPage() == rid->GetPage()){
		if (DataRid.GetSlot() <  rid->GetSlot()) return -1;
		if (DataRid.GetSlot() == rid->GetSlot()) return 0;
		return 1;
	} else {
		if (DataRid.GetPage() < rid->GetPage()) return -1;
		return 1;
	}
}

RC IX_IndexHandle::GetEntry(const char *pData, const int pos, 
	char *&data, RID *&rid, PageNum *&pageNum) const{
	data = (char *)pData + pos * this->fileHeader.entrySize;
	rid = (RID *)(pData + pos * this->fileHeader.entrySize + 
		this->fileHeader.attrLength);
	pageNum = (PageNum *)(pData + pos * this->fileHeader.entrySize + 
		this->fileHeader.attrLength + sizeof(RID));
	return 0;
}

RC IX_IndexHandle::SetEntry(char *pData, const int pos, 
	const char *data, const RID &rid, PageNum pageNum){
	std::memcpy(pData + pos * this->fileHeader.entrySize,
		data, (size_t)this->fileHeader.attrLength);
	*(RID *)(pData + pos * this->fileHeader.entrySize + 
		this->fileHeader.attrLength) = rid;
	*(PageNum *)(pData + pos * this->fileHeader.entrySize + 
		this->fileHeader.attrLength + sizeof(RID)) = pageNum;
	return 0;
}

RC IX_IndexHandle::SplitAndInsert(PF_PageHandle &pageHandle, IX_PageHeader *pageHeader,
	char *pData, int pos, bool isLeaf) {
	if (pageHeader->numIndex == this->fileHeader.entryNumPerPage){
		PF_PageHandle newPageHandle;
		TRY(this->fileHandle->AllocatePage(newPageHandle));
		PageNum newPageNum;
		char *newPageData;
		TRY(newPageHandle.GetPageNum(newPageNum));
		TRY(this->fileHandle->MarkDirty(newPageNum));
		TRY(newPageHandle.GetData(newPageData));
		IX_PageHeader *newPageHeader = (IX_PageHeader *)newPageData;
		newPageData = newPageData + sizeof(IX_PageHeader);
		if (isLeaf){
			newPageHeader->nextPage = pageHeader->nextPage;
			pageHeader->nextPage = newPageNum;
		} else {
			newPageHeader->nextPage = IX_PAGE_NOT_LEAF;
			pageHeader->nextPage = IX_PAGE_NOT_LEAF;
		}
		int mid = pageHeader->numIndex / 2;
		newPageHeader->numIndex = pageHeader->numIndex - mid;
		memmove(newPageData, pData + mid * this->fileHeader.entrySize, pageHeader->numIndex - mid);
		pageHeader->numIndex = mid;
		if (pos < mid) {
			memmove(pData + (pos + 1) * this->fileHeader.entrySize,
				pData + pos * this->fileHeader.entrySize,
				(size_t)(pageHeader->numIndex - pos) * this->fileHeader.entrySize);
			TRY(SetEntry(pData, pos, this->Data, this->DataRid, this->newSonPageNum));
			pageHeader->numIndex ++;
		} else {
			pos -= mid;
			memmove(newPageData + (pos + 1) * this->fileHeader.entrySize, 
				newPageData + pos * this->fileHeader.entrySize,
				(size_t)(newPageHeader->numIndex - pos) * this->fileHeader.entrySize);
			TRY(SetEntry(newPageData, pos, this->Data, this->DataRid, this->newSonPageNum));
			newPageHeader->numIndex ++;
			char *dest = newPageData + (newPageHeader->numIndex - 1) * this->fileHeader.entrySize;
			memcpy(this->Data, dest, this->fileHeader.attrLength);
			this->DataRid = *(RID *)(dest + this->fileHeader.attrLength);
		}
		this->bSonSplited = true;
		this->newSonPageNum = newPageNum;
		TRY(this->fileHandle->UnpinPage(newPageNum));
	} else {
		memmove(pData + (pos + 1) * this->fileHeader.entrySize, 
			pData + pos * this->fileHeader.entrySize,
			(size_t)(pageHeader->numIndex - pos) * this->fileHeader.entrySize);
		//std::cerr << "pos = " << pos << " num = " << *(int *)(this->Data) << "\n";
		//std::cerr << "RID = (" << this->DataRid.GetPage() << "," << this->DataRid.GetSlot() <<
		//	")\n";
		TRY(SetEntry(pData, pos, this->Data, this->DataRid, this->newSonPageNum));
		pageHeader->numIndex ++;
		this->bSonSplited = false;
		this->newSonPageNum = 0;
	}
	return 0;
}

RC IX_IndexHandle::Insert(PageNum pageNum) {
	PF_PageHandle pageHandle;
	//std::cerr << "pagenum = " << pageNum << "\n";
	IXTRY(this->fileHandle->GetThisPage(pageNum, pageHandle), pageNum);
	IXTRY(this->fileHandle->MarkDirty(pageNum), pageNum);
	//std::cerr << "pagenum = " << pageNum << "\n";
	char *pData;
	IXTRY(pageHandle.GetData(pData), pageNum);
	IX_PageHeader *pageHeader = (IX_PageHeader *)pData;

	//std::cerr << "pData = " << (void *)pData << "\n";
	pData = pData + sizeof(IX_PageHeader);
	//std::cerr << "pData = " << (void *)pData << "\n";
	int pos = pageHeader->numIndex;
	//std::cerr << "nextPage = " << pageHeader->nextPage << "\n";
	//std::cerr << "numIndex = " << pageHeader->numIndex << "\n";
	char *p = pData;
	for (int i = 0; i < pageHeader->numIndex; i ++, p = p + this->fileHeader.entrySize) {
		int res = Compare(p);
		if (res == -1) {
			pos = i;
			break;
		}
		if (res == 0) {
			this->fileHandle->UnpinPage(pageNum);
			return IX_WAR_DUPLICATEDIX;
		}
	}

	// NOT LEAF
	if (pageHeader->nextPage == IX_PAGE_NOT_LEAF){
		// modify max value
		if (pos == pageHeader->numIndex) {
			p = pData + (pageHeader->numIndex - 1) * this->fileHeader.entrySize;
			memcpy(p, Data, this->fileHeader.attrLength);
			memcpy(p + this->fileHeader.attrLength, &DataRid, sizeof(RID));
			pos --;
		} 
		PageNum sonPageNum = *(PageNum *)(pData + pos * this->fileHeader.entrySize
			+ this->fileHeader.attrLength + sizeof(RID));
		int rc = Insert(sonPageNum);
		if (rc == IX_WAR_DUPLICATEDIX || rc < 0) {
			this->fileHandle->UnpinPage(pageNum);
			return rc;
		}
		pos = pageHeader->numIndex;
		p = pData;
		for (int i = 0; i < pageHeader->numIndex; i ++, p = p + this->fileHeader.entrySize) {
			int res = Compare(p);
			if (res == -1) {
				pos = i;
				break;
			}
		}
		IXTRY(SplitAndInsert(pageHandle, pageHeader, pData, pos, false), pageNum);
	} else {
		// LEAF
		IXTRY(SplitAndInsert(pageHandle, pageHeader, pData, pos, true), pageNum);
	}
	this->fileHandle->UnpinPage(pageNum);
	return 0;
}

RC IX_IndexHandle::InsertEntry(void *pData, const RID &rid){
	TRY(IsValid());
	if (pData == NULL) {
		return IX_ERR_NULLENTRY;
	}
	memcpy(this->Data, pData, this->fileHeader.attrLength);
	this->DataRid = rid;

	PF_PageHandle pageHandle;
	this->bSonSplited = false;
	this->bMaxModified = false;
	this->newSonPageNum = 0;
	int rc = Insert(this->fileHeader.rootPage);
	if (rc == IX_WAR_DUPLICATEDIX || rc < 0) {
		return rc;
	}
	if (bSonSplited){
		PF_PageHandle newPageHandle;
		this->fileHandle->AllocatePage(newPageHandle);
		PageNum newPageNum;
		char *newPageData;
		TRY(newPageHandle.GetPageNum(newPageNum));
		TRY(newPageHandle.GetData(newPageData));
		TRY(this->fileHandle->MarkDirty(newPageNum));
		IX_PageHeader *newPageHeader = (IX_PageHeader *)newPageData;
		newPageHeader->nextPage = IX_PAGE_NOT_LEAF;
		newPageHeader->numIndex = 2;
		newPageData = newPageData + sizeof(IX_PageHeader);
		TRY(SetEntry(newPageData, 0, Data, DataRid, this->fileHeader.rootPage));
		TRY(this->fileHandle->GetThisPage(newSonPageNum, pageHandle));
		char *pageData;
		TRY(pageHandle.GetData(pageData));
		IX_PageHeader *pageHeader = (IX_PageHeader *)pageData;
		pageData = pageData + sizeof(IX_PageHeader);
		char *dest = pageData + (pageHeader->numIndex - 1) * this->fileHeader.entrySize;
		RID rid = *(RID *)(dest + this->fileHeader.attrLength);
		TRY(SetEntry(newPageData, 1, dest, rid, newSonPageNum));
		this->bHeaderDirty = true;
		this->fileHeader.rootPage = newPageNum;
		TRY(this->fileHandle->UnpinPage(newPageNum));
	}
	return 0;
}

RC IX_IndexHandle::Remove(PF_PageHandle pageHandle) {
	char *pData;
	PageNum pageNum;

	TRY(pageHandle.GetPageNum(pageNum));
	TRY(pageHandle.GetData(pData));
	IX_PageHeader *pageHeader = (IX_PageHeader *)pData;

	pData = pData + sizeof(IX_PageHeader);
	int pos = pageHeader->numIndex - 1;
	char *p = pData;
	bool ok = false;
	for (int i = 0; i < pageHeader->numIndex; i ++, p = p + this->fileHeader.entrySize) {
		int res = Compare(p);
		if (res <= 0) pos = i;
		if (res == 0) ok = true;
		if (res == 1) break;
	}

	if (pageHeader->nextPage == IX_PAGE_NOT_LEAF){
		// NOT LEAF
		PageNum *sonPageNum = (PageNum *)(pData + pos * this->fileHeader.entrySize
			+ this->fileHeader.attrLength + sizeof(RID));
		PF_PageHandle sonPageHandle;
		this->fileHandle->GetThisPage(*sonPageNum, sonPageHandle);
		int rc = Remove(sonPageHandle);
		if (rc == IX_WAR_NOSUCHINDEX || rc < 0) {
			return rc;
		}
		// son has been deleted
		if (bSonSplited) {
			memmove(pData + pos * this->fileHeader.entrySize, 
				pData + (pos + 1) * this->fileHeader.entrySize,
				(size_t)(pageHeader->numIndex - pos - 1) * this->fileHeader.entrySize);
			pageHeader->numIndex --;
			this->bSonSplited = (pageHeader->numIndex == 0);
			if (!this->bSonSplited && pos == pageHeader->numIndex) {
				this->bMaxModified = true;
				char *dest = pData + (pageHeader->numIndex - 1) * this->fileHeader.entrySize;
				memcpy(this->Data, dest, this->fileHeader.attrLength);
				this->DataRid = *(RID *)(dest + this->fileHeader.attrLength);
				this->newSonPageNum = pageNum;
			} else {
				this->bMaxModified = false;
			}
		} else {
			if (this->bMaxModified) {
				memcpy(pData + pos * this->fileHeader.entrySize,
					this->Data, this->fileHeader.attrLength);
				memcpy(pData + pos * this->fileHeader.entrySize + this->fileHeader.attrLength,
					&this->DataRid, sizeof(DataRid));
				this->bMaxModified = (pos == pageHeader->numIndex - 1);
			}
		}
	} else {
		// LEAF
		if (!ok) {
			return IX_WAR_NOSUCHINDEX;
		}
		memmove(pData + pos * this->fileHeader.entrySize, 
			pData + (pos + 1) * this->fileHeader.entrySize,
			(size_t)(pageHeader->numIndex - pos - 1) * this->fileHeader.entrySize);
		pageHeader->numIndex --;
		this->bSonSplited = (pageHeader->numIndex == 0);
		if (!this->bSonSplited && pos == pageHeader->numIndex) {
			this->bMaxModified = true;
			char *dest = pData + (pageHeader->numIndex - 1) * this->fileHeader.entrySize;
			memcpy(this->Data, dest, this->fileHeader.attrLength);
			this->DataRid = *(RID *)(dest + this->fileHeader.attrLength);
			this->newSonPageNum = pageNum;
		} else {
			this->bMaxModified = false;
		}
	}
	return 0;
}

RC IX_IndexHandle::DeleteEntry(void *pData, const RID &rid){
	TRY(IsValid());
	if (pData == NULL) {
		return IX_ERR_NULLENTRY;
	}
	if (this->Data == NULL) {
		this->Data = (char *)malloc(fileHeader.attrLength);
	}
	std::strcpy(this->Data, (char *)pData);
	this->DataRid = rid;

	PF_PageHandle pageHandle;

	TRY(this->fileHandle->GetThisPage(this->fileHeader.rootPage, pageHandle));
	TRY(Remove(pageHandle));
	return 0;
}

RC IX_IndexHandle::ForcePages(){
    return this->fileHandle->ForcePages();
}  

RC IX_IndexHandle::IsValid() const {
	if (this->fileHandle == NULL || !this->bFileOpen) {
		return IX_ERR_FILENOTOPEN;
	}
	return 0;
}