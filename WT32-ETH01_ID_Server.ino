/**************************************
1.Arduino版本：Arduino1.8.19
2.開發版：ESP32 Dev Module version_3.3.5
3.功能：預設AP模式，自動導入設定頁面
**************************************/

#include <ETH.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "SPIFFS.h"
#include "soc/soc.h"          //低電壓強制運行
#include "soc/rtc_cntl_reg.h" //低電壓強制運行

//================網路模式定義===================
enum NetworkMode
{
  MODE_AP_ONLY,     // 僅AP模式
  MODE_ETH_ONLY,    // 僅乙太網
  MODE_WIFI_ONLY,   // 僅WiFi STA模式
  MODE_BOTH,        // 雙網路
  MODE_ETH_PRIMARY, // 乙太網優先
  MODE_WIFI_PRIMARY // WiFi優先
};

//================全域變數宣告===================
static bool eth_connected = false;
static bool wifi_connected = false;
static bool ap_mode_active = true; // 預設啟動AP模式
static NetworkMode currentMode = MODE_AP_ONLY;
static IPAddress wifiIP;
static IPAddress ethIP;
static String ap_ssid = "";
static String ap_password = "";

WebServer webServer(80); // Web Server 使用 port 80

// 流量統計
struct TrafficStats
{
  unsigned long totalPackets;
  unsigned long totalBytes;
  unsigned long lastReset;
};

TrafficStats trafficStats = {0, 0, 0};

//================TCP伺服器相關宣告===================
#define MAX_SRV_CLIENTS 256              // 最大同時聯接數
WiFiServer tcpServer(2024);              // TCP伺服器 port 2024
WiFiClient client[MAX_SRV_CLIENTS];      // 定義客戶端名牌陣列
WiFiClient client_room[MAX_SRV_CLIENTS]; // 定義客戶端房間陣列
bool Server_Mode = 0;                    // mode0 = debug mode , mode1 = speed mode

//================ID伺服器相關宣告===================
byte ID_Name[] = {0x41, 0x69, 0x64, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x2D, 0x2D, 0x2B, 0x30, 0x30};

uint16_t ncfc = 0;                    // New_Client_Flag_Count
uint16_t Client_Number = 0;           // Client計數器
byte data[1460];                      // 接收資料緩衝區
int data_len;                         // 資料長度
uint8_t ID_Information_list[256][16]; // ID資料表

//================網路設定結構===================
struct NetworkConfig
{
  // AP模式設定
  bool ap_enabled;
  String ap_ssid;
  String ap_password;

  // WiFi設定
  String wifi_ssid;
  String wifi_password;
  bool wifi_enabled;

  // 乙太網設定
  bool eth_enabled;
  bool eth_dhcp;
  String eth_static_ip;
  String eth_gateway;
  String eth_subnet;
  String eth_dns1;
  String eth_dns2;

  // 網路模式
  NetworkMode network_mode;
  String device_name;

  // 首次設定標記
  bool first_setup;
};

NetworkConfig networkConfig;

//================其他宣告===================
unsigned long Light_Time = 0, time1 = 0;
const unsigned long Light_Time_Stamp = 1L * 1000L;
bool Light = HIGH;

//================函數宣告===================
void Working_Light();
void Server_monitor(uint8_t var, int clientIndex = -1); // 修改這裡，添加默認參數
void New_Client();
void Client_Recv_Data();
bool compareID(int clientIndex);    // 修改這裡，添加參數
void data_process(int clientIndex); // 修改這裡，添加參數
void PerDateService();

// Web Server 函數
void initWebServer();
void handleRoot();
void handleNetworkConfig();
void handleSaveConfig();
void handleGetStatus();
void handleSwitchNetwork();
void handleRestart();
void handleSetupComplete();
void handleNotFound();
void serveFile(String path, String contentType);
bool loadConfig();
bool saveConfig();
String readHTMLFile(String filename);
String getModeString(NetworkMode mode);
String generateAPSSID();
String getMACLast2Chars();

// 網路管理函數
void initNetworks();
void startAPMode();
void stopAPMode();
void startWiFi();
void stopWiFi();
void startEthernet();
void stopEthernet();
void switchNetworkMode(NetworkMode newMode);
String getConnectionStatus();
String getActiveIP();

void handleScanWiFi();
void handleConnectWiFi();
void handleWiFiStatus();

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 乙太網事件處理
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case ARDUINO_EVENT_ETH_START:
    Serial.println("ETH Started");
    ETH.setHostname("esp32-ethernet");
    break;
  case ARDUINO_EVENT_ETH_CONNECTED:
    Serial.println("ETH Connected");
    break;
  case ARDUINO_EVENT_ETH_GOT_IP:
    Serial.print("ETH MAC: ");
    Serial.print(ETH.macAddress());
    Serial.print(", IPv4: ");
    ethIP = ETH.localIP();
    Serial.print(ethIP);
    if (ETH.fullDuplex())
    {
      Serial.print(", FULL_DUPLEX");
    }
    Serial.print(", ");
    Serial.print(ETH.linkSpeed());
    Serial.println("Mbps");
    eth_connected = true;
    break;
  case ARDUINO_EVENT_ETH_DISCONNECTED:
    Serial.println("ETH Disconnected");
    eth_connected = false;
    break;
  case ARDUINO_EVENT_ETH_STOP:
    Serial.println("ETH Stopped");
    eth_connected = false;
    break;

  case ARDUINO_EVENT_WIFI_STA_CONNECTED:
    Serial.println("WiFi STA Connected");
    break;
  case ARDUINO_EVENT_WIFI_STA_GOT_IP:
    wifiIP = WiFi.localIP();
    Serial.print("WiFi STA IPv4: ");
    Serial.println(wifiIP);
    wifi_connected = true;
    break;
  case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
    Serial.println("WiFi STA Disconnected");
    wifi_connected = false;
    break;

  case ARDUINO_EVENT_WIFI_AP_START:
    Serial.println("WiFi AP Started");
    break;
  case ARDUINO_EVENT_WIFI_AP_STOP:
    Serial.println("WiFi AP Stopped");
    break;

  default:
    break;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 網路管理函數
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//============================================獲取MAC最後2個字元============================================
String getMACLast2Chars()
{
  String mac = WiFi.macAddress();
  Serial.print("原始MAC地址: ");
  Serial.println(mac);

  // 移除冒號
  mac.replace(":", "");
  Serial.print("移除冒號後MAC: ");
  Serial.println(mac);

  // 取最後2個字元
  if (mac.length() >= 2)
  {
    String last2 = mac.substring(mac.length() - 2);
    last2.toUpperCase(); // 轉大寫
    Serial.print("最後2個字元: ");
    Serial.println(last2);
    return last2;
  }

  return "FF"; // 預設值
}

