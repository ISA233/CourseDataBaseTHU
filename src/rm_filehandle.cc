//
// Created by mektpoy on 2018/11/05.
//

#include "pf.h"
#include "rm.h"
#include "rm_internal.h"
#include <iostream>
#include <cassert>

RM_FileHandle::RM_FileHandle() {
    pf_FileHandle = NULL;
}

RM_FileHandle::~RM_FileHandle() {
}

RC RM_FileHandle::GetRec (const RID &rid, RM_Record &rec) const {
	TRY(IsValid());
	PageNum pageNum;
	SlotNum slotNum;
	TRY(rid.GetPageNum(pageNum));
	TRY(rid.GetSlotNum(slotNum));
	if (pageNum <= 0) return RM_ERR_PAGENUM;
	if (slotNum < 0 || slotNum >= this->rm_FileHeader.recordNumPerPage) return RM_ERR_SLOTNUM;

	PF_PageHandle pageHandle;
	int rc = this->pf_FileHandle->GetThisPage(pageNum, pageHandle);
	if (rc) {
		if (rc < 0 || rc == PF_EOF) {
			return rc;
		}
		std::cout << "non-zero return code : " << rc << std::endl; 
	}

	char *pData;
	TRY(pageHandle.GetData(pData));

	if (GetBit((unsigned char *)(pData + sizeof(RM_PageHeader) - 1), slotNum) == false) {
		return RM_ERR_NOSUCHREC;
	}
	unsigned int offset = this->rm_FileHeader.pageHeaderSize + this->rm_FileHeader.recordSize * slotNum;
	rec.size = this->rm_FileHeader.recordSize;
	rec.data = (char *) malloc(this->rm_FileHeader.recordSize);
	memcpy (rec.data, pData + offset, this->rm_FileHeader.recordSize);
	rec.rid = rid;
	TRY(this->pf_FileHandle->UnpinPage(pageNum));
    return 0;
}

RC RM_FileHandle::InsertRec (const char *pData, RID &rid) {
	TRY(IsValid());
	if (pData == NULL) {
		return RM_ERR_NULLRECDATA;
	}

	PF_PageHandle pageHandle;
	PageNum pageNum;
	SlotNum slotNum;
	if (this->rm_FileHeader.firstFreePage == RM_PAGE_LIST_END) {
		TRY(this->pf_FileHandle->AllocatePage(pageHandle));
		TRY(pageHandle.GetPageNum(pageNum));
		char *pageData;
		TRY(pageHandle.GetData(pageData));
		RM_PageHeader *rm_PageHeader = (RM_PageHeader *)pageData;
		memset(rm_PageHeader, 0, (size_t)this->rm_FileHeader.pageHeaderSize);
		rm_PageHeader->firstFreeSlot = 0;
		rm_PageHeader->nextPage = RM_PAGE_LIST_END;
		rm_PageHeader->numRecord = 0;
		this->bHeaderDirty = true;
		this->rm_FileHeader.firstFreePage = pageNum;
		char *p = (pageData + this->rm_FileHeader.pageHeaderSize);
		for (short i = 0; i < this->rm_FileHeader.recordNumPerPage; i ++) {
			std::cout << "slot = " << i << " address = " << (short *)p << std::endl;
			*(short *)p = i + 1;
			if (*(short *)p == this->rm_FileHeader.recordNumPerPage)
				*(short *)p = -1;
			p += this->rm_FileHeader.recordSize;
		}
		// TRY(this->pf_FileHandle->UnpinPage(pageNum));
	}

	TRY(this->pf_FileHandle->GetThisPage(this->rm_FileHeader.firstFreePage, pageHandle));
	TRY(pageHandle.GetPageNum(pageNum));
	char *pageData;
	TRY(pageHandle.GetData(pageData));
	RM_PageHeader *rm_PageHeader = (RM_PageHeader *)pageData;
	int &tot = rm_PageHeader->numRecord;
	assert (tot < this->rm_FileHeader.recordNumPerPage);
	slotNum = rm_PageHeader->firstFreeSlot;

	std::cout << "pageNum : " << pageNum << ", slotNum : " << slotNum << std::endl;
	assert(GetBit((unsigned char *)(pageData + sizeof(RM_PageHeader) - 1), slotNum) == false);
	SetBit((unsigned char *)(pageData + sizeof(RM_PageHeader) - 1), slotNum, true);
	char *dest = pageData + this->rm_FileHeader.pageHeaderSize + slotNum * this->rm_FileHeader.recordSize;
	std::cout << "dest address : " << (short *)dest << std::endl;
	TRY(this->pf_FileHandle->MarkDirty(pageNum));
	rm_PageHeader->firstFreeSlot = *(short *) dest;
	memcpy(dest, pData, this->rm_FileHeader.recordSize);
	tot ++;

	if (tot == this->rm_FileHeader.recordNumPerPage) {
		this->bHeaderDirty = true;
		this->rm_FileHeader.firstFreePage = rm_PageHeader->nextPage;
		rm_PageHeader->nextPage = RM_PAGE_USED;
	}
	TRY(this->pf_FileHandle->UnpinPage(pageNum));
	rid = RID(pageNum, slotNum);

    return 0;
}

