# ESP32 E-Paper Calendar

[![PlatformIO](https://img.shields.io/badge/PlatformIO-Compatible-orange.svg)](https://platformio.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](https://opensource.org/licenses/MIT)

这是一个基于 ESP32的墨水屏桌面智能日历

## ✨ 核心特性

- **智能局部刷新**：支持分钟级别的时钟无闪烁局部刷新。
- **和风天气**：
  - 实时显示当地温湿度、空气质量 (AQI)。
  - 展示今日详细天气（温度区间、风向风力、降水概率）。
  - 提供未来两天的天气预报。
- **Microsoft To Do 待办同步**：采用垂直时间轴的排版方式。支持 OAuth2 授权接入。
- **倒数日**：
  - 支持最多 20 个本地配置的倒计日（支持公历/农历）。
  - 支持通过远端 JSON 接口拉取动态倒数日配置（自行配置服务器）。
- **SHT30 高精度室内温湿**：使用SHT30读取温湿度。
- **网络时间与节假日历**：实时同步 SNTP 时间，自动获取中国法定节假日和调休安排，并在日历视图中标注。

## 📦 目录结构

```text
├── assets/                  # 相关的图标、参考设计图片 (SVG等)
├── include/                 # 头文件目录，包含所有核心结构的定义和配置
│   ├── config.h             # 硬件引脚定义与系统全局常量
│   ├── _preference.h        # NVS(Preferences) 本地存储的 Key 定义
│   ├── icons.h              # PROGMEM 的点阵图标
│   ├── ...
├── src/                     # C++ 源文件大本营
│   ├── main.cpp             # 主程序：休眠控制、供电时序、工作流分发
│   ├── screen_ink.cpp       # 屏幕渲染总管（天气、日历、待办排版计算）
│   ├── weather.cpp          # 和风天气 API 交互解析
│   ├── todo.cpp             # 微软 Graph API (To-Do) 交互与 OAuth2 授权流
│   ├── sntp.cpp             # SNTP 对时与节假日(聚合数据API)解析
│   ├── countdown.cpp        # 倒计时多轨合并与日期逻辑计算
│   ├── battery.cpp          # ADC 电池电压与拟合电量计算
│   └── API.hpp              # HTTP(S) 请求封装及 GZIP 解压逻辑
├── platformio.ini           # PlatformIO 编译配置文件
└── README.md
```

---

## 🚀 快速上手

### 1. 平台准备
强烈建议使用 [VSCode + PlatformIO](https://platformio.org/install/ide?install=vscode) 进行编译与上传。
项目依赖的库（如 `GxEPD2`, `U8g2_for_Adafruit_GFX`, `ArduinoJson`, `WiFiManager` 等）已在 `platformio.ini` 中配置，编译时会自动拉取。

### 2. 获取必要的 API Keys
在开始配网前，你需要准备以下两种 API 的服务凭据：
- **和风天气 (QWeather)**：
  前往 [和风天气开发者控制台](https://dev.qweather.com/) 注册并创建应用，获取 `API Key`，并确定你要监控的城市 `Location ID` (经纬度)。
- **Microsoft To Do**：
  前往 [Azure Active Directory admin center](https://aad.portal.azure.com/) 注册应用，获取 `Client ID` 和 `Tenant ID` (个人用户填 `consumers`)。需赋予应用 `Tasks.Read` 权限。

### 3. 接线及修改引脚
如果您使用的不是标准打样的开发板，请务必前往 `include/config.h` 和 `include/wiring.h` 检查 SPI 引脚、I2C 引脚及电池检测引脚（包括使能引脚 `WAKE_IO_PIN`）是否与您的硬件连接一致。

### 4. 编译上传与配网
1. 连上串口，点击 PlatformIO 的 Upload 烧录固件。
2. 首次开机时由于没有网络，设备会开启名为 `ESP32-Epaper-Setup` (或类似名称) 的热点。
3. 手机连接热点后，会自动弹出版面清晰的配网 Portal。
4. 输入你的 WiFi 密码，并填入之前准备好的**和风 API Key**、**微软 Client ID** 以及**本地倒计日内容**。
5. 保存并重启后，串口输出会提示TODO授权。

---

## ⚙️ 核心配置说明

### 本地倒计日格式
配网页面中有一个【本地倒计日】的输入框。由于节省空间的需要，采用分号 `;` 分隔多项。
**格式：** `MM-DD,事件名称,农历标识(0或1)` 
**示例：** `01-01,元旦,0;08-15,中秋,1;05-01,劳动节,0`
*(注：最多可配置 20 项，系统会自动筛选并显示最近的一个倒计日)*

---

## 🤝 贡献与二次开发
欢迎提 Issue 或 Pull Request 加入共建。

## NOTE
*本项目大量使用AI开发，可能存在问题请仔细校验*

## Reference:
1. 本项目有大量代码参考自J-Calendar项目
「J-Calendar」https://github.com/JADE-Jerry/jcalendar
2. 局刷驱动来自FastFreshBWOnColor「FastFreshBWOnColor」https://github.com/yanyuandi/FastFreshBWOnColor
3. 硬件购买自淘宝成品，在此之上设计了功能。商家已开源原理图可参照原理图自行构建。https://gitee.com/corogoo/epaper-7.5-weather-station-plus/tree/master#https://gitee.com/link?target=https%3A%2F%2Fdocs.qq.com%2Fsheet%2FDQ2x3RnVLSGtXenhp
4. 天气图标来自「和风天气」https://github.com/qwd/Icons