//============================================生成AP SSID============================================
String generateAPSSID()
{
  String last2 = getMACLast2Chars();
  return "ESP32_" + last2;
}

//============================================初始化網路============================================
void initNetworks()
{
  // 設定事件處理器
  WiFi.onEvent(WiFiEvent);

  // 檢查是否為首次設定
  if (networkConfig.first_setup)
  {
    Serial.println("首次設定模式：啟動AP模式");
    startAPMode();
    return;
  }

  // 根據設定啟動網路
  switch (networkConfig.network_mode)
  {
  case MODE_AP_ONLY:
    startAPMode();
    break;
  case MODE_ETH_ONLY:
    startEthernet();
    break;
  case MODE_WIFI_ONLY:
    startWiFi();
    break;
  case MODE_BOTH:
    startEthernet();
    startWiFi();
    break;
  case MODE_ETH_PRIMARY:
    startEthernet();
    if (!eth_connected)
    {
      delay(1000);
      startWiFi();
    }
    break;
  case MODE_WIFI_PRIMARY:
    startWiFi();
    if (!wifi_connected)
    {
      delay(1000);
      startEthernet();
    }
    break;
  }
}

//============================================啟動AP模式============================================
void startAPMode()
{
  Serial.println("啟動AP模式...");

  // 生成AP SSID
  if (networkConfig.ap_ssid.length() == 0)
  {
    networkConfig.ap_ssid = generateAPSSID();
  }

  ap_ssid = networkConfig.ap_ssid;
  ap_password = networkConfig.ap_password;

  // 啟動AP
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());

  // 設定AP IP
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  Serial.print("AP SSID: ");
  Serial.println(ap_ssid);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.print("AP MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  ap_mode_active = true;
}

//============================================停止AP模式============================================
void stopAPMode()
{
  WiFi.softAPdisconnect(true);
  ap_mode_active = false;
  Serial.println("AP模式已停止");
}

//============================================啟動WiFi STA============================================
void startWiFi()
{
  if (networkConfig.wifi_enabled && networkConfig.wifi_ssid.length() > 0)
  {
    Serial.println("啟動WiFi STA模式...");

    // 如果AP模式正在運行，先停止
    if (ap_mode_active)
    {
      stopAPMode();
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin(networkConfig.wifi_ssid.c_str(), networkConfig.wifi_password.c_str());

    // 等待連接，最多15秒
    unsigned long startTime = millis();
    Serial.print("連接WiFi: ");
    Serial.print(networkConfig.wifi_ssid);

    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000)
    {
      delay(500);
      Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      wifi_connected = true;
      wifiIP = WiFi.localIP();
      Serial.println("\nWiFi連接成功!");
      Serial.print("IP地址: ");
      Serial.println(wifiIP);
    }
    else
    {
      Serial.println("\nWiFi連接失敗");
      // 連接失敗，回到AP模式
      Serial.println("返回AP模式...");
      startAPMode();
    }
  }
  else
  {
    Serial.println("WiFi未啟用或未設定，啟動AP模式");
    startAPMode();
  }
}

//============================================停止WiFi============================================
void stopWiFi()
{
  WiFi.disconnect(true);
  wifi_connected = false;
  Serial.println("WiFi已停止");
}

//============================================啟動乙太網============================================
void startEthernet()
{
  if (networkConfig.eth_enabled)
  {
    Serial.println("啟動乙太網...");
    ETH.begin();

    // 等待乙太網連接
    delay(1000);
  }
}

//============================================停止乙太網============================================
void stopEthernet()
{
  eth_connected = false;
  Serial.println("乙太網已停止");
}

//============================================切換網路模式============================================
void switchNetworkMode(NetworkMode newMode)
{
  if (currentMode == newMode)
    return;

  Serial.print("切換網路模式: ");
  Serial.print(getModeString(currentMode));
  Serial.print(" -> ");
  Serial.println(getModeString(newMode));

  // 停止當前網路
  stopAPMode();
  stopWiFi();
  stopEthernet();

  // 更新模式
  currentMode = newMode;
  networkConfig.network_mode = newMode;

  // 啟動新網路模式
  switch (newMode)
  {
  case MODE_AP_ONLY:
    startAPMode();
    break;
  case MODE_ETH_ONLY:
    startEthernet();
    break;
  case MODE_WIFI_ONLY:
    startWiFi();
    break;
  case MODE_BOTH:
    startEthernet();
    startWiFi();
    break;
  case MODE_ETH_PRIMARY:
    startEthernet();
    if (!eth_connected)
    {
      delay(1000);
      startWiFi();
    }
    break;
  case MODE_WIFI_PRIMARY:
    startWiFi();
    if (!wifi_connected)
    {
      delay(1000);
      startEthernet();
    }
    break;
  }

  // 儲存設定
  saveConfig();
}

//============================================獲取模式字串============================================
String getModeString(NetworkMode mode)
{
  switch (mode)
  {
  case MODE_AP_ONLY:
    return "AP模式";
  case MODE_ETH_ONLY:
    return "僅乙太網";
  case MODE_WIFI_ONLY:
    return "僅WiFi";
  case MODE_BOTH:
    return "雙網路";
  case MODE_ETH_PRIMARY:
    return "乙太網優先";
  case MODE_WIFI_PRIMARY:
    return "WiFi優先";
  default:
    return "未知";
  }
}

