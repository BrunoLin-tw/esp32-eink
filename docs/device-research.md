# ELECROW CrowPanel ESP32 5.79 吋電子紙裝置研究

## 摘要

ELECROW CrowPanel ESP32 5.79 吋電子紙 HMI（型號 `DIS08792E`）是一塊整合 ESP32-S3、黑白電子紙、按鍵、microSD、UART 與 GPIO 擴充接頭的開發板。本文針對硬體／軟體 V1.0 整理可直接用於韌體開發的資料。它沒有觸控與背光；USB-C 經過 CH340C，並非 ESP32-S3 原生 USB。

面板可見解析度是 792x272，但官方 driver 使用 800x272、1 bpp 的內部 framebuffer，以配合兩顆級聯 SSD1683 的位址配置。這兩個尺寸不能互換。官方 Arduino 程式可用來驗證硬體，後續應用則可評估 `GxEPD2_579_GDEY0579T93`；此 class 的支援始於 GxEPD2 1.5.9，本文實作細節已對照 1.6.9。

本文使用以下標記區分資料性質：

- **官方／電路圖確認**：ELECROW 商品頁、Wiki、tutorial、repository、原理圖或原廠程式可以直接核對的內容。
- **函式庫實作**：來自 GxEPD2 或官方 driver 的程式行為與常數，不等於實機量測保證。
- **社群經驗／推論**：社群實作、電路推算或建議驗證方法；採用前需以手上板卡和儀器確認。

## 型號與資料可信度

**官方／電路圖確認**

| 項目 | 資料 |
| --- | --- |
| 商品名稱 | ELECROW CrowPanel ESP32 5.79-inch E-Paper HMI |
| SKU／型號 | `DIS08792E` |
| 硬體版本 | V1.0 |
| 軟體版本 | V1.0 |
| MCU 模組 | `ESP32-S3-WROOM-1-N8R8` |
| 面板 | 5.79 吋、792x272、黑白 AM EPD |

ELECROW 的商品頁、Wiki、Arduino tutorial、GitHub repository 與原理圖是本文件的主要依據，但官方資料彼此並非完全一致。最明顯的例子是 Arduino IDE 設定截圖顯示 4 MB Flash，而型號中的 N8 與硬體資料均指向 8 MB Flash。開發設定應採 8 MB，燒錄前仍應讀取晶片資訊確認實際板卡。

官方 repository 提供 Arduino 範例、driver、電路及 factory firmware，但軟體成熟度有限。2026-08-19 檢查時，repository 只有兩筆實質 commit、沒有 release，兩個公開 issue 也仍為 open。因此，官方程式適合做硬體基準，不宜把未驗證的行為視為穩定 SDK 介面。

## 硬體規格

**官方／電路圖確認**

| 項目 | 規格 |
| --- | --- |
| MCU | `ESP32-S3-WROOM-1-N8R8`，最高 240 MHz |
| Flash | 8 MB |
| PSRAM | 8 MB OPI PSRAM |
| 顯示技術 | 黑白 active-matrix electrophoretic display（AM EPD） |
| 可見解析度 | 792x272，1 bpp |
| 有效顯示區 | 139.00x47.74 mm |
| Pixel pitch | 0.1755 mm |
| 顯示 controller | 兩顆級聯 SSD1683 |
| 額定操作溫度 | 0 至 50 °C |
| 額定儲存溫度 | -25 至 70 °C |
| 電池輸入 | SH1.0-2P，3.7 V 單節（1S）LiPo／Li-ion |
| USB | USB-C 經 CH340C USB-to-UART bridge |

電子紙在停止供電後仍可保留最後畫面，但這不代表更新期間可以切斷顯示電源。更新必須完成並等到 `BUSY` 解除，再進入休眠和關閉顯示電源路徑。

## 外觀、按鍵與介面

**官方／電路圖確認**

板上可用介面如下：

- microSD／TF 卡槽。
- `UART0` 介面。
- 2x10 GPIO 擴充接頭，共引出 12 個可用 GPIO，另含電源與接地等接點。
- SH1.0-2P、3.7 V 1S 電池接頭。
- USB-C，連至 CH340C，供電及燒錄均經此路徑。
- `MENU/HOME`、`EXIT/BACK`、搖桿的上、下與按壓，以及獨立 `BOOT`、`RESET`。
- `PWR` LED。