RC RM_FileHandle::DeleteRec (const RID &rid) {
	TRY(IsValid());
	PageNum pageNum;
	SlotNum slotNum;
	TRY(rid.GetPageNum(pageNum));
	TRY(rid.GetSlotNum(slotNum));
	if (slotNum < 0 || slotNum >= this->rm_FileHeader.recordNumPerPage) return RM_ERR_SLOTNUM;

	PF_PageHandle pageHandle;
	TRY(this->pf_FileHandle->GetThisPage(pageNum, pageHandle));

	char *pageData;
	TRY(pageHandle.GetData(pageData));

	RM_PageHeader *rm_PageHeader = (RM_PageHeader *)pageData;

	if (GetBit((unsigned char *)(pageData + sizeof(RM_PageHeader) - 1), slotNum) == 0) {
		return RM_WAR_NOSUCHRECORD;
	}
	TRY(this->pf_FileHandle->MarkDirty(pageNum));
	unsigned int offset = this->rm_FileHeader.pageHeaderSize + this->rm_FileHeader.recordSize * slotNum;
	memset(pageData + offset, 0, this->rm_FileHeader.recordSize);
	SetBit((unsigned char *)(pageData + sizeof(RM_PageHeader) - 1), slotNum, false);

	if (rm_PageHeader->numRecord == this->rm_FileHeader.recordNumPerPage) {
		rm_PageHeader->nextPage = this->rm_FileHeader.firstFreePage;
		this->rm_FileHeader.firstFreePage = pageNum;
		this->bHeaderDirty = true;
	}
	rm_PageHeader->numRecord --;
	TRY(this->pf_FileHandle->UnpinPage(pageNum));

    return 0;
}

RC RM_FileHandle::UpdateRec (const RM_Record &rec) {
	TRY(IsValid());
	PageNum pageNum;
	SlotNum slotNum;
	if (rec.size != this->rm_FileHeader.recordSize) {
		return RM_ERR_RECSIZE;
	}
	TRY(rec.rid.GetPageNum(pageNum));
	TRY(rec.rid.GetSlotNum(slotNum));
	if (slotNum < 0 || slotNum >= this->rm_FileHeader.recordNumPerPage) {
		return RM_ERR_SLOTNUM;
	}

	PF_PageHandle pageHandle;
	TRY(this->pf_FileHandle->GetThisPage(pageNum, pageHandle));

	char *pData;
	TRY(pageHandle.GetData(pData));

	unsigned int offset = this->rm_FileHeader.pageHeaderSize + this->rm_FileHeader.recordSize * slotNum;
	TRY(this->pf_FileHandle->MarkDirty(pageNum));
	memcpy(pData + offset, rec.data, (size_t)rec.size);
	TRY(this->pf_FileHandle->UnpinPage(pageNum));
    return 0;
}

RC RM_FileHandle::ForcePages (PageNum pageNum) {
    return this->pf_FileHandle->ForcePages(pageNum);
}

RC RM_FileHandle::IsValid() const {
	if (this->pf_FileHandle == NULL || !this->bFileOpen) {
		return RM_ERR_FILENOTOPEN;
	}
	return 0;
}