//============================================獲取連接狀態============================================
String getConnectionStatus()
{
  String status = "";

  if (ap_mode_active)
  {
    status += "📡 AP模式運行中<br>";
    status += "SSID: " + ap_ssid + "<br>";
    status += "IP: 192.168.4.1<br>";
  }

  if (eth_connected)
  {
    status += "✅ 乙太網已連接 ";
    status += ethIP.toString();
    status += "<br>";
  }

  if (wifi_connected)
  {
    status += "✅ WiFi已連接 ";
    status += wifiIP.toString();
    status += "<br>";
  }

  status += "⚙️ 網路模式: ";
  status += getModeString(currentMode);

  return status;
}

//============================================獲取活動IP============================================
String getActiveIP()
{
  if (ap_mode_active)
  {
    return "192.168.4.1";
  }

  switch (currentMode)
  {
  case MODE_ETH_ONLY:
    return eth_connected ? ethIP.toString() : "未連接";
  case MODE_WIFI_ONLY:
    return wifi_connected ? wifiIP.toString() : "未連接";
  case MODE_ETH_PRIMARY:
    return eth_connected ? ethIP.toString() : (wifi_connected ? wifiIP.toString() : "未連接");
  case MODE_WIFI_PRIMARY:
    return wifi_connected ? wifiIP.toString() : (eth_connected ? ethIP.toString() : "未連接");
  case MODE_BOTH:
    if (eth_connected && wifi_connected)
    {
      return ethIP.toString() + " (ETH) / " + wifiIP.toString() + " (WiFi)";
    }
    else if (eth_connected)
    {
      return ethIP.toString() + " (ETH)";
    }
    else if (wifi_connected)
    {
      return wifiIP.toString() + " (WiFi)";
    }
    else
    {
      return "未連接";
    }
  default:
    return "192.168.4.1";
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Web Server 函數
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//============================================讀取HTML檔案============================================
String readHTMLFile(String filename)
{
  String html = "";

  if (!filename.startsWith("/"))
  {
    filename = "/" + filename;
  }

  File file = SPIFFS.open(filename, "r");
  if (!file)
  {
    Serial.println("無法開啟檔案: " + filename);
    return "<html><body><h1>檔案不存在: " + filename + "</h1></body></html>";
  }

  while (file.available())
  {
    html += char(file.read());
  }

  file.close();
  return html;
}

//============================================Web Server 初始化============================================
void initWebServer()
{
  // 初始化 SPIFFS
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // 設定路由
  webServer.on("/", HTTP_GET, handleRoot);
  webServer.on("/network-config", HTTP_GET, handleNetworkConfig);
  webServer.on("/save-config", HTTP_POST, handleSaveConfig);
  webServer.on("/setup-complete", HTTP_POST, handleSetupComplete);
  webServer.on("/status", HTTP_GET, handleGetStatus);
  webServer.on("/switch-network", HTTP_POST, handleSwitchNetwork);
  webServer.on("/restart", HTTP_POST, handleRestart);
  // 新增網路狀態路由
  webServer.on("/network-status", HTTP_GET, handleNetworkStatus);

  // 新增WiFi相關路由
  webServer.on("/scan-wifi", HTTP_GET, handleScanWiFi);
  webServer.on("/connect-wifi", HTTP_POST, handleConnectWiFi);
  webServer.on("/wifi-status", HTTP_GET, handleWiFiStatus);

  webServer.onNotFound(handleNotFound);

  webServer.begin();
  Serial.println("Web Server started on port 80");
}

//============================================處理根目錄============================================
void handleRoot()
{
  // 如果是首次設定，重定向到設定頁面
  if (networkConfig.first_setup)
  {
    webServer.sendHeader("Location", "/network-config");
    webServer.send(302, "text/plain", "Redirecting to setup");
    return;
  }

  // 如果是AP模式，顯示歡迎頁面
  if (ap_mode_active)
  {
    String html = readHTMLFile("welcome.html");
    if (html.length() > 0)
    {
      // 替換變數
      html.replace("{{ap_ssid}}", ap_ssid);
      html.replace("{{device_name}}", networkConfig.device_name);
      webServer.send(200, "text/html", html);
    }
    else
    {
      // 如果沒有welcome.html，顯示簡易歡迎頁面
      String simpleHTML = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 設定頁面</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { 
            font-family: Arial, sans-serif; 
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            justify-content: center;
            align-items: center;
            padding: 20px;
        }
        .container {
            background: white;
            padding: 40px;
            border-radius: 15px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.1);
            text-align: center;
            max-width: 500px;
            width: 100%;
        }
        h1 { color: #333; margin-bottom: 20px; }
        p { color: #666; margin-bottom: 30px; line-height: 1.6; }
        .btn {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 15px 30px;
            border: none;
            border-radius: 8px;
            font-size: 1.1em;
            cursor: pointer;
            text-decoration: none;
            display: inline-block;
            margin: 10px;
        }
        .btn:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(102, 126, 234, 0.3); }
        .info { background: #f8f9fa; padding: 15px; border-radius: 8px; margin: 20px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎯 ESP32 設定頁面</h1>
        <div class="info">
            <p>AP SSID: <strong>)rawliteral" +
                          ap_ssid + R"rawliteral(</strong></p>
            <p>IP地址: <strong>192.168.4.1</strong></p>
        </div>
        <p>歡迎使用ESP32網路伺服器！請點擊下方按鈕開始設定網路參數。</p>
        <a href="/network-config" class="btn">🔧 開始設定</a>
        <a href="/status" class="btn">📊 系統狀態</a>
    </div>
</body>
</html>
)rawliteral";
      webServer.send(200, "text/html", simpleHTML);
    }
  }
  else
  {
    // 正常模式，顯示主控制台
    String html = readHTMLFile("index.html");
    if (html.length() > 0)
    {
      webServer.send(200, "text/html", html);
    }
    else
    {
      webServer.send(500, "text/plain", "無法載入 index.html");
    }
  }
}

//============================================處理網路設定頁面============================================
// 修改handleNetworkConfig函數，不要進行模板替換
void handleNetworkConfig()
{
  String html = readHTMLFile("network-config.html");

  // 不再進行模板替換，由前端JavaScript處理
  // 直接發送HTML檔案
  if (html.length() > 0)
  {
    webServer.send(200, "text/html", html);
  }
  else
  {
    webServer.send(500, "text/plain", "無法載入 network-config.html");
  }
}

//============================================完成首次設定============================================
void handleSetupComplete()
{
  if (webServer.hasArg("complete") && webServer.arg("complete") == "true")
  {
    networkConfig.first_setup = false;
    saveConfig();

    StaticJsonDocument<200> responseDoc;
    responseDoc["success"] = true;
    responseDoc["message"] = "設定完成，系統將重新啟動";

    String response;
    serializeJson(responseDoc, response);
    webServer.send(200, "application/json", response);

    delay(1000);
    ESP.restart();
  }
  else
  {
    webServer.send(400, "text/plain", "無效的請求");
  }
}

//============================================儲存設定============================================
void handleSaveConfig()
{
  Serial.println("收到儲存設定請求");

  if (webServer.hasArg("plain"))
  {
    String jsonString = webServer.arg("plain");
    Serial.print("收到JSON資料: ");
    Serial.println(jsonString);

    // 解析JSON
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, jsonString);

    if (!error)
    {
      Serial.println("JSON解析成功");

      // 儲存設定值
      networkConfig.device_name = doc["device_name"].as<String>();
      networkConfig.ap_ssid = doc["ap_ssid"].as<String>();
      networkConfig.ap_password = doc["ap_password"].as<String>();
      networkConfig.wifi_ssid = doc["wifi_ssid"].as<String>();
      networkConfig.wifi_password = doc["wifi_password"].as<String>();
      networkConfig.wifi_enabled = doc["wifi_enabled"];
      networkConfig.eth_enabled = doc["eth_enabled"];
      networkConfig.eth_dhcp = doc["eth_dhcp"];
      networkConfig.eth_static_ip = doc["eth_static_ip"].as<String>();
      networkConfig.eth_gateway = doc["eth_gateway"].as<String>();
      networkConfig.eth_subnet = doc["eth_subnet"].as<String>();
      networkConfig.eth_dns1 = doc["eth_dns1"].as<String>();
      networkConfig.eth_dns2 = doc["eth_dns2"].as<String>();

      int modeValue = doc["network_mode"];
      if (modeValue >= 0 && modeValue <= 5)
      {
        networkConfig.network_mode = (NetworkMode)modeValue;
      }

      // 儲存到SPIFFS
      if (saveConfig())
      {
        Serial.println("設定儲存成功");

        // 如果是首次設定，標記為已完成
        if (networkConfig.first_setup)
        {
          networkConfig.first_setup = false;
          saveConfig();
        }

        // 重新啟動網路
        switchNetworkMode(networkConfig.network_mode);

        StaticJsonDocument<200> responseDoc;
        responseDoc["success"] = true;
        responseDoc["message"] = "設定已儲存並套用";
        responseDoc["first_setup"] = networkConfig.first_setup;

        String response;
        serializeJson(responseDoc, response);
        webServer.send(200, "application/json", response);

        Serial.println("網路設定已更新");
      }
      else
      {
        Serial.println("設定儲存失敗");
        StaticJsonDocument<200> responseDoc;
        responseDoc["success"] = false;
        responseDoc["message"] = "儲存失敗";

        String response;
        serializeJson(responseDoc, response);
        webServer.send(500, "application/json", response);
      }
    }
    else
    {
      Serial.print("JSON解析錯誤: ");
      Serial.println(error.c_str());
      StaticJsonDocument<200> responseDoc;
      responseDoc["success"] = false;
      responseDoc["message"] = String("JSON解析錯誤: ") + error.c_str();

      String response;
      serializeJson(responseDoc, response);
      webServer.send(400, "application/json", response);
    }
  }
  else
  {
    Serial.println("無請求資料");
    webServer.send(400, "text/plain", "無請求資料");
  }
}