此型號沒有觸控層，也沒有背光。文件或程式若出現 touch controller、觸控校正、亮度 PWM 或 backlight GPIO，不能直接套用到 `DIS08792E`。

按鍵是 active-low：未按下時讀到高電位，按下後接地並讀到低電位。韌體應使用適當的 pull-up 與 debounce。`RESET` 鍵直接作用於 ESP32-S3 的 `EN`，不是一般 GPIO。

## 腳位定義

以下腳位均屬**官方／電路圖確認**。GPIO 編號是 ESP32-S3 GPIO，不是接頭腳位序號。

### 顯示器

| 信號 | GPIO | 說明 |
| --- | ---: | --- |
| `SCK` | 12 | 顯示 SPI clock |
| `MOSI` | 11 | 顯示 SPI data |
| `RESET` | 47 | SSD1683 reset |
| `DC` | 46 | Data／command select |
| `CS` | 45 | 顯示 chip select |
| `BUSY` | 48 | 顯示器 busy 狀態 |
| `EPD_GND_EN` | 7 | 經 MOSFET 控制顯示器接地／供電使能路徑 |

GPIO7 不是 SPI 信號。啟用顯示器時需依官方硬體流程將它設為有效狀態；低功耗關閉時才拉低。若在 controller 忙碌時切換 GPIO7，可能中斷更新或讓 controller 狀態失步。

### 按鍵與系統控制

| 功能 | GPIO／信號 | 有效狀態 |
| --- | ---: | --- |
| `MENU/HOME` | 2 | Low |
| `EXIT/BACK` | 1 | Low |
| Rocker up | 6 | Low |
| Rocker down | 4 | Low |
| Rocker press | 5 | Low |
| `BOOT` | 0 | Low；重置時拉低可進入下載模式 |
| `RESET` | `EN` | Low reset；不是 GPIO |

### microSD／TF

| 信號 | GPIO |
| --- | ---: |
| `MOSI` | 40 |
| `MISO` | 13 |
| `SCLK` | 39 |
| `CS` | 10 |
| Power enable | 42 |

microSD 與電子紙使用不同的 SPI 腳位。啟用卡片前應先設定 GPIO42；低功耗時可在檔案 flush、關閉卡片介面後停用此電源路徑。

### UART、LED 與擴充 GPIO

| 功能 | GPIO |
| --- | --- |
| `UART0 RX` | 44 |
| `UART0 TX` | 43 |
| `PWR` LED | 41 |
| 擴充 GPIO | 3、8、9、14、15、16、17、18、19、20、21、38 |

擴充腳位在接上外部模組前仍需核對電壓、啟動 strapping、輸入輸出能力及現有板載用途。僅有 GPIO 清單，不代表每個腳位都能在所有開機狀態下任意使用。

## 顯示器架構

**官方／電路圖確認與官方 driver 實作**

面板由兩顆 SSD1683 級聯。每顆負責 396x272 個可見 pixel，合計形成 792x272 畫面。controller 的 RAM 位址在接縫處有額外欄位，因此官方 driver 使用 800x272 的虛擬 framebuffer，而不是直接把 792x272 bitmap 傳給 `EPD_Display()`。

```text
Visible image:       792 x 272 / 8 = 26,928 bytes at 1 bpp
Elecrow framebuffer: 800 x 272 / 8 = 27,200 bytes at 1 bpp
Controller split:    2 x (396 x 272 visible), plus seam/address padding
```

兩種官方 API 的輸入契約不同：

- `EPD_ShowPicture()` 接受邏輯 792x272 圖片，傳送時插入雙 controller 接縫所需的 padding。
- `EPD_Display()` 預期收到已按官方內部配置排列的 800x272 framebuffer，也就是 27,200 bytes。

把 26,928-byte 圖片直接交給期待 27,200 bytes 的路徑，會造成錯位、接縫異常或越界讀取。反過來，把內部 800x272 buffer 當成一般 792x272 圖片處理，也會把 padding 顯示邏輯混入可見內容。

**函式庫實作**

GxEPD2 的 `GxEPD2_579_GDEY0579T93` class 封裝雙 SSD1683、接縫和視窗位址處理。應用層仍以面板的 792x272 邏輯座標繪圖，不應自行假設官方 800x272 buffer 可直接代入 GxEPD2 的資料路徑。

