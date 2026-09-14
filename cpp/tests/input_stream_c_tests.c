#include "makxd_c.h"
#include <stdio.h>
int main(void) {
    const uint8_t digital[]={0xde,0xad,3,0,0x53,1,31,1};
    uint8_t trigger[]={0xde,0xad,4,0,0x53,3,10,255,3};
    const uint8_t overflow[]={0xde,0xad,3,0,0x53,2,255,255};
    makxd_input_change_t e;
    if (!makxd_input_change_decode(digital,sizeof digital,&e) || e.kind!=MAKXD_STREAM_MOUSE || e.control!=31 || e.value!=1) return 1;
    if (!makxd_input_change_decode(trigger,sizeof trigger,&e) || e.value!=1023 || e.control!=10) return 2;
    trigger[8]=4;if(makxd_input_change_decode(trigger,sizeof trigger,&e))return 3;
    if(!makxd_input_change_decode(overflow,sizeof overflow,&e) || !e.overflow || e.kind!=MAKXD_STREAM_KEYBOARD)return 4;
    if(makxd_input_change_decode(digital,sizeof digital-1,&e))return 5;
    puts("C_STREAM=passed framed=yes trigger=1023 overflow=yes");return 0;
}
