#include "rm_rid.h"

RID::RID()
{
	page = -1;
    slot = -1;
}

RID::RID(PageNum pageNum, SlotNum slotNum){
    page = pageNum;
    slot = slotNum;
}

RID::~RID(){

}

RC RID::GetPageNum(PageNum &pageNum) const{
    pageNum = page;
    return 0;
}

RC RID::GetSlotNum(SlotNum &slotNum) const{
    slotNum = slot;
    return 0;
}