## 刷新模式與殘影

**官方 driver 實作**

| API | Update control value | 用途 |
| --- | ---: | --- |
| `EPD_Update()` | `0xF7` | Full update |
| `EPD_FastUpdate()` | `0xC7` | Fast update |
| `EPD_PartUpdate()` | `0xDC` | Partial update waveform |
| `EPD_DeepSleep()` | 不適用 | 讓顯示 controller 進入 deep sleep |

官方 partial-refresh 範例雖然選用 partial update waveform，資料傳輸仍送出完整 800x272 內部 framebuffer。官方 driver 沒有提供只傳矩形區域的 API。因此，「partial」在這個範例中主要描述驅動波形，不代表 SPI 只傳局部矩形，也不能直接推導傳輸時間按更新面積縮短。

**Driver／library 實作**

電子紙 controller 的局部刷新流程使用 previous／OLD RAM 與 current／NEW RAM 資料。Driver 或 library 必須依其流程維護兩份 RAM，讓 controller 能以新舊資料的差異執行局部驅動；只寫入一份 RAM 後直接送出 update command，兩份資料便不再同步。

官方 driver 的 `EPD_Display_Clear()` 對 master controller 的 current RAM `0x24` 寫入 `0xFF`，對 previous RAM `0x26` 寫入 `0x00`；slave controller 的 `0xA4`／`0xA6` 也採相同的 current／previous 差異。`EPD_Update()` 只經 `0x22`、`0x20` 觸發 full waveform，不會把 current RAM 複製到 previous RAM。因此，執行 full update 本身不保證兩份 RAM 同步。

在第一次 differential partial refresh 前，previous RAM 必須同步成當前畫面的內容。官方 `EPD_Clear_R26A6H()` 只把 previous RAM 的 `0x26`／`0xA6` 寫成 `0xFF`，適合 current image 也是全白時建立 white baseline。若 current image 是任意內容，必須把同一份影像明確寫入 previous RAM，或使用已確認會維護 current／previous RAM 的 library flow；官方 driver 沒有可在此不加驗證便推薦的通用高階 API。

**工程／實務建議，不是 ELECROW 官方保證**

開機或喚醒後，OLD RAM 可能沒有可依賴的前一畫面；軟體若未維持新舊 RAM 同步，也可能出現內容不變、局部漏刷、黑白反轉過程異常或殘影增加。這些是工程上應防範的結果，不是每次都會出現的固定故障模式。

Ghosting 是局部或快速刷新後仍看見前一畫面的現象，程度會受溫度、圖案、黑白翻轉比例、面板個體與 waveform 影響。實務上，開機、重置、喚醒或 controller 重新上電後先做一次 full refresh，以建立已知的可見畫面；接著仍要另行確認 OLD／NEW RAM 已同步，再開始 differential partial refresh。這是穩健的起始策略，不是原廠保證或強制週期。

- 可先設定每 5 至 30 次 partial refresh 插入一次 full refresh，再依實機殘影、更新內容和溫度調整。
- 畫面大幅變更、跨黑白高反差區域，或已看見殘影時，提早做 full refresh。
- 所有 `BUSY` 等待都應有 timeout，長迴圈應讓出執行時間，避免硬體異常時永久卡住或觸發 watchdog。

**函式庫實作，不是板卡量測**

GxEPD2 1.6.9 header 對此 panel class 列出的名目時間是 full refresh 2.2 秒、partial refresh 450 ms。這是函式庫用於控制流程的數值，不是 ELECROW 對此整板、所有溫度和所有供電條件的保證。應以實機從觸發更新到 `BUSY` 解除的時間為準。

## 電源、電池與低功耗

**官方／電路圖確認**

板卡可由 USB-C 或 SH1.0-2P 接頭上的 3.7 V 單節 LiPo／Li-ion 供電。電池應自帶保護電路。接電前必須用電表量測板端和電池端極性，不可依紅黑線顏色判斷；不同供應商可能使用相同接頭但極性相反。

原理圖可見 4054A 類型的線性充電器，但沒有明顯的電芯過充、過放與短路保護 IC。這表示不能把「板上可充電」等同於「板上有完整 cell protection」。原理圖也沒有提供可讓 ESP32-S3 量測電池電壓或讀取充電狀態的 ADC／status GPIO。應用程式若要顯示電量，需要另加經驗證的量測電路，不能從目前硬體直接讀出可靠百分比。