//============================================獲取狀態============================================
void handleGetStatus()
{
  StaticJsonDocument<512> doc;

  // 計算連線中的客戶端數量
  int connectedClients = 0;
  for (int i = 0; i < MAX_SRV_CLIENTS; i++)
  {
    if (client[i] && client[i].connected())
    {
      connectedClients++;
    }
  }

  doc["ap_active"] = ap_mode_active;
  doc["ap_ssid"] = ap_ssid;
  doc["ap_ip"] = "192.168.4.1";

  doc["eth_connected"] = eth_connected;
  doc["eth_ip"] = ethIP.toString();
  doc["eth_mac"] = ETH.macAddress();
  doc["eth_speed"] = ETH.linkSpeed();
  doc["eth_full_duplex"] = ETH.fullDuplex();

  doc["wifi_connected"] = wifi_connected;
  doc["wifi_ip"] = wifiIP.toString();
  doc["wifi_mac"] = WiFi.macAddress();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["wifi_ssid"] = WiFi.SSID(); // 新增：當前連接的WiFi SSID

  doc["tcp_port"] = 2024;
  doc["client_count"] = connectedClients;
  doc["active_ip"] = getActiveIP();
  doc["network_mode"] = getModeString(currentMode);
  doc["connection_status"] = getConnectionStatus();
  doc["first_setup"] = networkConfig.first_setup;

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

//============================================切換網路============================================
void handleSwitchNetwork()
{
  if (webServer.hasArg("mode"))
  {
    int mode = webServer.arg("mode").toInt();
    if (mode >= 0 && mode <= 5)
    {
      switchNetworkMode((NetworkMode)mode);

      StaticJsonDocument<200> responseDoc;
      responseDoc["success"] = true;
      responseDoc["message"] = "網路模式已切換";

      String response;
      serializeJson(responseDoc, response);
      webServer.send(200, "application/json", response);
    }
    else
    {
      webServer.send(400, "text/plain", "無效的模式");
    }
  }
  else
  {
    webServer.send(400, "text/plain", "缺少模式參數");
  }
}

//============================================重啟系統============================================
void handleRestart()
{
  StaticJsonDocument<200> responseDoc;
  responseDoc["success"] = true;
  responseDoc["message"] = "系統將重新啟動";

  String response;
  serializeJson(responseDoc, response);
  webServer.send(200, "application/json", response);

  delay(1000);
  ESP.restart();
}

//============================================處理找不到的頁面============================================
void handleNotFound()
{
  // 在AP模式下，重定向所有請求到主頁
  if (ap_mode_active)
  {
    webServer.sendHeader("Location", "/");
    webServer.send(302, "text/plain", "Redirect to home");
    return;
  }

  String path = webServer.uri();

  if (path.endsWith("/"))
    path += "index.html";

  if (SPIFFS.exists(path))
  {
    String contentType = "text/plain";
    if (path.endsWith(".html"))
      contentType = "text/html";
    else if (path.endsWith(".css"))
      contentType = "text/css";
    else if (path.endsWith(".js"))
      contentType = "application/javascript";
    else if (path.endsWith(".png"))
      contentType = "image/png";
    else if (path.endsWith(".jpg"))
      contentType = "image/jpeg";
    else if (path.endsWith(".json"))
      contentType = "application/json";

    serveFile(path, contentType);
  }
  else
  {
    String message = "檔案未找到\n\n";
    message += "路徑: ";
    message += webServer.uri();
    message += "\n方法: ";
    message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
    message += "\n參數: ";
    message += webServer.args();
    message += "\n";

    for (uint8_t i = 0; i < webServer.args(); i++)
    {
      message += " ";
      message += webServer.argName(i);
      message += ": ";
      message += webServer.arg(i);
      message += "\n";
    }

    webServer.send(404, "text/plain", message);
  }
}

//============================================提供靜態檔案============================================
void serveFile(String path, String contentType)
{
  File file = SPIFFS.open(path, "r");
  if (!file)
  {
    webServer.send(500, "text/plain", "無法開啟檔案");
    return;
  }

  webServer.sendHeader("Cache-Control", "max-age=3600");
  webServer.streamFile(file, contentType);
  file.close();
}

//============================================載入設定檔============================================
bool loadConfig()
{
  if (!SPIFFS.exists("/config.json"))
  {
    Serial.println("設定檔不存在，使用預設值（首次設定）");

    // 生成AP SSID（使用MAC最後2碼）
    String defaultAPSSID = generateAPSSID();

    // 設定預設值（首次設定模式）
    networkConfig.device_name = "ESP32-Network-Server";
    networkConfig.ap_ssid = defaultAPSSID;
    networkConfig.ap_password = ""; // 無密碼
    networkConfig.wifi_ssid = "";
    networkConfig.wifi_password = "";
    networkConfig.wifi_enabled = true;
    networkConfig.eth_enabled = true;
    networkConfig.eth_dhcp = true;
    networkConfig.eth_static_ip = "192.168.1.100";
    networkConfig.eth_gateway = "192.168.1.1";
    networkConfig.eth_subnet = "255.255.255.0";
    networkConfig.eth_dns1 = "8.8.8.8";
    networkConfig.eth_dns2 = "8.8.4.4";
    networkConfig.network_mode = MODE_AP_ONLY; // 預設AP模式
    networkConfig.first_setup = true;          // 標記為首次設定

    return saveConfig();
  }

  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile)
  {
    Serial.println("無法開啟設定檔");
    return false;
  }

  size_t size = configFile.size();
  if (size == 0)
  {
    Serial.println("設定檔為空");
    configFile.close();
    return false;
  }

  std::unique_ptr<char[]> buf(new char[size]);
  configFile.readBytes(buf.get(), size);

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, buf.get());

  if (error)
  {
    Serial.println("設定檔解析失敗");
    configFile.close();
    return false;
  }

  // 讀取設定值
  networkConfig.device_name = doc["device_name"].as<String>();
  networkConfig.ap_ssid = doc["ap_ssid"].as<String>();
  networkConfig.ap_password = doc["ap_password"].as<String>();
  networkConfig.wifi_ssid = doc["wifi_ssid"].as<String>();
  networkConfig.wifi_password = doc["wifi_password"].as<String>();
  networkConfig.wifi_enabled = doc["wifi_enabled"];
  networkConfig.eth_enabled = doc["eth_enabled"];
  networkConfig.eth_dhcp = doc["eth_dhcp"];
  networkConfig.eth_static_ip = doc["eth_static_ip"].as<String>();
  networkConfig.eth_gateway = doc["eth_gateway"].as<String>();
  networkConfig.eth_subnet = doc["eth_subnet"].as<String>();
  networkConfig.eth_dns1 = doc["eth_dns1"].as<String>();
  networkConfig.eth_dns2 = doc["eth_dns2"].as<String>();
  networkConfig.first_setup = doc["first_setup"];

  int modeValue = doc["network_mode"];
  if (modeValue >= 0 && modeValue <= 5)
  {
    networkConfig.network_mode = (NetworkMode)modeValue;
  }

  configFile.close();
  Serial.println("設定檔載入成功");
  return true;
}

