#ifndef __TODO_H__
#define __TODO_H__

#include <Arduino.h>

// 最多存储的待办项数
#define TODO_MAX_ITEMS 20
// 每个待办标题最大字节数 (UTF-8中文约3字节/字，18字≈54字节+余量)
#define TODO_TITLE_MAX_LEN 80
// 截止时间/日期
#define TODO_DUE_MAX_LEN 16

struct TodoItem {
  char title[TODO_TITLE_MAX_LEN];
  char dueInfo[TODO_DUE_MAX_LEN]; // 截止日期/时间, 如 "14:30" 或 "2/14"
  uint64_t dueSortKey;            // 排序用: YYYYMMDDHHmm, 无截止=UINT64_MAX
  bool important;                 // 高优先级
};

struct TodoData {
  TodoItem items[TODO_MAX_ITEMS];
  uint8_t count;
};

// 启动获取待办（异步任务）
void todo_exec();

// 停止待办任务（如果仍在运行）
void todo_stop();

// 获取状态: -1=未开始, 0=进行中, 1=成功, 2=失败, 3=需要认证(显示device code)
int8_t todo_status();

// 获取待办数据
TodoData *todo_data();

// Device code flow 信息（状态==3时使用）
const char *todo_user_code();
const char *todo_verify_url();

#endif