**原理圖確認**

GPIO7 經 MOSFET 切換顯示器的 ground path。它不是整板總電源開關，拉低後 ESP32-S3、CH340C、LED、microSD 或其他電路未必隨之斷電。

**電路推論，中低信心**

依 4054A 類充電器及約 2 kΩ program resistor 推算，充電電流可能約為 500 mA。這不是 ELECROW 保證值，也未經板上量測；實際數值會受所用 IC 變體、元件容差、USB 電源、溫度調節與電池狀態影響。選擇電池時不得只依此推算決定允許充電倍率。

**社群經驗／建議流程**

低功耗起始順序可採：

1. 完成畫面更新。
2. 等待 `BUSY` 解除，並設 timeout 處理異常。
3. 呼叫 `EPD_DeepSleep()` 或對應函式庫的 controller hibernate。
4. 將 GPIO7 拉低，關閉顯示器接地／供電路徑。
5. 停用 Wi-Fi、BLE 與不需要的周邊；若使用 microSD，先 flush、卸載再關閉 GPIO42。
6. 設定喚醒來源後進入 ESP32 deep sleep。

喚醒時反向處理：先恢復必要的板上電源控制，拉起 GPIO7，等待電源穩定，重新初始化顯示 controller，建立已知 framebuffer 狀態，再進行 full refresh。Full refresh 後仍須依 driver／library 的已驗證流程，把 current image 同步到 previous RAM，才能開始 differential partial refresh；不要假設 controller 經 GPIO7 斷電後仍保留可用的 OLD RAM。

目前沒有可信的官方整板 sleep-current 數值。任何續航估算都應分別量測 deep sleep、顯示更新、Wi-Fi／BLE、microSD、PWR LED 與 CH340C 所造成的電流；只引用 ESP32-S3 晶片的 deep-sleep 規格會低估整板耗電。

## USB、燒錄與開發環境

**官方／電路圖確認**

USB-C 連到 CH340C，再接 ESP32-S3 `UART0`，因此 Arduino 選單中的 `USB CDC On Boot` 應設為 `Disabled`。常見 serial device 名稱如下：

- Linux：`/dev/ttyUSB*`
- macOS：`/dev/cu.usbserial-*`
- Windows：`COM` 編號，例如 `COM5`

CH340C driver 是否需要另裝取決於作業系統版本。若 port 未出現，先檢查資料線、供電、系統裝置清單與 driver，不要用原生 USB CDC 的裝置名稱排查。

建議 Arduino board options：

| 選項 | 建議值 |
| --- | --- |
| Board | `ESP32S3 Dev Module` |
| Flash Size | `8MB` |
| PSRAM | `OPI PSRAM` |
| Partition Scheme | `Huge APP (3MB No OTA/1MB SPIFFS)`，僅用於重現官方範例 |
| USB CDC On Boot | `Disabled` |
| Upload Speed | 先用 `460800`，不穩時降至 `115200` |

官方 tutorial 截圖中曾出現 4 MB Flash，和 N8 模組及官方 8 MB 規格不一致。`Huge APP (3MB No OTA/1MB SPIFFS)` 可重現官方範例的配置，但其 partition table 只映射前 4 MiB，實體 8 MB Flash 的其餘空間不會被該配置使用。

正式專案應依 OTA、app 大小與 filesystem 容量需求，選擇涵蓋 8 MiB 的 partition layout 或維護 custom CSV。Arduino core 與 board package 的選單名稱可能不同，因此本文不指定未經核對的 8 MiB menu option。設定後要檢查產生的 partition table 結束位址，並用燒錄工具確認實際 Flash；若板卡批次不同，應記錄模組字樣與偵測結果，而不是照抄官方截圖。

自動進入下載模式失敗時，可手動操作：

1. 按住 `BOOT`。
2. 點按 `RESET`。
3. 先放開 `RESET`，再放開 `BOOT`。
4. 立即重新執行 upload。

此順序讓 GPIO0 在 reset 取樣時保持低電位。燒錄完成後按一次 `RESET` 啟動應用程式。