//============================================儲存設定檔============================================
bool saveConfig()
{
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile)
  {
    Serial.println("無法建立設定檔");
    return false;
  }

  StaticJsonDocument<1024> doc;

  doc["device_name"] = networkConfig.device_name;
  doc["ap_ssid"] = networkConfig.ap_ssid;
  doc["ap_password"] = networkConfig.ap_password;
  doc["wifi_ssid"] = networkConfig.wifi_ssid;
  doc["wifi_password"] = networkConfig.wifi_password;
  doc["wifi_enabled"] = networkConfig.wifi_enabled;
  doc["eth_enabled"] = networkConfig.eth_enabled;
  doc["eth_dhcp"] = networkConfig.eth_dhcp;
  doc["eth_static_ip"] = networkConfig.eth_static_ip;
  doc["eth_gateway"] = networkConfig.eth_gateway;
  doc["eth_subnet"] = networkConfig.eth_subnet;
  doc["eth_dns1"] = networkConfig.eth_dns1;
  doc["eth_dns2"] = networkConfig.eth_dns2;
  doc["network_mode"] = (int)networkConfig.network_mode;
  doc["first_setup"] = networkConfig.first_setup;

  if (serializeJson(doc, configFile) == 0)
  {
    Serial.println("寫入設定檔失敗");
    configFile.close();
    return false;
  }

  configFile.close();
  Serial.println("設定檔已儲存");
  return true;
}

