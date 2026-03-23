#ifndef ___PREFERENCE_H__
#define ___PREFERENCE_H__

#include <Preferences.h>
#define PREF_NAMESPACE "J_CALENDAR"

// Preferences KEY定义
// !!!preferences key限制15字符
#define PREF_SI_CAL_DATE "SI_CAL_DATE" // 屏幕当前显示的日期
#define PREF_SI_WEEK_1ST "SI_WEEK_1ST" // 每周第一天，0: 周日（默认），1:周一
#define PREF_SI_TYPE "SI_TYPE"         // 屏幕显示类型

#define PREF_QWEATHER_HOST "QWEATHER_HOST" // QWEATHER HOST
#define PREF_QWEATHER_KEY "QWEATHER_KEY"   // QWEATHER KEY/TOKEN
#define PREF_QWEATHER_TYPE "QWEATHER_TYPE" // 0: 每日天气，1: 实时天气
#define PREF_QWEATHER_LAT "QWEATHER_LAT"   // 纬度
#define PREF_QWEATHER_LON "QWEATHER_LON"   // 经度
#define PREF_CD_URL "CD_URL"               // 倒计日JSON拉取地址
#define PREF_CD_LOCAL "CD_LOCAL"             // 本地倒计日(分号分隔: MM-DD,名称,0/1;...)

// 假期信息，tm年，假期日(int8)，假期日(int8)...
#define PREF_HOLIDAY "HOLIDAY"

// Microsoft Todo OAuth2 配置
#define PREF_MS_CLIENT_ID "MS_CLIENT_ID"   // Azure AD Client ID
#define PREF_MS_TENANT_ID "MS_TENANT_ID"   // Azure AD Tenant ID (consumers)
#define PREF_MS_REFRESH_TK "MS_REFRESH_TK" // OAuth2 refresh token

#endif
