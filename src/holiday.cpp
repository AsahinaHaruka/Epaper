#include "holiday.h"

#include "Arduino.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

// 获取当月假期信息。数组中数字为当月日期，正为休假，负为补工作日
// https://timor.tech/api/holiday/
bool getHolidays(Holiday &result, int year, int month)
{
    // 1. 构建 URL
    String req = "https://timor.tech/api/holiday/year/" + String(year) + "-" + String(month);
    Serial.printf("Requesting: %s\n", req.c_str());

    // 2. 配置 HTTPS 安全客户端
    WiFiClientSecure client;
    client.setInsecure(); // 关键：跳过证书验证，否则 HTTPS 会连接失败

    HTTPClient http;
    http.setTimeout(10 * 1000); // 10秒超时

    // 3. 启动连接 (传入 client)
    if (!http.begin(client, req))
    {
        Serial.println("Connection failed!");
        return false;
    }

    int httpCode = http.GET();

    // 5. 检查 HTTP 状态码
    if (httpCode != HTTP_CODE_OK)
    {
        Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
        http.end();
        return false;
    }

    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, http.getStream());

    if (error)
    {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        http.end();
        return false;
    }
    http.end(); // 解析完即可关闭连接

    // 7. 业务逻辑检查
    int status = doc["code"].as<int>();
    if (status != 0)
    {
        Serial.println("API logic error (code != 0)");
        return false;
    }

    result.year = year;
    result.month = month;
    JsonObject oHoliday = doc["holiday"].as<JsonObject>();

    int i = 0;
    Serial.print("Holidays found: ");

    for (JsonPair kv : oHoliday)
    {
        // key 格式例如 "02-14"
        String key = String(kv.key().c_str());
        JsonObject value = kv.value();
        bool isHoliday = value["holiday"].as<bool>();

        // 解析日期: key="02-14", substring(3,5) 取索引3和4，即 "14"
        int day = key.substring(3, 5).toInt();

        // 防止数组越界
        if (i < 50)
        {
            // 假期为正数，调休/补班为负数
            result.holidays[i] = day * (isHoliday ? 1 : -1);
            Serial.printf("[%02d: %d] ", day, result.holidays[i]);
            i++;
        }
    }
    Serial.println();
    result.length = i;

    return true;
}