//================新增：掃描WiFi函數===================
void handleScanWiFi()
{
  Serial.println("開始掃描WiFi...");

  // 掃描WiFi網路
  int n = WiFi.scanNetworks();
  Serial.print("找到 ");
  Serial.print(n);
  Serial.println(" 個WiFi網路");

  // 建立JSON響應
  StaticJsonDocument<4096> doc; // 增加大小以容納更多WiFi資訊
  JsonArray networks = doc.createNestedArray("networks");

  for (int i = 0; i < n; i++)
  {
    JsonObject network = networks.createNestedObject();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["channel"] = WiFi.channel(i);
    network["encryption"] = WiFi.encryptionType(i);

    // 將加密類型轉換為可讀字串
    String encryptionStr;
    switch (WiFi.encryptionType(i))
    {
    case WIFI_AUTH_OPEN:
      encryptionStr = "開放";
      break;
    case WIFI_AUTH_WEP:
      encryptionStr = "WEP";
      break;
    case WIFI_AUTH_WPA_PSK:
      encryptionStr = "WPA-PSK";
      break;
    case WIFI_AUTH_WPA2_PSK:
      encryptionStr = "WPA2-PSK";
      break;
    case WIFI_AUTH_WPA_WPA2_PSK:
      encryptionStr = "WPA/WPA2-PSK";
      break;
    case WIFI_AUTH_WPA2_ENTERPRISE:
      encryptionStr = "WPA2-Enterprise";
      break;
    default:
      encryptionStr = "未知";
    }
    network["encryption_str"] = encryptionStr;

    // 計算訊號強度百分比
    int rssi = WiFi.RSSI(i);
    int quality = 0;
    if (rssi <= -100)
    {
      quality = 0;
    }
    else if (rssi >= -50)
    {
      quality = 100;
    }
    else
    {
      quality = 2 * (rssi + 100);
    }
    network["quality"] = quality;

    Serial.printf("SSID: %s, RSSI: %d, Channel: %d, Encryption: %s\n",
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i), encryptionStr.c_str());
  }

  // 如果沒有找到網路
  if (n == 0)
  {
    doc["message"] = "未找到任何WiFi網路";
  }

  doc["count"] = n;

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);

  // 清理掃描結果
  WiFi.scanDelete();
}

//================新增：連接WiFi函數===================
void handleConnectWiFi()
{
  if (webServer.hasArg("plain"))
  {
    String jsonString = webServer.arg("plain");
    Serial.print("收到WiFi連接請求: ");
    Serial.println(jsonString);

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, jsonString);

    if (!error)
    {
      String ssid = doc["ssid"].as<String>();
      String password = doc["password"].as<String>();

      Serial.print("嘗試連接WiFi: ");
      Serial.print(ssid);
      Serial.print(", 密碼長度: ");
      Serial.println(password.length());

      // 儲存到設定
      networkConfig.wifi_ssid = ssid;
      networkConfig.wifi_password = password;
      networkConfig.wifi_enabled = true;

      // 嘗試連接WiFi
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid.c_str(), password.c_str());

      // 等待連接，最多15秒
      unsigned long startTime = millis();
      bool connected = false;
      String ipAddress = "";

      while (millis() - startTime < 15000)
      {
        delay(500);
        Serial.print(".");

        if (WiFi.status() == WL_CONNECTED)
        {
          connected = true;
          wifi_connected = true;
          wifiIP = WiFi.localIP();
          ipAddress = wifiIP.toString();

          Serial.println("\nWiFi連接成功!");
          Serial.print("IP地址: ");
          Serial.println(ipAddress);

          // 如果AP模式正在運行，停止它
          if (ap_mode_active)
          {
            stopAPMode();
          }

          // 儲存設定
          saveConfig();
          break;
        }
      }

      // 建立JSON回應
      StaticJsonDocument<300> responseDoc;

      if (connected)
      {
        responseDoc["success"] = true;
        responseDoc["message"] = "WiFi連接成功";
        responseDoc["ssid"] = ssid;
        responseDoc["ip"] = ipAddress;
        responseDoc["rssi"] = WiFi.RSSI();
        responseDoc["status"] = "connected";
      }
      else
      {
        responseDoc["success"] = false;
        responseDoc["message"] = "WiFi連接失敗";
        responseDoc["status"] = "failed";

        // 連接失敗，回到AP模式
        if (!ap_mode_active)
        {
          startAPMode();
        }
      }

      String response;
      serializeJson(responseDoc, response);
      webServer.send(200, "application/json", response);

      if (!connected)
      {
        Serial.println("\nWiFi連接失敗");
      }
    }
    else
    {
      webServer.send(400, "text/plain", "JSON解析錯誤");
    }
  }
  else
  {
    webServer.send(400, "text/plain", "無請求資料");
  }
}