Arduino 是 ELECROW 有實際範例支援的環境。PlatformIO 使用 Arduino framework 時，可沿用相同 GPIO、8 MB Flash 與 OPI PSRAM 設定；partition 則應依正式專案需求採 8 MiB layout 或 custom CSV。建立專案後仍需先做最小化編譯和 upload 驗證。

## 官方軟體資源

**官方 repository 確認**

ELECROW repository 內共有 12 個 Arduino sketch：

1. `5.79_WIFI_refresh`
2. `5.79_ble_refresh`
3. `5.79_wifi_http_openweather`
4. `5.79_BLE`
5. `5.79_GPIO`
6. `5.79_Global_refresh`
7. `5.79_PWR`
8. `5.79_TF`
9. `5.79_key`
10. `5.79_partial_refresh`
11. `5.79_wifi`
12. `5.79_wifi_http`

官方 EPD driver 檔案是複製到各 sketch 內使用，並非獨立、版本化的 Arduino library。修改某個範例中的 driver 不會自動更新其他範例。若後續仍以官方 driver 開發，應先整理單一來源並鎖定版本，以免不同 sketch 的副本逐漸分歧。

涉及 JSON 的範例依用途使用 `Arduino_JSON` 0.2.0 或 `ArduinoJson` 7.1.0。重現原廠範例時先固定這些版本，不要同時把兩套套件的 API 混用。

Factory firmware 以四個 binary 和固定 offset 還原：

```text
bootloader   0x0000
partitions   0x8000
boot_app0    0xE000
app          0x10000
```

燒錄參數需使用該型號資料夾指定的 DIO、80 MHz，不能只拿 app binary 從 `0x0000` 寫入。原廠檔案在 [`factory_firmware`](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/tree/master/factory_firmware)；還原前應保存目前韌體，並再次核對 binary 名稱、offset、Flash 大小與目標型號。

官方頁面聲稱可使用 Arduino、ESP-IDF 與 MicroPython，但 repository 只有可直接辨識的板卡專用 Arduino sketches／driver。沒有板卡專用的 ESP-IDF component、範例工程或 MicroPython display driver 可供等價使用。這是現有 SDK 完整度的限制，不代表 ESP-IDF 或 MicroPython 在技術上不能驅動此硬體。

## GxEPD2 開發建議

**函式庫實作**

GxEPD2 1.5.9 首次加入 `GxEPD2_579_GDEY0579T93` 支援；本文已對照 1.6.9 的 header 與實作。此 class 對應 792x272、雙 SSD1683 的 GDEY0579T93 類型面板。採用時應：

- 以 CrowPanel 的實際 `CS` 45、`DC` 46、`RESET` 47、`BUSY` 48 建立 display instance，SPI 使用 `SCK` 12、`MOSI` 11。
- 在 library 初始化前先正確啟用 GPIO7。
- 使用 GxEPD2 的 page／window API 管理 framebuffer，不把官方 driver 的 800x272 raw buffer 直接傳入。
- 保留 library 對 previous／current image RAM、full／partial mode 切換與 hibernate 的處理順序。
- 將 2.2 秒 full、450 ms partial 視為 library timing constant，另做實機紀錄。

在 1.6.9 實作中，`writeImage()`／`writeImagePart()` 可依矩形限制 RAM data write；但 `refresh(x, y, w, h)` 最後仍把 master 與 slave controller 都設為完整 half-panel area，再觸發 partial update。小矩形可減少部分資料寫入量，不能據此假設面板刷新範圍同樣縮小，也不能預期 refresh time 和矩形面積成比例。

第一次 bring-up 先用全白、全黑、棋盤格、邊框與接縫線測試完整畫面，再測文字和 partial refresh。這能較快分辨腳位錯誤、黑白極性、X 軸映射、雙 controller 接縫或刷新狀態問題。

## ESPHome 與 MicroPython

### ESPHome

**社群實作**

