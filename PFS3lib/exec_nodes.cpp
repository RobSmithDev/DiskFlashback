#include "exec_nodes.h"
#include <time.h>

void NewList(struct List* lh) {
	lh->lh_Head = (struct Node*)&(lh->lh_Tail);
	lh->lh_Tail = NULL;
	lh->lh_TailPred = (struct Node*)&(lh->lh_Head);
}

bool isLeapYear(const int year) {
    return (!(year % 100) ? !(year % 400) : !(year % 4));
}

void PFS3_DateTime2DateStamp(const struct tm* inStamp, struct DateStamp* stamp) {
    struct tm tmp = *inStamp;
    // Ensure all fields are populated
    tmp.tm_isdst = -1;
    mktime(&tmp);

    stamp->ds_Minute = inStamp->tm_hour * 60 + inStamp->tm_min;
    stamp->ds_Tick = inStamp->tm_sec * 50;
    stamp->ds_Days = tmp.tm_yday;


    int tmYear = inStamp->tm_year;   // years since 1900
    while (tmYear > 78) {
        stamp->ds_Days += isLeapYear(1900 + tmYear) ? 366 : 365;
        tmYear--;
    }
}

void PFS3_DateStamp2DateTime(const struct DateStamp* stamp, struct tm* inStamp) {
    inStamp->tm_min = stamp->ds_Minute % 60;
    inStamp->tm_hour = stamp->ds_Minute / 60;
    inStamp->tm_sec = stamp->ds_Tick / 50;

    static const int daysInMonth[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    inStamp->tm_year = 1978;
    inStamp->tm_mday = stamp->ds_Days;

    // Remove the years
    int yearDays = isLeapYear(inStamp->tm_year) ? 366 : 365;
    while (inStamp->tm_mday >= yearDays) {
        inStamp->tm_mday -= yearDays;
        inStamp->tm_year++;
        yearDays = isLeapYear(inStamp->tm_year) ? 366 : 365;
    }
    inStamp->tm_yday = yearDays;

    // Remove the months
    for (inStamp->tm_mon = 0; inStamp->tm_mon < 12; inStamp->tm_mon++) {
        const int monthDays = ((inStamp->tm_mon == 1) && (isLeapYear(inStamp->tm_year))) ? 29 : daysInMonth[inStamp->tm_mon];
        if (inStamp->tm_mday < monthDays) break;
        inStamp->tm_mday -= monthDays;
    }
    inStamp->tm_wday = 0; // unknown
    inStamp->tm_year -= 1900;
    inStamp->tm_isdst = -1;
    mktime(inStamp);
}


void DateTime(struct DateStamp* r) {
    struct tm local;
    time_t cal;

    time(&cal);
    localtime_s(&local,& cal);

    PFS3_DateTime2DateStamp(&local, r);
   
    r->ds_Minute = local.tm_hour * 60 + local.tm_min;
    r->ds_Tick = local.tm_sec * 50;
    r->ds_Days = local.tm_yday;

    while (local.tm_year > 78) {
        r->ds_Days += isLeapYear(1900 + local.tm_year) ? 366 : 365;
        local.tm_year--;
    }
}

void SmartSwap(uint16_t& p) {
    p = (p << 8) | (p >> 8);
}


void SmartSwap(uint32_t& p) {
    p  = (p >> 24) | (p << 24) | ((p >> 8) & 0xFF00) | ((p << 8) & 0xFF0000);
}

void SmartSwap(int16_t& p) {
    p = (p << 8) | (p >> 8);
}

void SmartSwap(int32_t& p) {
    p = (p >> 24) | (p << 24) | ((p >> 8) & 0xFF00) | ((p << 8) & 0xFF0000);
}

