#ifndef __COUNTDOWN_H__
#define __COUNTDOWN_H__

#include <Arduino.h>

#define CD_MAX_ITEMS 20
#define CD_LABEL_MAX_LEN 32

struct CountdownItem {
  char label[CD_LABEL_MAX_LEN];
  char dateStr[16]; // MM-DD
  bool isLunar;
  int daysRemaining; // Computed value, -1 means invalid or past
  uint64_t targetTime; // Used for sorting, simple YYYYMMDD
};

struct CountdownData {
  CountdownItem items[CD_MAX_ITEMS];
  int count;
};

// Start or synchronize countdown items
void countdown_exec();

// Status:
// -1: Init
//  0: Fetching/Running
//  1: Success
//  2: Network Error
//  3: Invalid Config
int8_t countdown_status();

// Access the global data
CountdownData* countdown_data();

// Helper to calculate days remaining from MM-DD string
void calculate_countdown_days(CountdownItem& item);

#endif // __COUNTDOWN_H__