ESPHome 目前沒有 merged upstream 的 CrowPanel 5.79 吋原生支援。ESPBoards 的 [`esphome-lvgl-crowpanel-epaper-5.79-4.2`](https://github.com/ESPBoards/esphome-lvgl-crowpanel-epaper-5.79-4.2) 提供 external component／範例，可作為社群參考，但它不是 ELECROW 官方 SDK，也不等於 ESPHome upstream 對此板卡提供相容性保證。採用前需檢查 component 綁定的 ESPHome 版本、顯示腳位、GPIO7 電源流程與 partial-refresh 行為。

### MicroPython

**官方 firmware 基礎與社群實作**

MicroPython 官方有 `ESP32_GENERIC_S3` firmware。此板有 8 MB OPI PSRAM，若要使用 PSRAM，應評估下載頁提供的 `SPIRAM_OCT` 變體，並核對 Flash layout、octal PSRAM 啟用狀態和可用 heap。這只解決 ESP32-S3 firmware 基礎，不包含 CrowPanel 雙 SSD1683、接縫、按鍵、microSD 或 GPIO7 的板卡 driver。

[`omiq/crowpanel`](https://github.com/omiq/crowpanel) 等 community project 可供參考其 MicroPython 實作方式。這些專案的目標板、版本與 API 可能不同；套用前要逐項比對型號、解析度、controller、腳位與 framebuffer 格式。官方 repository 聲稱支援 MicroPython，但目前沒有可直接使用的板卡專用 MicroPython project／driver，不能把 community code 當成原廠保證。

## 圖片資料格式與轉換

**官方流程**

ELECROW tutorial 使用 Windows-only 的 Image2Lcd 將圖片轉為 C array。轉換時需選擇單色、正確寬高與掃描方向，再依消費端 API 決定輸出 792x272 邏輯圖片或 800x272 內部 framebuffer。Image2Lcd 產生的 array 仍需用測試圖確認資料排列，不能只看檔案長度判斷正確。

**實作注意事項**

1 bpp 圖片常見差異包括每 byte 的 bit order（MSB-first／LSB-first）、逐列或逐欄排列、黑白 polarity（0 是黑或白）、列尾 padding，以及雙 controller 接縫插入位置。官方 `EPD_ShowPicture()`、官方 `EPD_Display()` 與 GxEPD2 的 consumer contract 不同，同一份 byte array 未必能直接共用。

Portable workflow 日後可用 Pillow 建立：讀圖、縮放或裁切至 792x272、轉為 1-bit、依目標 API 打包 bytes，再用已知圖樣驗證 bit order 和 polarity。本文不加入未經實機驗證的轉換 script。建立工具時至少要輸出尺寸、byte count 和目標 consumer，並拒絕把 26,928-byte 邏輯圖誤標成 27,200-byte 官方 framebuffer。

## 已知問題

以下狀態來自官方 repository 與公開 issue；未有實機驗證的項目不延伸推定根因。

| 問題 | 狀態與影響 | 資料層級 |
| --- | --- | --- |
| Filled rectangle off-by-one | [Issue #1](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/issues/1) 指出 `EPD_DrawRectangle()` 畫 filled square 時高度少一個 pixel；issue 仍 open | 官方 repository 的公開 issue |
| Partial-refresh example 無畫面 | [Issue #2](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/issues/2) 回報 `5.79_partial_refresh` 不顯示內容；issue 仍 open | 官方 repository 的公開 issue，根因未確認 |
| `BUSY` 無保護 | 官方 driver 的 busy wait 缺少 timeout／yield；接線、供電或 controller 異常時可能永久卡住或觸發 watchdog | 官方 driver 實作檢查 |
| 4 MB／8 MB 不一致 | tutorial 截圖顯示 4 MB，但 N8 模組和硬體資料為 8 MB | 官方資料內部不一致 |
| Partial 不代表矩形傳輸 | 官方範例仍傳完整 800x272 framebuffer，沒有官方 rectangular transfer API | 官方 driver 實作檢查 |
| Image2Lcd 限制 | 官方圖片流程依賴 Windows-only 工具，不利於可重現的跨平台 build | 官方 tutorial 流程 |
| 維護度低 | 2026-08-19 檢查時只有兩筆實質 commit、沒有 release | Repository 狀態觀察 |
| SDK 不完整 | ESP-IDF／MicroPython 雖在官方說明中被列為可用，但 repository 沒有板卡專用 project／driver | 官方聲明和 repository 內容比對 |

目前的軟體 SDK 尚不成熟。若產品需要穩定更新、錯誤復原和長時間運作，應自行加入 `BUSY` timeout、狀態重建、完整測試圖與版本固定，並把官方範例當成 bring-up 參考，而不是已驗證的 production driver。

## 建議驗證順序

以下是**社群經驗／工程驗證建議**，不是原廠合格測試程序：

1. 核對板上型號、V1.0 標示、ESP32-S3 模組字樣、電池接頭極性與 USB 供電電壓。
2. 只接 USB，確認 CH340C serial port 出現，讀取 chip、Flash 與 PSRAM 資訊；先用 460800 upload，失敗再改 115200。
3. 編譯並燒錄最小 serial／GPIO 程式，確認 reset、BOOT 與 serial log。
4. 測試 active-low 按鍵、rocker、PWR LED 與未使用擴充 GPIO，不先接外部高負載。
5. 啟用 GPIO7，以 full refresh 顯示全白、全黑、棋盤格、外框及中央接縫；每次 `BUSY` 等待記錄 timeout 與實測時間。
6. 驗證 792x272 邏輯圖片和所用 API 的 byte count，不混用官方 800x272 framebuffer。
7. full refresh 成功後再測 partial refresh，確認 OLD／NEW RAM 同步、連續更新及 ghosting；從每 5 次 partial 插入一次 full 開始，再依結果放寬。
8. 測 microSD 的 GPIO42 電源控制、讀寫、flush 與移除失敗情境。
9. 分別測 Wi-Fi、BLE，再測它們和顯示更新、microSD 同時運作時的電源穩定性與記憶體餘量。
10. 確認 controller hibernate、GPIO7 低電位、ESP deep sleep 與喚醒後重新初始化；以電流表量測各階段，不引用未量測的整板 sleep current。
11. 最後才接受保護的 1S 電池。先量極性，再監測充電電流、溫度與截止行為；不要以約 500 mA 推論代替量測。

每項結果應記錄韌體 commit／版本、函式庫版本、供電方式、環境溫度與實測時間。沒有實機與儀器時，只能報告編譯或靜態檢查結果。

## 參考資料

### ELECROW 官方資料

- [商品頁：CrowPanel ESP32 5.79-inch E-Paper HMI](https://www.elecrow.com/crowpanel-esp32-5-79-e-paper-hmi-display-with-272-792-resolution-black-white-color-driven-by-spi-interface.html)
- [Wiki：CrowPanel ESP32 E-paper 5.79-inch HMI Display](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-paper_5.79-inch_HMI_Display.html)
- [Arduino Tutorial](https://www.elecrow.com/wiki/CrowPanel_ESP32_E-Paper_5.79inch_Arduino_Tutorial.html)
- [官方 GitHub repository](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792)
- [SSD1683 datasheet](https://www.elecrow.com/download/product/DIS08792E/SSD1683_Datasheet.PDF)
- [ESP32-S3-WROOM-1 datasheet](https://www.elecrow.com/download/product/DIS08792E/esp32-s3-wroom-1_datasheet.pdf)
- [Factory firmware folder](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/tree/master/factory_firmware)
- [Issue #1：filled rectangle off-by-one](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/issues/1)
- [Issue #2：partial-refresh example 無畫面](https://github.com/Elecrow-RD/CrowPanel-ESP32-5.79-E-paper-HMI-Display-with-272-792/issues/2)

### 函式庫實作

- [GxEPD2 1.5.9 `library.properties`](https://github.com/ZinggJM/GxEPD2/blob/1.5.9/library.properties)：固定版本的 release metadata，用於標示支援起始版本。
- [GxEPD2 1.6.9 `GxEPD2_579_GDEY0579T93.h`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey/GxEPD2_579_GDEY0579T93.h)：固定版本的 class 介面與 timing constants。
- [GxEPD2 1.6.9 `GxEPD2_579_GDEY0579T93.cpp`](https://github.com/ZinggJM/GxEPD2/blob/1.6.9/src/gdey/GxEPD2_579_GDEY0579T93.cpp)：固定版本的 RAM write、partial refresh 與雙 controller 實作。
- [GxEPD2 master header](https://github.com/ZinggJM/GxEPD2/blob/master/src/gdey/GxEPD2_579_GDEY0579T93.h)：latest mutable reference，內容可能隨後續 commit 改變，不作為本文版本化證據。

### 社群與替代環境

- [ESPBoards ESPHome external component](https://github.com/ESPBoards/esphome-lvgl-crowpanel-epaper-5.79-4.2)：community project，非 ELECROW 或 ESPHome upstream 保證。
- [MicroPython `ESP32_GENERIC_S3` downloads](https://micropython.org/download/ESP32_GENERIC_S3/)：官方通用 ESP32-S3 firmware，不含此板顯示 driver。
- [`omiq/crowpanel`](https://github.com/omiq/crowpanel)：community implementation，使用前需核對型號、腳位與版本。

## 實機 bring-up 量測（2026-08-24）

量測方法：PlatformIO + `main.cpp` serial 選單測試韌體（見
`docs/superpowers/specs/2026-08-24-bringup-verification-design.md`），
時間戳取自 `millis()`，USB-C 經 CH340C（`/dev/ttyUSB0`），上傳速度
460800 穩定。韌體以 GxEPD2 1.6.9、platform espressif32@7.0.1
（Arduino core ESP32 2.0.17）、`qio_opi` PSRAM 設定建置。

| 項目 | 結果 |
| --- | --- |
| 晶片／Flash／PSRAM | `ESP32-S3 rev=0 cores=2`；8192 KB Flash @ 80 MHz；PSRAM 偵測成功（回報 8386279 bytes，heap 可用大小） |
| init 呼叫耗時 | 22 ms（clean full refresh 發生於第一次更新） |
| clean full refresh | 4415 ms（兩顆 SSD1683 各 `_Update_Full 1741000 us`） |
| partial 單次耗時 | 664 ms，30 次全部一致（`_Update_Part 486001 us`） |
| 30 次 partial 批次總耗時 | 24388 ms（第 10、20 次後各插入一次全刷） |
| 按鍵（MENU/EXIT/UP/DOWN/PRESS） | 全部通過，pressed/released 無重複無漏報 |
| microSD 讀寫 | 通過：FAT32 卡寫入＋讀回比對正確，卸載後 GPIO42 關閉；拔卡時正確回報 init failed 且不影響選單 |
| Wi-Fi 掃描 | 找到 7 個網路（RSSI -83 至 -96），掃描後射頻正常關閉 |
| deep sleep → timer 喚醒 | 通過：10 秒 timer 喚醒，`rst:0x5 (DSLEEP)`，RTC memory `sleepCount=1` |

異常事件：
- 首次上傳遇到 `/dev/ttyUSB0` 權限問題，將使用者加入 `dialout` 群組後解決。
- 測試圖樣文字框初版高度不足導致第二行文字溢出，已加高修正（不影響硬體結論）。
- 觀察到 full refresh 實測 4415 ms 高於 GxEPD2 標頭標註的 nominal 2200 ms；
  推論與雙 controller 序列更新及 SPI 資料傳輸有關，留待後續應用設計納入考量。

## 天氣看板應用量測（2026-08-25）

量測方法：天氣看板韌體 serial log（見
`docs/superpowers/specs/2026-08-25-weather-station-design.md`），
時間戳取自 `millis()`，USB-C 經 CH340C。依賴：GxEPD2 1.6.9、
ArduinoJson 7.4.3、U8g2_for_Adafruit_GFX 1.8.0。

| 項目 | 結果 |
| --- | --- |
| Wi-Fi 連線 | 約 100–4800 ms（視訊號與快取狀態；RSSI -86 至 -87） |
| NTP 同步 | 約 1800–4700 ms；同次開機二次呼叫為 0 ms（已同步） |
| Open-Meteo GET | payload 約 1365–1394 bytes |
| Dashboard full refresh | 約 4400–4500 ms（兩顆 SSD1683 各 `_Update_Full` 約 1741000 us） |
| awake 提示條 partial refresh | 單次約 660 ms（`_Update_Part` 486001 us） |
| 30 分睡眠週期＋timer 喚醒 | 通過 |
| EXT1 撥桿下壓喚醒 → awake 模式 | 通過（rtc_gpio 內部拉高維持於睡眠期間） |
| NVS 地點記憶（跨 reset） | 通過 |
| 失敗路徑（錯誤密碼） | wifi timeout → OFFLINE 全刷 → 5 分短睡眠重試，通過 |

異常事件：
- GxEPD2 partial 視窗狀態會殘留：awake 提示條的 setPartialWindow
  未被 setFullWindow 重置前，dashboard 渲染會被塞進局部視窗。
  已修正並記錄於 commit 43e143c。
- 大字溫度採 logisoso62_tn（僅純數字字集）＋°C 以 helvR18 接續繪製。