//================新增：WiFi狀態檢查函數===================
void handleWiFiStatus()
{
  StaticJsonDocument<200> doc;

  int status = WiFi.status();
  doc["status"] = status;

  switch (status)
  {
  case WL_IDLE_STATUS:
    doc["status_str"] = "閒置";
    break;
  case WL_NO_SSID_AVAIL:
    doc["status_str"] = "SSID不可用";
    break;
  case WL_SCAN_COMPLETED:
    doc["status_str"] = "掃描完成";
    break;
  case WL_CONNECTED:
    doc["status_str"] = "已連接";
    doc["ip"] = WiFi.localIP().toString();
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    break;
  case WL_CONNECT_FAILED:
    doc["status_str"] = "連接失敗";
    break;
  case WL_CONNECTION_LOST:
    doc["status_str"] = "連接丟失";
    break;
  case WL_DISCONNECTED:
    doc["status_str"] = "未連接";
    break;
  default:
    doc["status_str"] = "未知狀態";
  }

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

//================新增：取得網路狀態===================
void handleNetworkStatus()
{
  StaticJsonDocument<512> doc;

  doc["wifi_connected"] = wifi_connected;
  if (wifi_connected)
  {
    doc["wifi_ip"] = wifiIP.toString();
    doc["wifi_ssid"] = WiFi.SSID();
    doc["wifi_rssi"] = WiFi.RSSI();
  }

  doc["eth_connected"] = eth_connected;
  if (eth_connected)
  {
    doc["eth_ip"] = ethIP.toString();
  }

  doc["ap_active"] = ap_mode_active;
  if (ap_mode_active)
  {
    doc["ap_ssid"] = ap_ssid;
    doc["ap_ip"] = "192.168.4.1";
  }

  doc["network_mode"] = getModeString(currentMode);
  doc["active_ip"] = getActiveIP();

  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// TCP伺服器函數
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

//============================================運作信號燈============================================
void Working_Light()
{
  if (millis() - Light_Time > Light_Time_Stamp)
  {
    Light = Light == HIGH ? LOW : HIGH;
    digitalWrite(2, Light);
    Light_Time = millis();
  }
}

//============================================伺服器資訊監視============================================
void Server_monitor(uint8_t var, int clientIndex) // 修改這裡
{
  if (Server_Mode == 0)
  {
    switch (var)
    {
    case 0:
      Serial.println("");
      Serial.print("New_connent:");
      if (clientIndex >= 0 && clientIndex < MAX_SRV_CLIENTS && client[clientIndex])
      {
        Serial.print(client[clientIndex].remoteIP());
        Serial.print(":");
        Serial.println(client[clientIndex].remotePort());
      }
      Serial.println("");
      break;
    case 1:
      Serial.println("");
      Serial.println("#################################################################");
      if (clientIndex >= 0 && clientIndex < MAX_SRV_CLIENTS && client[clientIndex])
      {
        Serial.print(client[clientIndex].remoteIP());
        Serial.print(":");
        Serial.println(client[clientIndex].remotePort());
      }
      Serial.println("ID error: not the object of the service");
      for (int i = 0; i < 64; i++)
      {
        Serial.printf("%2X ,", data[i]);
        if (i == 15 || i == 31 || i == 47 || i == 63)
          Serial.println("");
      }
      Serial.println("#################################################################");
      Serial.println("");
      break;
    case 2:
      Serial.println("");
      Serial.printf("DATA-Size: %d \n", data_len);
      Serial.println("#################################################################");
      Serial.printf("Source ID: %2X \n", data[30]);
      if (data[30] < MAX_SRV_CLIENTS && client_room[data[30]])
      {
        Serial.print(client_room[data[30]].remoteIP());
        Serial.print(":");
        Serial.println(client_room[data[30]].remotePort());
      }
      Serial.println("   To");
      Serial.printf("Destination ID: %2X \n", data[62]);
      if (data[62] < MAX_SRV_CLIENTS && client_room[data[62]])
      {
        Serial.print(client_room[data[62]].remoteIP());
        Serial.print(":");
        Serial.println(client_room[data[62]].remotePort());
      }
      for (int i = 64; i < 75; i++)
      {
        Serial.printf("%2X ,", data[i]);
        if (i == 74)
          Serial.println("");
      }
      Serial.println("#################################################################");
      Serial.println("");
      break;
    default:
      Serial.println("Server_monitor Var Error");
      break;
    }
  }
}

//============================================新的客戶端連線處理============================================
void New_Client()
{
  if (tcpServer.hasClient())
  {
    if (!client[ncfc] || !client[ncfc].connected())
    {
      client[ncfc].stop();
      client[ncfc] = tcpServer.available();
      Server_monitor(0, ncfc); // 傳遞客戶端索引
    }
    if (ncfc < MAX_SRV_CLIENTS - 1)
      ncfc++;
    else
      ncfc = 0;
  }
}

//============================================收客戶端的資料============================================
void Client_Recv_Data()
{
  for (int i = 0; i < MAX_SRV_CLIENTS; i++) // 使用局部變數i
  {
    yield();
    if (!client[i] || !client[i].connected())
      continue;

    if (client[i].available())
    {
      data_len = 0;

      // 添加超時保護
      unsigned long startTime = millis();

      while (client[i].available() && data_len < 1460)
      {
        data[data_len] = client[i].read();
        data_len++;

        // 添加超時檢查，避免阻塞
        if (millis() - startTime > 100)
        { // 最多100ms讀取時間
          Serial.printf("客戶端 %d 讀取超時\n", i);
          break;
        }
        yield();
      }

      if (data_len > 0)
      {
        if (!compareID(i)) // 傳遞客戶端索引
        {
          data_process(i); // 傳遞客戶端索引
        }
      }
    }
  }
}

//============================================Data_ID_Name比對============================================
bool compareID(int clientIndex) // 修改這裡，添加參數
{
  bool compare_flag = false;
  for (int compare_c = 0; compare_c < sizeof(ID_Name); compare_c++)
  {
    if (data[compare_c] != ID_Name[compare_c] || data[compare_c + 32] != ID_Name[compare_c])
    {
      Server_monitor(1, clientIndex); // 傳遞clientIndex
      client[clientIndex].stop();
      memset(data, 0, sizeof(data));
      compare_flag = true;
      break;
    }
  }
  return compare_flag;
}

//============================================對資料做處理============================================
void data_process(int clientIndex) // 修改這裡，添加參數
{
  uint8_t ipsd = data[30];
  uint8_t ipdd = data[62];

  // 檢查ID範圍
  if (ipsd >= MAX_SRV_CLIENTS || ipdd >= MAX_SRV_CLIENTS)
  {
    Serial.printf("ID範圍錯誤: ipsd=%d, ipdd=%d\n", ipsd, ipdd);
    client[clientIndex].stop();
    memset(data, 0, sizeof(data));
    return;
  }

  IPAddress IP = client[clientIndex].remoteIP();
  uint8_t H_Port = client[clientIndex].remotePort() / 256;
  uint8_t L_port = client[clientIndex].remotePort() % 256;

  // 使用正確的方式儲存IP地址
  IPAddress remoteIP = client[clientIndex].remoteIP();
  ID_Information_list[ipsd][0] = remoteIP[0];
  ID_Information_list[ipsd][1] = remoteIP[1];
  ID_Information_list[ipsd][2] = remoteIP[2];
  ID_Information_list[ipsd][3] = remoteIP[3];
  ID_Information_list[ipsd][4] = H_Port;
  ID_Information_list[ipsd][5] = L_port;

  client_room[ipsd] = client[clientIndex];

  Server_monitor(2, clientIndex);

  // 檢查目標客戶端是否存在且連接
  if (client_room[ipdd] && client_room[ipdd].connected())
  {
    client_room[ipdd].write(data, data_len);
  }
  else
  {
    Serial.printf("目標客戶端 %d 未連接\n", ipdd);
  }

  memset(data, 0, sizeof(data));

  // 更新流量統計
  trafficStats.totalPackets++;
  trafficStats.totalBytes += data_len;

  // 每100個封包顯示一次統計
  if (trafficStats.totalPackets % 100 == 0)
  {
    Serial.printf("[Traffic] 封包: %lu, 位元組: %lu\n",
                  trafficStats.totalPackets, trafficStats.totalBytes);
  }
}

//============================================記憶體管理和監控============================================
void checkMemory()
{
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 30000)
  { // 每30秒檢查一次
    lastCheck = millis();
    Serial.printf("[Memory] Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("[Memory] Max Alloc Heap: %d bytes\n", ESP.getMaxAllocHeap());

    // 檢查客戶端連接數
    int activeClients = 0;
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if (client[i] && client[i].connected())
      {
        activeClients++;
      }
    }
    Serial.printf("[TCP] Active Clients: %d/%d\n", activeClients, MAX_SRV_CLIENTS);
  }
}

// 添加客戶端清理函數
void cleanupClients()
{
  static unsigned long lastCleanup = 0;
  if (millis() - lastCleanup > 60000)
  { // 每60秒清理一次
    lastCleanup = millis();

    int cleaned = 0;
    for (int i = 0; i < MAX_SRV_CLIENTS; i++)
    {
      if (client[i] && !client[i].connected())
      {
        client[i].stop();
        cleaned++;
      }
    }
    if (cleaned > 0)
    {
      Serial.printf("[Cleanup] Cleaned %d inactive clients\n", cleaned);
    }
  }
}

//============================================每日服務檢查============================================
void PerDateService()
{
  Serial.println("========= Per Date Service Check===========");
}

/*============================================主程式============================================*/
void setup()
{
  // 低電壓強制運行
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000); // 等待串口穩定
  Serial.println("\n\n=== ESP32 網路伺服器啟動 ===");
  Serial.println("初始化 SPIFFS...");

  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS 初始化失敗");
    Serial.println("嘗試格式化...");
    SPIFFS.format();
    if (!SPIFFS.begin(true))
    {
      Serial.println("SPIFFS 初始化仍然失敗");
    }
  }

  // 載入設定
  Serial.println("載入網路設定...");
  loadConfig();

  // 初始化網路
  Serial.println("初始化網路...");
  initNetworks();

  // 啟動TCP伺服器
  Serial.println("啟動TCP伺服器...");
  tcpServer.begin();
  tcpServer.setNoDelay(true);

  // 初始化Web Server
  Serial.println("啟動Web伺服器...");
  initWebServer();

  // 顯示啟動資訊
  Serial.println("\n=== 系統啟動完成 ===");
  Serial.print("設備名稱: ");
  Serial.println(networkConfig.device_name);
  Serial.print("網路模式: ");
  Serial.println(getModeString(currentMode));

  if (ap_mode_active)
  {
    Serial.print("AP SSID: ");
    Serial.println(ap_ssid);
    Serial.print("AP IP: 192.168.4.1");
    Serial.println(" (無密碼)");
  }

  if (eth_connected)
  {
    Serial.print("乙太網IP: ");
    Serial.println(ethIP);
  }

  if (wifi_connected)
  {
    Serial.print("WiFi IP: ");
    Serial.println(wifiIP);
  }

  Serial.print("Web介面: http://");
  Serial.println(getActiveIP());

  if (networkConfig.first_setup)
  {
    Serial.println("⚠️  首次設定模式：請連接WiFi AP進行設定");
  }

  Serial.println("===================================\n");

  memset(data, NULL, sizeof(data));
  PerDateService();
  time1 = millis();
}

void loop()
{
  // 處理Web Server請求
  webServer.handleClient();

  // 添加小延遲，避免過度CPU使用
  delay(5);

  // 處理TCP客戶端
  Working_Light();
  New_Client();
  Client_Recv_Data();

  // 定期清理和監控
  cleanupClients();
  checkMemory();
}