#include "pf.h"
#include "ix.h"
#include "ix_internal.h"
#include <iostream>
#include <cstring>

IX_IndexScan::IX_IndexScan(){

}

IX_IndexScan::~IX_IndexScan(){

}

RC IX_IndexScan::Compare (char *str) {
	int t;
	switch (this->indexHandle->fileHeader.attrType) {
		case STRING:{
			std::memcpy(indexData, str, this->indexHandle->fileHeader.attrLength);
			t = strcmp(indexData, (char *)value);
			break;
		}
		case INT:{
			int A = *(int *)str;
			int B = *(int *)value;
			if (A < B) t = -1;
			if (A == B) t = 0;
			if (A > B) t = 1;
			break;
		}
		case FLOAT:{
			double A = *(double *)str;
			double B = *(double *)value;
			if (A < B) t = -1;
			if (A == B) t = 0;
			if (A > B) t = 1;
			break;
		}
	}
	return t;
}

RC IX_IndexScan::OpenScan(const IX_IndexHandle &indexHandle, CompOp compOp,
					void *value, ClientHint pinHint){
	if (indexHandle.bFileOpen == false || indexHandle.fileHandle == NULL) {
		return IX_ERR_FILENOTOPEN;
	}
	if (value == NULL) {
		return IX_ERR_NULLENTRY;
	}
	if (compOp == NE_OP) {
		return IX_ERR_NEOP;
	}
	if (compOp == NO_OP || compOp == LT_OP || compOp == LE_OP) {
		this->getLeft = true;
	} else {
		this->getLeft = false;
	}
	this->indexHandle = &indexHandle;
	this->compOp = compOp;
	this->value = value;
	this->indexData = (char *)malloc(indexHandle.fileHeader.attrLength);
	this->pDataBuf = (char *)malloc(PF_PAGE_SIZE);
	PF_PageHandle pageHandle;
	//std::cerr << "rootPage = " << indexHandle.fileHeader.rootPage << "\n";
	TRY(indexHandle.fileHandle->GetThisPage(indexHandle.fileHeader.rootPage, pageHandle));
	while(true) {
		char *pData;
		TRY(pageHandle.GetData(pData));
		IX_PageHeader *pageHeader = (IX_PageHeader *)pData;
		//std::cerr << "nextPage = " << pageHeader->nextPage << "\n";
		//std::cerr << "numIndex = " << pageHeader->numIndex << "\n";

		pData = pData + sizeof(IX_PageHeader);
		if (pageHeader->nextPage == IX_PAGE_NOT_LEAF) {
			int pos = pageHeader->numIndex - 1;
			for (int i = 0; i < pageHeader->numIndex; i ++) {
				char *dest = pData + i * indexHandle.fileHeader.entrySize;
				int res = Compare(dest);
				if (res <= 0) pos = i;
				if (res > 0) break;
			}
			char *dest = pData + pos * indexHandle.fileHeader.entrySize;
			PageNum pageNum, newPageNum;
			TRY(pageHandle.GetPageNum(pageNum));
			newPageNum = *(PageNum *)(dest + indexHandle.fileHeader.attrLength + sizeof(RID));
			indexHandle.fileHandle->UnpinPage(pageNum);
			indexHandle.fileHandle->GetThisPage(newPageNum, pageHandle);
		} else {
			TRY(pageHandle.GetPageNum(currentPage));
			currentSlot = 0;
			char *pData;
			TRY(pageHandle.GetData(pData));
			memcpy(this->pDataBuf, pData, PF_PAGE_SIZE);
			indexHandle.fileHandle->UnpinPage(currentPage);
			break;
		}
	}
	return 0;
}

RC IX_IndexScan::Check(char *str){
	int t;
	switch (this->indexHandle->fileHeader.attrType) {
		case STRING:{
			std::memcpy(indexData, str, this->indexHandle->fileHeader.attrLength);
			t = strcmp(indexData, (char *)value);
			break;
		}
		case INT:{
			int A = *(int *)str;
			int B = *(int *)value;
			//std::cerr << "A = " << A << " B = " << B << "\n";
			if (A < B) t = -1;
			if (A == B) t = 0;
			if (A > B) t = 1;
			break;
		}
		case FLOAT:{
			double A = *(double *)str;
			double B = *(double *)value;
			if (A < B) t = -1;
			if (A == B) t = 0;
			if (A > B) t = 1;
			break;
		}
	}
	switch (this->compOp) {
		case NO_OP:{
			return IX_CHECK_OK;
		}
		case EQ_OP:{
			if (t == -1) return IX_CHECK_WILLOK;
			else if (t == 0) return IX_CHECK_OK;
			else return IX_CHECK_EOF;
		}
		case NE_OP:{
			return IX_ERR_NEOP;
		}
		case LE_OP:{
			if (t <= 0) return IX_CHECK_OK;
			else return IX_CHECK_EOF;
		}
		case LT_OP:{
			if (t < 0) return IX_CHECK_OK;
			else return IX_CHECK_EOF;
		}
		case GT_OP:{
			if (t <= 0) return IX_CHECK_WILLOK;
			else return IX_CHECK_OK;
		}
		case GE_OP:{
			if (t < 0) return IX_CHECK_WILLOK;
			else return IX_CHECK_OK;
		}
	}
}

RC IX_IndexScan::GetNextEntry(RID &rid){
	while (true) {
		if (currentPage == IX_PAGE_LIST_END) {
			return IX_EOF;
		}
		IX_PageHeader *pageHeader = (IX_PageHeader *)this->pDataBuf;
		//std::cerr << "currentSlot = " << currentSlot << "\n";
		int offset = sizeof(IX_PageHeader) + currentSlot 
			* this->indexHandle->fileHeader.entrySize;
		//std::cerr << "offset = " << offset << "\n";
		char *dest = this->pDataBuf + offset;
		int res = Check(dest);
		this->currentSlot ++;
		if (res == IX_CHECK_OK) {
			rid = *(RID *)(dest + this->indexHandle->fileHeader.attrLength);
		}
		if (res == IX_CHECK_EOF) return IX_EOF;
		if (this->currentSlot == pageHeader->numIndex) {
			this->currentSlot = 0;
			currentPage = pageHeader->nextPage;
			if (currentPage == IX_PAGE_LIST_END) {
				if (res != IX_CHECK_OK) return IX_EOF;
				return 0;
			} 
			PF_PageHandle pageHandle;
			char *pData;
			TRY(this->indexHandle->fileHandle->GetThisPage(currentPage, pageHandle));
			TRY(pageHandle.GetData(pData));
			memcpy(this->pDataBuf, pData, PF_PAGE_SIZE);
		}
		if (res == IX_CHECK_OK) return 0;
	}
}

RC IX_IndexScan::CloseScan(){
	free(pDataBuf);
	free(indexData);
	return 0;
}