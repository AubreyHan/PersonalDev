#include "NetworkManager.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <type_traits>
#include <utility>

namespace {

// Compile-time SFINAE inspection for WiFi.setPins(clk, cmd, d0, d1, d2, d3, rst):
// Verified against local WiFiGeneric.h & WiFiGeneric.cpp (L251-280):
// - Local framework-arduinoespressif32 hardcodes Tab5 SDIO pins 12/13/11/10/9/8 + GPIO15
//   inside wifiHostedInit() and does not declare WiFiClass::setPins().
// - Upstream Arduino-ESP32 Hosted WiFi exposes WiFi.setPins(clk, cmd, d0, d1, d2, d3, rst).
// This trait cleanly supports both without compilation error!
template <typename T, typename = void>
struct HasWifiSetPins7 : std::false_type {};

template <typename T>
struct HasWifiSetPins7<
    T,
    std::void_t<decltype(std::declval<T&>().setPins(
        int8_t(12), int8_t(13), int8_t(11), int8_t(10), int8_t(9), int8_t(8), int8_t(15)))>>
    : std::true_type {};

template <typename T>
inline void applyTab5SdioPinsIfAvailable(T& wifiInstance) {
    if constexpr (HasWifiSetPins7<T>::value) {
        wifiInstance.setPins(
            Esp32C6SdioConfig::PIN_CLK,
            Esp32C6SdioConfig::PIN_CMD,
            Esp32C6SdioConfig::PIN_D0,
            Esp32C6SdioConfig::PIN_D1,
            Esp32C6SdioConfig::PIN_D2,
            Esp32C6SdioConfig::PIN_D3,
            Esp32C6SdioConfig::PIN_RESET
        );
    }
}

void escapeQrToken(const char* src, char* dst, size_t dstCap) {
    if (dst == nullptr || dstCap == 0) {
        return;
    }
    size_t w = 0;
    if (src != nullptr) {
        for (size_t i = 0; src[i] != '\0' && (w + 2) < dstCap; ++i) {
            const char c = src[i];
            if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
                dst[w++] = '\\';
            }
            dst[w++] = c;
        }
    }
    dst[w] = '\0';
}

bool endsWithCi(const String& s, const char* suffix) {
    const size_t sufLen = strlen(suffix);
    if (s.length() < sufLen) {
        return false;
    }
    return strcasecmp(s.c_str() + (s.length() - sufLen), suffix) == 0;
}

// Pure-Dark Automotive Mobile Web Portal HTML (Setup Mode: M5Track-Setup)
const char kSetupPortalHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>M5Track-Tab5 极速配网与云服务配置</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#fff;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;padding:14px;max-width:560px;margin:0 auto}
h1{font-size:20px;color:#00FFAA;margin-bottom:4px;letter-spacing:.5px}
.sub{color:#8B949E;font-size:12px;margin-bottom:14px}
.card{background:#0D131A;border:1px solid #243242;border-radius:12px;padding:14px;margin-bottom:14px}
.card h2{font-size:15px;color:#00D4FF;margin-bottom:10px;display:flex;justify-content:space-between;align-items:center}
.badge{padding:3px 8px;border-radius:99px;font-size:12px;font-weight:600}
.ok{background:rgba(0,255,170,.16);color:#00FFAA;border:1px solid #00FFAA}
.warn{background:rgba(255,165,0,.16);color:#FFA500;border:1px solid #FFA500}
label{display:block;font-size:12px;color:#8B949E;margin:8px 0 4px}
input{width:100%;padding:11px 12px;border-radius:8px;border:1px solid #243242;background:#05080C;color:#fff;font-size:15px;outline:none}
input:focus{border-color:#00FFAA}
button{padding:11px 16px;border-radius:8px;border:none;font-size:14px;font-weight:700;cursor:pointer;transition:.15s}
.btn-p{background:#00FFAA;color:#000;width:100%;margin-top:12px}
.btn-s{background:#16202C;color:#00D4FF;border:1px solid #243242;padding:6px 12px;font-size:12px}
.btn-d{background:rgba(255,59,48,.18);color:#FF3B30;border:1px solid #FF3B30;padding:5px 10px;font-size:12px}
.ap-row{display:flex;justify-content:space-between;align-items:center;padding:10px;border-bottom:1px solid #16202C;cursor:pointer}
.ap-row:last-child{border-bottom:none}
.ap-row:active{background:#16202C}
.rssi{color:#FFD700;font-size:12px;font-family:monospace}
.msg{margin-top:10px;font-size:13px;color:#00FFAA;min-height:18px}
</style>
</head>
<body>
<h1>M5Track-Tab5 无线配网中心</h1>
<div class="sub">ESP32-C6 Wi-Fi 6 多热点漫游 (5槽位) · AGPS / OneDrive 配置</div>

<div class="card">
  <h2><span>当前连接状态</span><span id="stBadge" class="badge warn">检测中</span></h2>
  <div id="stInfo" style="font-size:13px;color:#8B949E">正在读取设备网络状态...</div>
</div>

<div class="card">
  <h2><span>添加 / 更新 Wi-Fi 热点</span><button class="btn-s" onclick="scanAp()">刷新扫描</button></h2>
  <div id="apList" style="max-height:190px;overflow-y:auto;margin-bottom:8px;border:1px solid #16202C;border-radius:8px"></div>
  <label>Wi-Fi 名称 (SSID)</label>
  <input id="ssid" placeholder="点击上方扫描结果或手动输入">
  <label>Wi-Fi 密码 (开放网络留空)</label>
  <input id="pwd" type="password" placeholder="输入热点密码">
  <button class="btn-p" onclick="connectAp()">保存至 NVS 并立即连接</button>
  <div id="connMsg" class="msg"></div>
</div>

<div class="card">
  <h2><span>已保存漫游网络 (最多 5 组)</span></h2>
  <div id="savedList" style="font-size:13px;color:#8B949E">加载中...</div>
</div>

<div class="card">
  <h2><span>可选云服务凭证 (AGPS & OneDrive)</span></h2>
  <label>u-blox AssistNow Online Token (秒级热搜星)</label>
  <input id="agpsTok" placeholder="例如 Thingstream Token (选填)">
  <label>Microsoft OneDrive Client ID (Azure App ID)</label>
  <input id="odCid" placeholder="UUID 格式 Client ID (选填)">
  <label>OneDrive Tenant (个人版填 consumers，通用填 common)</label>
  <input id="odTen" placeholder="common">
  <button class="btn-p" style="background:#00D4FF" onclick="saveCloud()">保存云服务凭证</button>
  <div id="cloudMsg" class="msg"></div>
</div>

<script>
async function refreshStatus(){
  try{
    const r=await fetch('/api/status');const d=await r.json();
    const b=document.getElementById('stBadge');
    const info=document.getElementById('stInfo');
    if(d.connected){
      b.className='badge ok';b.textContent='已连接 Wi-Fi';
      info.innerHTML='SSID: <b style="color:#fff">'+esc(d.ssid)+'</b> | IP: '+d.ip+' | RSSI: '+d.rssi+' dBm';
      if(d.provisionedOk){
        document.getElementById('connMsg').textContent='配网成功！设备将在 3 秒后自动切回驾驶仪表盘...';
      }
    }else{
      b.className='badge warn';b.textContent='待连接';
      info.textContent=d.statusText||'当前处于配网热点模式 (192.168.4.1)';
    }
  }catch(e){}
}
function esc(s){return (s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
async function scanAp(){
  const box=document.getElementById('apList');
  box.innerHTML='<div style="padding:10px;color:#8B949E;font-size:13px">正在扫描周边 2.4GHz Wi-Fi...</div>';
  try{
    const r=await fetch('/api/scan');const d=await r.json();
    if(!d.aps||!d.aps.length){box.innerHTML='<div style="padding:10px;color:#8B949E">暂未发现热点，请再试一次</div>';return;}
    box.innerHTML=d.aps.map(a=>`<div class="ap-row" onclick="pickSsid('${esc(a.ssid)}')"><span>${esc(a.ssid)} ${a.auth>0?'🔒':''}</span><span class="rssi">${a.rssi} dBm</span></div>`).join('');
  }catch(e){box.innerHTML='<div style="padding:10px;color:#FF3B30">扫描请求超时</div>';}
}
function pickSsid(s){document.getElementById('ssid').value=s;document.getElementById('pwd').focus();}
async function loadSaved(){
  try{
    const r=await fetch('/api/saved');const d=await r.json();
    const box=document.getElementById('savedList');
    if(!d.saved||!d.saved.length){box.innerHTML='暂无已保存网络';return;}
    box.innerHTML=d.saved.map((s,i)=>`<div class="ap-row"><span>#${i+1} <b style="color:#fff">${esc(s.ssid)}</b></span><button class="btn-d" onclick="delSaved(${s.index})">删除</button></div>`).join('');
  }catch(e){}
}
async function connectAp(){
  const ssid=document.getElementById('ssid').value.trim();
  const pwd=document.getElementById('pwd').value;
  const m=document.getElementById('connMsg');
  if(!ssid){m.style.color='#FF3B30';m.textContent='请输入或选择 Wi-Fi 名称';return;}
  m.style.color='#FFD700';m.textContent='正在写入凭证并验证连接...';
  try{
    const r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid,pwd})});
    const d=await r.json();
    m.style.color=d.success?'#00FFAA':'#FF3B30';
    m.textContent=d.message||'已提交连接';
    loadSaved();
  }catch(e){m.style.color='#FF3B30';m.textContent='提交失败';}
}
async function delSaved(idx){
  await fetch('/api/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:idx})});
  loadSaved();
}
async function loadCloud(){
  try{
    const r=await fetch('/api/cloud/config');const d=await r.json();
    if(d.assistNowToken)document.getElementById('agpsTok').value=d.assistNowToken;
    if(d.oneDriveClientId)document.getElementById('odCid').value=d.oneDriveClientId;
    if(d.oneDriveTenant)document.getElementById('odTen').value=d.oneDriveTenant;
  }catch(e){}
}
async function saveCloud(){
  const m=document.getElementById('cloudMsg');
  const body={
    assistNowToken:document.getElementById('agpsTok').value.trim(),
    oneDriveClientId:document.getElementById('odCid').value.trim(),
    oneDriveTenant:document.getElementById('odTen').value.trim()||'common'
  };
  try{
    const r=await fetch('/api/cloud/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const d=await r.json();
    m.textContent=d.success?'云凭证已持久化保存至 NVS！':'保存失败';
  }catch(e){m.textContent='保存请求失败';}
}
refreshStatus();scanAp();loadSaved();loadCloud();
setInterval(refreshStatus,2000);
</script>
</body>
</html>)rawliteral";

// Pure-Dark Automotive Wireless Track Export HTML (Share Mode: M5Track-Share)
const char kShareTracksHtml[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>M5Track-Tab5 无线轨迹极速导出</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{background:#000;color:#fff;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;padding:14px;max-width:680px;margin:0 auto}
h1{font-size:20px;color:#00FFAA;margin-bottom:4px}
.sub{color:#8B949E;font-size:12px;margin-bottom:12px}
.card{background:#0D131A;border:1px solid #243242;border-radius:12px;padding:14px;margin-bottom:12px}
.bar-bg{height:10px;background:#16202C;border-radius:99px;overflow:hidden;margin:8px 0}
.bar-fg{height:100%;background:linear-gradient(90deg,#00FFAA,#FFD700);width:0%}
.tabs{display:flex;gap:6px;margin-bottom:12px;flex-wrap:wrap}
.tab{flex:1;min-width:70px;padding:9px 8px;border-radius:8px;background:#0D131A;border:1px solid #243242;color:#8B949E;font-size:13px;font-weight:700;text-align:center;cursor:pointer}
.tab.act{background:#00FFAA;color:#000;border-color:#00FFAA}
.frow{display:flex;justify-content:space-between;align-items:center;padding:10px 0;border-bottom:1px solid #16202C;gap:8px}
.frow:last-child{border-bottom:none}
.fname{font-size:14px;font-weight:600;word-break:break-all}
.fmeta{font-size:11px;color:#8B949E;margin-top:3px}
.tag-w{color:#00D4FF;border:1px solid #00D4FF;padding:1px 6px;border-radius:4px;font-size:10px;margin-right:6px}
.tag-c{color:#FFD700;border:1px solid #FFD700;padding:1px 6px;border-radius:4px;font-size:10px;margin-right:6px}
.btn-dl{background:#00FFAA;color:#000;text-decoration:none;padding:7px 12px;border-radius:7px;font-size:12px;font-weight:700;white-space:nowrap}
.btn-del{background:rgba(255,59,48,.16);color:#FF3B30;border:1px solid #FF3B30;padding:6px 10px;border-radius:7px;font-size:12px;cursor:pointer}
.quick{display:flex;gap:8px;margin-top:8px;flex-wrap:wrap}
.quick a{flex:1;text-align:center;background:#16202C;border:1px solid #243242;color:#00D4FF;text-decoration:none;padding:9px;border-radius:8px;font-size:13px;font-weight:600}
</style>
</head>
<body>
<h1>M5Track-Share 轨迹无线导出</h1>
<div class="sub">WGS-84 国际标准版 & GCJ-02 国内高德/两步路版 · 免拔卡高速直传</div>

<div class="card">
  <div id="quotaTxt" style="font-size:13px;color:#fff">正在读取 TF 卡容量与 50% 轨迹配额...</div>
  <div class="bar-bg"><div id="quotaBar" class="bar-fg"></div></div>
  <div class="quick">
    <a href="/api/download?file=trips.csv">📊 下载行程总表 (trips.csv)</a>
    <a href="/api/download?file=power.log">⚡ 下载电源日志 (power.log)</a>
  </div>
</div>

<div class="tabs">
  <div class="tab act" onclick="setCat('all',this)">全部</div>
  <div class="tab" onclick="setCat('single',this)">单次 (Single)</div>
  <div class="tab" onclick="setCat('daily',this)">单日 (Daily)</div>
  <div class="tab" onclick="setCat('sub',this)">小记 (Sub)</div>
</div>

<div class="card" id="fileBox">正在加载 GPX 轨迹列表...</div>

<script>
let allFiles=[],curCat='all';
function fmtSz(b){if(b>=1048576)return (b/1048576).toFixed(2)+' MB';if(b>=1024)return (b/1024).toFixed(1)+' KB';return b+' B';}
function esc(s){return (s||'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function setCat(c,el){curCat=c;document.querySelectorAll('.tab').forEach(t=>t.classList.remove('act'));el.classList.add('act');render();}
async function loadTracks(){
  try{
    const r=await fetch('/api/tracks');const d=await r.json();
    allFiles=d.files||[];
    const totGB=(d.totalBytes||0)/1073741824;
    const trkMB=(d.tracksBytes||0)/1048576;
    const pct=Math.min(100,((d.quotaRatio||0)*200));
    document.getElementById('quotaTxt').innerHTML=`轨迹占用: <b>${trkMB.toFixed(1)} MB</b> / 配额上限 (50%): ${(totGB*512).toFixed(0)} MB (TF总计 ${totGB.toFixed(1)} GB)`;
    document.getElementById('quotaBar').style.width=pct.toFixed(1)+'%';
    render();
  }catch(e){document.getElementById('fileBox').textContent='读取 TF 卡列表失败';}
}
function render(){
  const box=document.getElementById('fileBox');
  const list=allFiles.filter(f=>curCat==='all'||f.category===curCat);
  if(!list.length){box.innerHTML='<div style="color:#8B949E;font-size:13px;padding:8px 0">当前分类下暂无 GPX 轨迹文件</div>';return;}
  box.innerHTML=list.map(f=>{
    const tag=f.type==='gcj02'?'<span class="tag-c">GCJ-02 国内版</span>':'<span class="tag-w">WGS-84 国际版</span>';
    const act=f.active?' <span style="color:#FF3B30;font-size:11px">[录制中]</span>':'';
    const delBtn=f.active?'':`<button class="btn-del" onclick="delFile('${esc(f.path)}')">删除</button>`;
    return `<div class="frow"><div style="min-width:0"><div class="fname">${esc(f.name)}${act}</div><div class="fmeta">${tag}${esc(f.category)} · ${fmtSz(f.size)}</div></div><div style="display:flex;gap:6px;align-items:center"><a class="btn-dl" href="/api/download?file=${encodeURIComponent(f.path)}">下载</a>${delBtn}</div></div>`;
  }).join('');
}
async function delFile(p){
  if(!confirm('确认删除轨迹文件 '+p+' ?'))return;
  await fetch('/api/tracks/delete',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({path:p})});
  loadTracks();
}
loadTracks();
</script>
</body>
</html>)rawliteral";

} // namespace

NetworkManagerService& NetworkManagerService::instance() {
    static NetworkManagerService s_instance;
    return s_instance;
}

NetworkManagerService::NetworkManagerService()
    : _sd(nullptr),
      _sdMutex(nullptr),
      _sdMutexRecursive(false),
      _sdLockCb(nullptr),
      _sdUnlockCb(nullptr),
      _ntpSyncCb(nullptr),
      _dnsServer(),
      _webServer(NetConfig::HTTP_PORT),
      _hwInitialized(false),
      _softApRunning(false),
      _isShareHotspot(false),
      _scanInProgress(false),
      _roamAfterScan(false),
      _portalTestConnecting(false),
      _ntpRequested(false),
      _ntpSynced(false),
      _savedCount(0),
      _roamQueueCount(0),
      _roamCursor(0),
      _staConnectStartMs(0),
      _lastReconnectScanMs(0),
      _lastTelemetrySyncMs(0),
      _portalProvisionedMs(0),
      _tunnelUploadOk(false),
      _tunnelUploadBytes(0) {
    memset(_saved, 0, sizeof(_saved));
    memset(_roamQueue, 0, sizeof(_roamQueue));
}

NetworkManagerService::~NetworkManagerService() {
    if (_softApRunning) {
        _dnsServer.stop();
        _webServer.stop();
    }
}

void NetworkManagerService::attachSdStorage(SdFs* sd, SemaphoreHandle_t sdMutex, bool isRecursiveMutex) {
    _sd               = sd;
    _sdMutex          = sdMutex;
    _sdMutexRecursive = isRecursiveMutex;
}

void NetworkManagerService::setSdLockCallbacks(SdLockCallback lockCb, SdUnlockCallback unlockCb) {
    _sdLockCb   = lockCb;
    _sdUnlockCb = unlockCb;
}

bool NetworkManagerService::lockSd(uint32_t timeoutMs) const {
    if (_sdLockCb) {
        return _sdLockCb(timeoutMs);
    }
    if (_sdMutex != nullptr) {
        const TickType_t ticks = pdMS_TO_TICKS(timeoutMs);
        if (_sdMutexRecursive) {
            return (xSemaphoreTakeRecursive(_sdMutex, ticks) == pdTRUE);
        }
        return (xSemaphoreTake(_sdMutex, ticks) == pdTRUE);
    }
    return (_sd != nullptr);
}

void NetworkManagerService::unlockSd() const {
    if (_sdUnlockCb) {
        _sdUnlockCb();
        return;
    }
    if (_sdMutex != nullptr) {
        if (_sdMutexRecursive) {
            xSemaphoreGiveRecursive(_sdMutex);
        } else {
            xSemaphoreGive(_sdMutex);
        }
    }
}

bool NetworkManagerService::ensureEsp32C6PowerAndSdioReady() {
    if (_hwInitialized) {
        return true;
    }

    // STRICT #3 §1.3 & §1.5 Rule:
    // Verify U7 PI4IOE5V6408 (0x44) P0 WLAN_PWR_EN is Output, HIGH, and NOT High-Z (0x07 bit0=0),
    // and guarantee >= 50ms settle window before touching ESP-Hosted SDIO Wi-Fi APIs!
    bool newlyPoweredC6 = false;
    if (M5.In_I2C.isEnabled()) {
        const uint8_t addr  = SysI2cConfig::ADDR_PI4IOE_2; // 0x44 (U7)
        const uint32_t freq = SysI2cConfig::FREQ_HZ;
        const uint8_t dir   = M5.In_I2C.readRegister8(addr, Pi4IoeConfig::REG_IO_DIR, freq);
        const uint8_t out   = M5.In_I2C.readRegister8(addr, Pi4IoeConfig::REG_OUT_SET, freq);
        const uint8_t highZ = M5.In_I2C.readRegister8(addr, Pi4IoeConfig::REG_OUT_HIGH_Z, freq);

        const bool pwrAlreadyActive =
            ((dir & Pi4IoeConfig::U7_WLAN_PWR_EN) != 0) &&
            ((out & Pi4IoeConfig::U7_WLAN_PWR_EN) != 0) &&
            ((highZ & Pi4IoeConfig::U7_WLAN_PWR_EN) == 0);

        if (!pwrAlreadyActive) {
            M5.In_I2C.bitOn(addr, Pi4IoeConfig::REG_IO_DIR, Pi4IoeConfig::U7_WLAN_PWR_EN, freq);
            M5.In_I2C.bitOn(addr, Pi4IoeConfig::REG_OUT_SET, Pi4IoeConfig::U7_WLAN_PWR_EN, freq);
            M5.In_I2C.bitOff(addr, Pi4IoeConfig::REG_OUT_HIGH_Z, Pi4IoeConfig::U7_WLAN_PWR_EN, freq);
            newlyPoweredC6 = true;
        }
    }

    if (newlyPoweredC6) {
        vTaskDelay(pdMS_TO_TICKS(Esp32C6SdioConfig::POST_POWERON_DELAY_MS));
    } else if (millis() < Esp32C6SdioConfig::POST_POWERON_DELAY_MS) {
        vTaskDelay(pdMS_TO_TICKS(Esp32C6SdioConfig::POST_POWERON_DELAY_MS - millis()));
    }

    // Apply SDIO pins (12, 13, 11, 10, 9, 8, 15) if upstream WiFi.setPins exists
    applyTab5SdioPinsIfAvailable(WiFi);

    WiFi.persistent(false);
    const bool modeOk = WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);

    _hwInitialized = modeOk;
    {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().boot.wifiCoProcOnline = modeOk;
    }
    return modeOk;
}

bool NetworkManagerService::begin(SdFs* sd, SemaphoreHandle_t sdMutex, bool isRecursiveMutex) {
    if (sd != nullptr) {
        attachSdStorage(sd, sdMutex, isRecursiveMutex);
    }

    loadSavedNetworksAndTokensFromNvs();
    setupWebRoutes();

    if (!ensureEsp32C6PowerAndSdioReady()) {
        syncNetworkStateSnapshot("ESP32-C6 SDIO init failed");
        return false;
    }

    if (_savedCount > 0) {
        _roamAfterScan = true;
        triggerAsyncScan();
    } else {
        syncNetworkStateSnapshot("No saved Wi-Fi");
    }
    return true;
}

void NetworkManagerService::formatWifiQrCode(const char* ssid, const char* password,
                                      char* outBuf, size_t outSize) {
    if (outBuf == nullptr || outSize == 0) {
        return;
    }
    char escSsid[48] = {};
    escapeQrToken((ssid != nullptr) ? ssid : "", escSsid, sizeof(escSsid));

    if (password != nullptr && strlen(password) >= 8) {
        char escPwd[80] = {};
        escapeQrToken(password, escPwd, sizeof(escPwd));
        snprintf(outBuf, outSize, "WIFI:T:WPA;S:%s;P:%s;;", escSsid, escPwd);
    } else {
        snprintf(outBuf, outSize, "WIFI:T:nopass;S:%s;;", escSsid);
    }
}

void NetworkManagerService::loadSavedNetworksAndTokensFromNvs() {
    memset(_saved, 0, sizeof(_saved));
    _savedCount = 0;

    char agpsToken[64] = {};
    char odClient[64]  = {};
    char odTenant[24]  = {};

    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, true)) {
        uint8_t cnt = prefs.getUChar(NvsConfig::KEY_WIFI_COUNT, 0);
        if (cnt > NetConfig::MAX_SAVED_NETWORKS) {
            cnt = NetConfig::MAX_SAVED_NETWORKS;
        }

        for (uint8_t i = 0; i < cnt; ++i) {
            char ssidKey[16] = {};
            char pwdKey[16]  = {};
            snprintf(ssidKey, sizeof(ssidKey), "%s%u", NvsConfig::KEY_SSID_PREFIX, i);
            snprintf(pwdKey, sizeof(pwdKey), "%s%u", NvsConfig::KEY_PWD_PREFIX, i);

            char ssidBuf[33] = {};
            char pwdBuf[65]  = {};
            prefs.getString(ssidKey, ssidBuf, sizeof(ssidBuf));
            prefs.getString(pwdKey, pwdBuf, sizeof(pwdBuf));

            if (ssidBuf[0] != '\0' && _savedCount < NetConfig::MAX_SAVED_NETWORKS) {
                _saved[_savedCount].inUse = true;
                strncpy(_saved[_savedCount].ssid, ssidBuf, sizeof(_saved[_savedCount].ssid) - 1);
                strncpy(_saved[_savedCount].password, pwdBuf, sizeof(_saved[_savedCount].password) - 1);
                _savedCount++;
            }
        }

        prefs.getString(NvsConfig::KEY_AGPS_TOKEN, agpsToken, sizeof(agpsToken));
        prefs.getString(NvsConfig::KEY_OD_CLIENT_ID, odClient, sizeof(odClient));
        prefs.getString(NvsConfig::KEY_OD_TENANT, odTenant, sizeof(odTenant));
        prefs.end();
    }

    StateGuard lock(SystemState::instance());
    auto& st = SystemState::instance().data();
    st.network.savedCount = _savedCount;
    for (uint8_t i = 0; i < NetConfig::MAX_SAVED_NETWORKS; ++i) {
        st.network.savedList[i] = _saved[i];
    }
    if (agpsToken[0] != '\0') {
        strncpy(st.settings.assistNowToken, agpsToken, sizeof(st.settings.assistNowToken) - 1);
    }
    if (odClient[0] != '\0') {
        strncpy(st.settings.oneDriveClientId, odClient, sizeof(st.settings.oneDriveClientId) - 1);
    }
    if (odTenant[0] != '\0') {
        strncpy(st.settings.oneDriveTenant, odTenant, sizeof(st.settings.oneDriveTenant) - 1);
    }
}

void NetworkManagerService::persistSavedNetworksToNvs() {
    Preferences prefs;
    if (!prefs.begin(NvsConfig::NAMESPACE, false)) {
        return;
    }
    prefs.putUChar(NvsConfig::KEY_WIFI_COUNT, _savedCount);
    for (uint8_t i = 0; i < NetConfig::MAX_SAVED_NETWORKS; ++i) {
        char ssidKey[16] = {};
        char pwdKey[16]  = {};
        snprintf(ssidKey, sizeof(ssidKey), "%s%u", NvsConfig::KEY_SSID_PREFIX, i);
        snprintf(pwdKey, sizeof(pwdKey), "%s%u", NvsConfig::KEY_PWD_PREFIX, i);
        if (i < _savedCount && _saved[i].inUse && _saved[i].ssid[0] != '\0') {
            prefs.putString(ssidKey, _saved[i].ssid);
            prefs.putString(pwdKey, _saved[i].password);
        } else {
            prefs.remove(ssidKey);
            prefs.remove(pwdKey);
        }
    }
    prefs.end();

    StateGuard lock(SystemState::instance());
    auto& net = SystemState::instance().data().network;
    net.savedCount = _savedCount;
    for (uint8_t i = 0; i < NetConfig::MAX_SAVED_NETWORKS; ++i) {
        net.savedList[i] = _saved[i];
    }
}

bool NetworkManagerService::saveWifiCredential(const char* ssid, const char* password) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    const char* safePwd = (password != nullptr) ? password : "";

    // Update existing slot if SSID matches
    for (uint8_t i = 0; i < _savedCount; ++i) {
        if (strcmp(_saved[i].ssid, ssid) == 0) {
            strncpy(_saved[i].password, safePwd, sizeof(_saved[i].password) - 1);
            _saved[i].password[sizeof(_saved[i].password) - 1] = '\0';
            _saved[i].inUse = true;
            persistSavedNetworksToNvs();
            return true;
        }
    }

    // Append into free slot or FIFO-evict oldest slot 0 when all 5 slots are full
    if (_savedCount < NetConfig::MAX_SAVED_NETWORKS) {
        SavedWifiCredential& slot = _saved[_savedCount++];
        slot = SavedWifiCredential{};
        slot.inUse = true;
        strncpy(slot.ssid, ssid, sizeof(slot.ssid) - 1);
        strncpy(slot.password, safePwd, sizeof(slot.password) - 1);
    } else {
        for (uint8_t i = 0; i + 1 < NetConfig::MAX_SAVED_NETWORKS; ++i) {
            _saved[i] = _saved[i + 1];
        }
        SavedWifiCredential& slot = _saved[NetConfig::MAX_SAVED_NETWORKS - 1];
        slot = SavedWifiCredential{};
        slot.inUse = true;
        strncpy(slot.ssid, ssid, sizeof(slot.ssid) - 1);
        strncpy(slot.password, safePwd, sizeof(slot.password) - 1);
    }

    persistSavedNetworksToNvs();
    return true;
}

bool NetworkManagerService::deleteWifiCredential(uint8_t index) {
    if (index >= _savedCount) {
        return false;
    }
    char removedSsid[33] = {};
    strncpy(removedSsid, _saved[index].ssid, sizeof(removedSsid) - 1);

    for (uint8_t i = index; i + 1 < _savedCount; ++i) {
        _saved[i] = _saved[i + 1];
    }
    _saved[_savedCount - 1] = SavedWifiCredential{};
    _savedCount--;
    persistSavedNetworksToNvs();

    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == removedSsid) {
        WiFi.disconnect(false, false);
        if (!_softApRunning && _savedCount > 0) {
            _roamAfterScan = true;
            triggerAsyncScan();
        }
    }
    syncNetworkStateSnapshot();
    return true;
}

bool NetworkManagerService::deleteWifiCredentialBySsid(const char* ssid) {
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    for (uint8_t i = 0; i < _savedCount; ++i) {
        if (strcmp(_saved[i].ssid, ssid) == 0) {
            return deleteWifiCredential(i);
        }
    }
    return false;
}

bool NetworkManagerService::triggerAsyncScan() {
    if (!_hwInitialized && !ensureEsp32C6PowerAndSdioReady()) {
        return false;
    }
    if (_scanInProgress) {
        return true;
    }

    // Share-only AP mode must enable STA interface temporarily if scan is explicitly requested
    if (_softApRunning && _isShareHotspot) {
        return false;
    }

    WiFi.scanDelete();
    const int16_t res = WiFi.scanNetworks(true /*async*/, false /*show_hidden*/, false /*passive*/, 250U);
    if (res == WIFI_SCAN_RUNNING || res >= 0) {
        _scanInProgress = true;
        if (!_softApRunning) {
            StateGuard lock(SystemState::instance());
            auto& net = SystemState::instance().data().network;
            net.mode = WIFI_OP_STA_SCANNING;
            strncpy(net.statusText, "Scanning Wi-Fi...", sizeof(net.statusText) - 1);
        }
        return true;
    }
    return false;
}

void NetworkManagerService::pollAsyncScanResults() {
    if (!_scanInProgress) {
        return;
    }

    const int16_t n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
        return;
    }
    _scanInProgress = false;

    ScannedApEntry topList[NetworkData::MAX_SCAN_RESULTS] = {};
    uint8_t topCount = 0;

    if (n > 0) {
        for (int16_t i = 0; i < n; ++i) {
            const String ssidStr = WiFi.SSID(static_cast<uint8_t>(i));
            if (ssidStr.isEmpty()) {
                continue;
            }
            const int32_t rssi32 = WiFi.RSSI(static_cast<uint8_t>(i));
            const int8_t  rssi   = static_cast<int8_t>((rssi32 < -127) ? -127 : ((rssi32 > 0) ? 0 : rssi32));
            const uint8_t auth   = static_cast<uint8_t>(WiFi.encryptionType(static_cast<uint8_t>(i)));

            // Deduplicate identical SSIDs keeping strongest RSSI
            int existingIdx = -1;
            for (uint8_t k = 0; k < topCount; ++k) {
                if (strcmp(topList[k].ssid, ssidStr.c_str()) == 0) {
                    existingIdx = static_cast<int>(k);
                    break;
                }
            }
            if (existingIdx >= 0) {
                if (rssi > topList[existingIdx].rssi) {
                    topList[existingIdx].rssi     = rssi;
                    topList[existingIdx].authMode = auth;
                }
                continue;
            }

            if (topCount < NetworkData::MAX_SCAN_RESULTS) {
                strncpy(topList[topCount].ssid, ssidStr.c_str(), sizeof(topList[topCount].ssid) - 1);
                topList[topCount].rssi     = rssi;
                topList[topCount].authMode = auth;
                topCount++;
            } else {
                // Replace weakest entry if stronger
                uint8_t weakest = 0;
                for (uint8_t k = 1; k < topCount; ++k) {
                    if (topList[k].rssi < topList[weakest].rssi) {
                        weakest = k;
                    }
                }
                if (rssi > topList[weakest].rssi) {
                    memset(&topList[weakest], 0, sizeof(ScannedApEntry));
                    strncpy(topList[weakest].ssid, ssidStr.c_str(), sizeof(topList[weakest].ssid) - 1);
                    topList[weakest].rssi     = rssi;
                    topList[weakest].authMode = auth;
                }
            }
        }

        // Sort topList descending by RSSI (strongest first)
        for (uint8_t i = 0; i < topCount; ++i) {
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < topCount; ++j) {
                if (topList[j].rssi > topList[i].rssi) {
                    const ScannedApEntry tmp = topList[i];
                    topList[i] = topList[j];
                    topList[j] = tmp;
                }
            }
        }
    }
    WiFi.scanDelete();

    {
        StateGuard lock(SystemState::instance());
        auto& net     = SystemState::instance().data().network;
        net.scanCount = topCount;
        for (size_t i = 0; i < NetworkData::MAX_SCAN_RESULTS; ++i) {
            net.scanResults[i] = (i < topCount) ? topList[i] : ScannedApEntry{};
        }
    }

    if (_roamAfterScan && !_softApRunning) {
        _roamAfterScan = false;
        beginRoamFromScan();
    }
}

void NetworkManagerService::beginRoamFromScan() {
    _roamQueueCount = 0;
    _roamCursor     = 0;

    ScannedApEntry scanned[NetworkData::MAX_SCAN_RESULTS] = {};
    uint8_t scanCnt = 0;
    {
        StateGuard lock(SystemState::instance());
        const auto& net = SystemState::instance().data().network;
        scanCnt = net.scanCount;
        memcpy(scanned, net.scanResults, sizeof(scanned));
    }

    // Match scanned APs (already sorted by strongest RSSI) against saved 5-slot table
    for (uint8_t s = 0; s < scanCnt && _roamQueueCount < NetConfig::MAX_SAVED_NETWORKS; ++s) {
        for (uint8_t i = 0; i < _savedCount; ++i) {
            if (_saved[i].inUse && strcmp(scanned[s].ssid, _saved[i].ssid) == 0) {
                _roamQueue[_roamQueueCount].savedIdx = i;
                _roamQueue[_roamQueueCount].rssi     = scanned[s].rssi;
                _roamQueueCount++;
                break;
            }
        }
    }

    if (_roamQueueCount > 0) {
        tryNextRoamCandidate();
    } else {
        syncNetworkStateSnapshot("Saved Wi-Fi out of range");
    }
}

bool NetworkManagerService::tryNextRoamCandidate() {
    if (_roamCursor >= _roamQueueCount) {
        syncNetworkStateSnapshot("Connection failed, retrying...");
        return false;
    }

    const uint8_t slotIdx = _roamQueue[_roamCursor++].savedIdx;
    if (slotIdx >= _savedCount || !_saved[slotIdx].inUse) {
        return false;
    }

    const char* ssid = _saved[slotIdx].ssid;
    const char* pwd  = _saved[slotIdx].password;

    Serial.printf("[NET] Roaming to best-RSSI saved AP: '%s'\n", ssid);
    if (pwd[0] != '\0') {
        WiFi.begin(ssid, pwd);
    } else {
        WiFi.begin(ssid);
    }

    _staConnectStartMs = millis();
    char msg[64] = {};
    snprintf(msg, sizeof(msg), "Connecting %s...", ssid);
    {
        StateGuard lock(SystemState::instance());
        auto& net = SystemState::instance().data().network;
        if (!_softApRunning) {
            net.mode = WIFI_OP_STA_CONNECTING;
        }
        strncpy(net.statusText, msg, sizeof(net.statusText) - 1);
    }
    return true;
}

bool NetworkManagerService::startSetupCaptivePortal(const char* optionalWpaPassword) {
    return startSoftApCommon(NetConfig::AP_SETUP_SSID, optionalWpaPassword, false);
}

bool NetworkManagerService::startTrackShareHotspot(const char* optionalWpaPassword) {
    return startSoftApCommon(NetConfig::AP_SHARE_SSID, optionalWpaPassword, true);
}

bool NetworkManagerService::startSoftApCommon(const char* ssid, const char* password, bool isShareMode) {
    if (!_hwInitialized && !ensureEsp32C6PowerAndSdioReady()) {
        return false;
    }

    if (_softApRunning) {
        _dnsServer.stop();
        _webServer.stop();
        WiFi.softAPdisconnect(false);
        _softApRunning = false;
    }

    _isShareHotspot       = isShareMode;
    _portalTestConnecting = false;
    _portalProvisionedMs  = 0;

    // Setup mode uses WIFI_AP_STA so phone stays on AP while scanning & testing STA login;
    // Share mode uses dedicated WIFI_AP with radio power-save disabled for max HTTP download speed!
    WiFi.mode(isShareMode ? WIFI_AP : WIFI_AP_STA);
    WiFi.setSleep(false);

    const IPAddress apIp(192, 168, 4, 1);
    const IPAddress netMask(255, 255, 255, 0);
    WiFi.softAPConfig(apIp, apIp, netMask);

    bool apStarted = false;
    if (password != nullptr && strlen(password) >= 8) {
        apStarted = WiFi.softAP(ssid, password, 6 /*channel*/, 0 /*visible*/, 4 /*max_conn*/);
    } else {
        apStarted = WiFi.softAP(ssid);
    }

    if (!apStarted) {
        Serial.printf("[NET] Failed to start SoftAP %s\n", ssid);
        return false;
    }

    // Start Captive Portal DNS catch-all on UDP port 53 ("*" -> 192.168.4.1)
    _dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    _dnsServer.setTTL(60);
    _dnsServer.start(NetConfig::DNS_PORT, "*", apIp);

    _webServer.begin();
    _softApRunning = true;

    char qrWifi[96] = {};
    formatWifiQrCode(ssid, password, qrWifi, sizeof(qrWifi));
    const char* qrUrl = isShareMode ? NetConfig::QR_SHARE_URL_PAYLOAD : NetConfig::QR_SETUP_URL_PAYLOAD;

    {
        StateGuard lock(SystemState::instance());
        auto& net = SystemState::instance().data().network;
        net.mode                = isShareMode ? WIFI_OP_AP_TRACK_SHARE : WIFI_OP_AP_SETUP_PORTAL;
        net.softApActive        = true;
        net.isShareHotspot      = isShareMode;
        net.apStationCount      = 0;
        net.portalProvisionedOk = false;
        net.portalOkMillis      = 0;
        strncpy(net.apSsid, ssid, sizeof(net.apSsid) - 1);
        net.apSsid[sizeof(net.apSsid) - 1] = '\0';
        strncpy(net.apIp, NetConfig::AP_IP_ADDR, sizeof(net.apIp) - 1);
        strncpy(net.qrWifiConnect, qrWifi, sizeof(net.qrWifiConnect) - 1);
        net.qrWifiConnect[sizeof(net.qrWifiConnect) - 1] = '\0';
        strncpy(net.qrWebPortalUrl, qrUrl, sizeof(net.qrWebPortalUrl) - 1);
        net.qrWebPortalUrl[sizeof(net.qrWebPortalUrl) - 1] = '\0';
        snprintf(net.statusText, sizeof(net.statusText),
                 isShareMode ? "Share Hotspot: %s" : "Setup Portal: %s", ssid);
    }

    if (!isShareMode) {
        triggerAsyncScan();
    }

    Serial.printf("[NET] SoftAP '%s' active @ 192.168.4.1 | QR1=%s | QR2=%s\n",
                  ssid, qrWifi, qrUrl);
    return true;
}

void NetworkManagerService::stopSoftApAndReturnToSta() {
    if (_softApRunning) {
        _dnsServer.stop();
        _webServer.stop();
        WiFi.softAPdisconnect(false);
        _softApRunning       = false;
        _isShareHotspot      = false;
        _portalTestConnecting = false;
        _portalProvisionedMs = 0;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);

    const bool alreadyConn = (WiFi.status() == WL_CONNECTED);
    {
        StateGuard lock(SystemState::instance());
        auto& net               = SystemState::instance().data().network;
        net.softApActive        = false;
        net.apStationCount      = 0;
        net.portalProvisionedOk = false;
        net.mode                = alreadyConn ? WIFI_OP_STA_CONNECTED : WIFI_OP_OFF;
    }

    if (!alreadyConn && _savedCount > 0) {
        _roamAfterScan = true;
        triggerAsyncScan();
    } else {
        syncNetworkStateSnapshot();
    }
}

void NetworkManagerService::update() {
    // 1. Consume Cross-Thread UI / System Commands
    bool cmdSetupAp     = false;
    bool cmdShareAp     = false;
    bool cmdStopAp      = false;
    bool cmdReconnect   = false;
    bool cmdScan        = false;
    int8_t cmdDelSlot   = -1;

    {
        StateGuard lock(SystemState::instance());
        auto& st  = SystemState::instance().data();
        auto& net = st.network;

        if (net.cmdStartSetupAp) {
            cmdSetupAp = true;
            net.cmdStartSetupAp = false;
        }
        if (net.cmdStartShareAp) {
            cmdShareAp = true;
            net.cmdStartShareAp = false;
        }
        if (net.cmdStopAp) {
            cmdStopAp = true;
            net.cmdStopAp = false;
        }
        if (net.cmdReconnectSta) {
            cmdReconnect = true;
            net.cmdReconnectSta = false;
        }
        if (net.cmdScanWifi) {
            cmdScan = true;
            net.cmdScanWifi = false;
        }
        if (net.cmdDeleteSavedIdx >= 0) {
            cmdDelSlot = net.cmdDeleteSavedIdx;
            net.cmdDeleteSavedIdx = -1;
        }
    }

    if (cmdDelSlot >= 0 && cmdDelSlot < static_cast<int8_t>(NetConfig::MAX_SAVED_NETWORKS)) {
        deleteWifiCredential(static_cast<uint8_t>(cmdDelSlot));
    }
    if (cmdStopAp) {
        stopSoftApAndReturnToSta();
    } else if (cmdSetupAp) {
        startSetupCaptivePortal();
    } else if (cmdShareAp) {
        startTrackShareHotspot();
    } else if (cmdReconnect) {
        if (_softApRunning) {
            stopSoftApAndReturnToSta();
        } else {
            WiFi.disconnect(false, false);
            _roamAfterScan = true;
            triggerAsyncScan();
        }
    } else if (cmdScan) {
        triggerAsyncScan();
    }

    if (!_hwInitialized) {
        return;
    }

    // 2. Service Captive Portal DNS + HTTP Web Server when SoftAP is active
    if (_softApRunning) {
        _dnsServer.processNextRequest();
        _webServer.handleClient();

        // Check Setup Portal provisioning verification & 3-second auto-close (SPEC §4)
        if (!_isShareHotspot && _portalTestConnecting) {
            if (WiFi.status() == WL_CONNECTED) {
                _portalTestConnecting = false;
                _portalProvisionedMs  = millis();
                {
                    StateGuard lock(SystemState::instance());
                    auto& net               = SystemState::instance().data().network;
                    net.portalProvisionedOk = true;
                    net.portalOkMillis      = _portalProvisionedMs;
                }
                syncNetworkStateSnapshot("Provisioned OK!");
            } else if ((millis() - _staConnectStartMs) >= STA_CONNECT_TIMEOUT_MS) {
                _portalTestConnecting = false;
                syncNetworkStateSnapshot("Auth failed / timeout");
            }
        }

        if (!_isShareHotspot && _portalProvisionedMs != 0 &&
            (millis() - _portalProvisionedMs) >= PORTAL_AUTOCLOSE_MS) {
            stopSoftApAndReturnToSta();
        }
    }

    // 3. Poll Non-Blocking Wi-Fi Scan & Roaming State Machine
    pollAsyncScanResults();
    handleStaConnectionLifecycle();
    checkNtpSyncProgress();

    // 4. Periodic 1s Telemetry Sync (RSSI, IP, AP Station Count)
    const uint32_t nowMs = millis();
    if ((nowMs - _lastTelemetrySyncMs) >= 1000UL) {
        _lastTelemetrySyncMs = nowMs;
        syncNetworkStateSnapshot();
    }
}

void NetworkManagerService::handleStaConnectionLifecycle() {
    if (_softApRunning) {
        return;
    }

    const wl_status_t st = WiFi.status();
    const uint32_t nowMs = millis();

    if (st == WL_CONNECTED) {
        _roamQueueCount = 0;
        _roamCursor     = 0;
        if (!_ntpRequested) {
            configTzTime("UTC0",
                         NetConfig::NTP_SERVER_1,
                         NetConfig::NTP_SERVER_2,
                         NetConfig::NTP_SERVER_3);
            _ntpRequested = true;
        }
        return;
    }

    _ntpRequested = false;

    // Step to next matched candidate if current AP attempt timed out
    if (_staConnectStartMs != 0 && (nowMs - _staConnectStartMs) >= STA_CONNECT_TIMEOUT_MS) {
        _staConnectStartMs = 0;
        if (_roamCursor < _roamQueueCount) {
            tryNextRoamCandidate();
            return;
        }
        _lastReconnectScanMs = nowMs;
    }

    // Periodic background roam scan when disconnected and credentials exist
    if (!_scanInProgress && _staConnectStartMs == 0 && _savedCount > 0) {
        if (_lastReconnectScanMs == 0 ||
            (nowMs - _lastReconnectScanMs) >= NetConfig::WIFI_RECONNECT_MS) {
            _lastReconnectScanMs = nowMs;
            _roamAfterScan       = true;
            triggerAsyncScan();
        }
    }
}

void NetworkManagerService::checkNtpSyncProgress() {
    if (_ntpSynced || WiFi.status() != WL_CONNECTED) {
        return;
    }
    const time_t nowSec = time(nullptr);
    if (nowSec > 1700000000) {
        _ntpSynced = true;
        {
            StateGuard lock(SystemState::instance());
            SystemState::instance().data().network.ntpSynced = true;
        }
        if (_ntpSyncCb) {
            _ntpSyncCb(static_cast<uint32_t>(nowSec));
        }
    }
}

void NetworkManagerService::syncNetworkStateSnapshot(const char* statusOverride) {
    const bool staOk = (WiFi.status() == WL_CONNECTED);
    String ssidStr   = staOk ? WiFi.SSID() : String("");
    String ipStr     = staOk ? WiFi.localIP().toString() : String("0.0.0.0");
    int8_t rssi      = staOk ? WiFi.RSSI() : -100;
    uint8_t apClients = _softApRunning ? WiFi.softAPgetStationNum() : 0;

    StateGuard lock(SystemState::instance());
    auto& net = SystemState::instance().data().network;

    net.staConnected   = staOk;
    net.rssiDbm        = rssi;
    net.softApActive   = _softApRunning;
    net.isShareHotspot = _isShareHotspot;
    net.apStationCount = apClients;

    strncpy(net.connectedSsid, ssidStr.c_str(), sizeof(net.connectedSsid) - 1);
    net.connectedSsid[sizeof(net.connectedSsid) - 1] = '\0';
    strncpy(net.staIp, ipStr.c_str(), sizeof(net.staIp) - 1);
    net.staIp[sizeof(net.staIp) - 1] = '\0';

    if (_softApRunning) {
        net.mode = _isShareHotspot ? WIFI_OP_AP_TRACK_SHARE : WIFI_OP_AP_SETUP_PORTAL;
    } else if (_scanInProgress) {
        net.mode = WIFI_OP_STA_SCANNING;
    } else if (staOk) {
        net.mode = WIFI_OP_STA_CONNECTED;
    } else if (_staConnectStartMs != 0) {
        net.mode = WIFI_OP_STA_CONNECTING;
    } else {
        net.mode = WIFI_OP_OFF;
    }

    if (statusOverride != nullptr && statusOverride[0] != '\0') {
        strncpy(net.statusText, statusOverride, sizeof(net.statusText) - 1);
        net.statusText[sizeof(net.statusText) - 1] = '\0';
    } else if (staOk) {
        snprintf(net.statusText, sizeof(net.statusText), "Connected: %s (%d dBm)",
                 net.connectedSsid, static_cast<int>(rssi));
    } else if (!_softApRunning && _savedCount == 0) {
        strncpy(net.statusText, "Unconfigured - Tap Setup", sizeof(net.statusText) - 1);
    }
}

// =============================================================================
// Captive Portal & Wireless Track Export WebServer Routes
// =============================================================================

void NetworkManagerService::setupWebRoutes() {
    _webServer.on("/", HTTP_GET, [this]() { handleWebRoot(); });
    _webServer.on("/setup", HTTP_GET, [this]() {
        _webServer.send_P(200, "text/html; charset=utf-8", kSetupPortalHtml);
    });
    _webServer.on("/tracks", HTTP_GET, [this]() {
        _webServer.send_P(200, "text/html; charset=utf-8", kShareTracksHtml);
    });

    // Common OS Captive Portal Detection Probes (iOS / Android / HarmonyOS / Windows)
    auto sendPortalRedirect = [this]() {
        const char* target = _isShareHotspot
                                 ? NetConfig::QR_SHARE_URL_PAYLOAD
                                 : NetConfig::QR_SETUP_URL_PAYLOAD;
        _webServer.sendHeader("Location", target, true);
        _webServer.send(302, "text/plain", "");
    };
    _webServer.on("/generate_204", HTTP_GET, sendPortalRedirect);
    _webServer.on("/gen_204", HTTP_GET, sendPortalRedirect);
    _webServer.on("/hotspot-detect.html", HTTP_GET, sendPortalRedirect);
    _webServer.on("/library/test/success.html", HTTP_GET, sendPortalRedirect);
    _webServer.on("/ncsi.txt", HTTP_GET, sendPortalRedirect);
    _webServer.on("/connecttest.txt", HTTP_GET, sendPortalRedirect);
    _webServer.on("/redirect", HTTP_GET, sendPortalRedirect);

    // REST JSON APIs
    _webServer.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });
    _webServer.on("/api/scan", HTTP_GET, [this]() { handleApiScan(); });
    _webServer.on("/api/saved", HTTP_GET, [this]() { handleApiSaved(); });
    _webServer.on("/api/connect", HTTP_POST, [this]() { handleApiConnect(); });
    _webServer.on("/api/delete", HTTP_POST, [this]() { handleApiDelete(); });

    _webServer.on("/api/cloud/config", HTTP_GET, [this]() { handleApiCloudConfigGet(); });
    _webServer.on("/api/cloud/config", HTTP_POST, [this]() { handleApiCloudConfigPost(); });
    _webServer.on("/api/onedrive/status", HTTP_GET, [this]() { handleApiOneDriveStatus(); });
    _webServer.on("/api/onedrive/action", HTTP_POST, [this]() { handleApiOneDriveAction(); });

    _webServer.on("/api/tracks", HTTP_GET, [this]() { handleApiTracksList(); });
    _webServer.on("/api/download", HTTP_GET, [this]() { handleApiTrackDownload(); });
    _webServer.on("/api/tracks/delete", HTTP_POST, [this]() { handleApiTrackDelete(); });
    _webServer.on("/api/sd/format", HTTP_POST, [this]() { handleApiSdFormat(); });

    _webServer.on("/api/tunnel/info", HTTP_GET, [this]() { handleApiTunnelInfo(); });
    _webServer.on("/api/tunnel/upload", HTTP_POST,
                  [this]() { handleApiTunnelUploadDone(); },
                  [this]() { handleApiTunnelUploadChunk(); });

    _webServer.onNotFound([this]() {
        if (!redirectIfCaptiveProbe()) {
            handleWebRoot();
        }
    });
}

bool NetworkManagerService::redirectIfCaptiveProbe() {
    const String host = _webServer.hostHeader();
    if (!host.isEmpty() && !host.startsWith("192.168.4.1")) {
        const char* target = _isShareHotspot
                                 ? NetConfig::QR_SHARE_URL_PAYLOAD
                                 : NetConfig::QR_SETUP_URL_PAYLOAD;
        _webServer.sendHeader("Location", target, true);
        _webServer.send(302, "text/plain", "");
        return true;
    }
    return false;
}

void NetworkManagerService::handleWebRoot() {
    if (redirectIfCaptiveProbe()) {
        return;
    }
    if (_isShareHotspot) {
        _webServer.send_P(200, "text/html; charset=utf-8", kShareTracksHtml);
    } else {
        _webServer.send_P(200, "text/html; charset=utf-8", kSetupPortalHtml);
    }
}

void NetworkManagerService::handleApiStatus() {
    JsonDocument doc;
    const bool conn = (WiFi.status() == WL_CONNECTED);
    doc["connected"] = conn;
    doc["ssid"]      = conn ? WiFi.SSID() : String("");
    doc["ip"]        = conn ? WiFi.localIP().toString() : String("0.0.0.0");
    doc["rssi"]      = conn ? static_cast<int>(WiFi.RSSI()) : -100;
    doc["shareMode"] = _isShareHotspot;

    {
        StateGuard lock(SystemState::instance());
        const auto& net = SystemState::instance().data().network;
        doc["provisionedOk"] = net.portalProvisionedOk;
        doc["statusText"]    = net.statusText;
    }

    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

void NetworkManagerService::handleApiScan() {
    if (!_isShareHotspot && !_scanInProgress) {
        // Trigger fresh scan if previous results are empty
        uint8_t existingCnt = 0;
        {
            StateGuard lock(SystemState::instance());
            existingCnt = SystemState::instance().data().network.scanCount;
        }
        if (existingCnt == 0) {
            const int16_t n = WiFi.scanNetworks(false /*sync*/, false, false, 200U);
            if (n >= 0) {
                _scanInProgress = true;
                pollAsyncScanResults();
            }
        } else {
            triggerAsyncScan();
        }
    }

    JsonDocument doc;
    JsonArray aps = doc["aps"].to<JsonArray>();
    {
        StateGuard lock(SystemState::instance());
        const auto& net = SystemState::instance().data().network;
        for (uint8_t i = 0; i < net.scanCount && i < NetworkData::MAX_SCAN_RESULTS; ++i) {
            JsonObject o = aps.add<JsonObject>();
            o["ssid"] = net.scanResults[i].ssid;
            o["rssi"] = net.scanResults[i].rssi;
            o["auth"] = net.scanResults[i].authMode;
        }
    }

    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

void NetworkManagerService::handleApiSaved() {
    JsonDocument doc;
    JsonArray arr = doc["saved"].to<JsonArray>();
    for (uint8_t i = 0; i < _savedCount; ++i) {
        if (_saved[i].inUse) {
            JsonObject o = arr.add<JsonObject>();
            o["index"] = i;
            o["ssid"]  = _saved[i].ssid;
        }
    }
    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

void NetworkManagerService::handleApiConnect() {
    String ssid, pwd;
    if (_webServer.hasArg("plain")) {
        JsonDocument doc;
        if (!deserializeJson(doc, _webServer.arg("plain"))) {
            ssid = doc["ssid"] | "";
            pwd  = doc["pwd"] | "";
        }
    } else {
        ssid = _webServer.arg("ssid");
        pwd  = _webServer.arg("pwd");
    }
    ssid.trim();

    if (ssid.isEmpty() || ssid.length() > 32) {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid SSID\"}");
        return;
    }

    saveWifiCredential(ssid.c_str(), pwd.c_str());

    if (pwd.length() > 0) {
        WiFi.begin(ssid.c_str(), pwd.c_str());
    } else {
        WiFi.begin(ssid.c_str());
    }
    _staConnectStartMs    = millis();
    _portalTestConnecting = true;

    _webServer.send(200, "application/json",
                    "{\"success\":true,\"message\":\"Connecting and verifying Wi-Fi...\"}");
}

void NetworkManagerService::handleApiDelete() {
    bool ok = false;
    if (_webServer.hasArg("plain")) {
        JsonDocument doc;
        if (!deserializeJson(doc, _webServer.arg("plain"))) {
            if (!doc["index"].isNull()) {
                const int idx = doc["index"] | -1;
                if (idx >= 0 && idx < static_cast<int>(NetConfig::MAX_SAVED_NETWORKS)) {
                    ok = deleteWifiCredential(static_cast<uint8_t>(idx));
                }
            } else if (!doc["ssid"].isNull()) {
                const char* s = doc["ssid"] | "";
                ok = deleteWifiCredentialBySsid(s);
            }
        }
    }
    _webServer.send(200, "application/json", ok ? "{\"success\":true}" : "{\"success\":false}");
}

void NetworkManagerService::handleApiCloudConfigGet() {
    JsonDocument doc;
    {
        StateGuard lock(SystemState::instance());
        const auto& s = SystemState::instance().data().settings;
        doc["assistNowToken"]   = s.assistNowToken;
        doc["oneDriveClientId"] = s.oneDriveClientId;
        doc["oneDriveTenant"]   = s.oneDriveTenant;
        doc["oneDriveAutoSync"] = s.oneDriveAutoSync;
    }
    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

void NetworkManagerService::handleApiCloudConfigPost() {
    if (!_webServer.hasArg("plain")) {
        _webServer.send(400, "application/json", "{\"success\":false}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _webServer.arg("plain"))) {
        _webServer.send(400, "application/json", "{\"success\":false}");
        return;
    }

    const char* agpsTok = doc["assistNowToken"] | "";
    const char* odCid   = doc["oneDriveClientId"] | "";
    const char* odTen   = doc["oneDriveTenant"] | "common";

    Preferences prefs;
    if (prefs.begin(NvsConfig::NAMESPACE, false)) {
        prefs.putString(NvsConfig::KEY_AGPS_TOKEN, agpsTok);
        prefs.putString(NvsConfig::KEY_OD_CLIENT_ID, odCid);
        prefs.putString(NvsConfig::KEY_OD_TENANT, odTen[0] ? odTen : "common");
        prefs.end();
    }

    {
        StateGuard lock(SystemState::instance());
        auto& st = SystemState::instance().data();
        strncpy(st.settings.assistNowToken, agpsTok, sizeof(st.settings.assistNowToken) - 1);
        st.settings.assistNowToken[sizeof(st.settings.assistNowToken) - 1] = '\0';
        strncpy(st.settings.oneDriveClientId, odCid, sizeof(st.settings.oneDriveClientId) - 1);
        st.settings.oneDriveClientId[sizeof(st.settings.oneDriveClientId) - 1] = '\0';
        strncpy(st.settings.oneDriveTenant, odTen[0] ? odTen : "common",
                sizeof(st.settings.oneDriveTenant) - 1);
        st.settings.oneDriveTenant[sizeof(st.settings.oneDriveTenant) - 1] = '\0';
    }

    _webServer.send(200, "application/json", "{\"success\":true}");
}

void NetworkManagerService::handleApiOneDriveStatus() {
    JsonDocument doc;
    {
        StateGuard lock(SystemState::instance());
        const auto& od = SystemState::instance().data().oneDrive;
        doc["isBound"]            = od.isBound;
        doc["bindState"]          = static_cast<uint8_t>(od.bindState);
        doc["transferState"]      = static_cast<uint8_t>(od.transferState);
        doc["autoSync"]           = od.autoSyncEnabled;
        doc["accountEmail"]       = od.accountEmail;
        doc["userCode"]           = od.userCode;
        doc["verificationUri"]    = od.verificationUri;
        doc["pendingFiles"]       = od.pendingFiles;
        doc["syncedTotalFiles"]   = od.syncedTotalFiles;
        doc["currentFilePercent"] = od.currentFilePercent;
        doc["currentFileName"]    = od.currentFileName;
        doc["lastError"]          = od.lastError;
    }
    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

void NetworkManagerService::handleApiOneDriveAction() {
    if (!_webServer.hasArg("plain")) {
        _webServer.send(400, "application/json", "{\"success\":false}");
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, _webServer.arg("plain"))) {
        _webServer.send(400, "application/json", "{\"success\":false}");
        return;
    }
    const char* action = doc["action"] | "";
    {
        StateGuard lock(SystemState::instance());
        auto& od = SystemState::instance().data().oneDrive;
        if (strcmp(action, "bind") == 0) {
            od.cmdStartDeviceCode = true;
        } else if (strcmp(action, "sync") == 0) {
            od.cmdSyncNow = true;
        } else if (strcmp(action, "unbind") == 0) {
            od.cmdUnbind = true;
        } else if (strcmp(action, "autosync") == 0) {
            od.requestedAutoSyncVal = doc["enabled"] | true;
            od.cmdSetAutoSync       = true;
        }
    }
    _webServer.send(200, "application/json", "{\"success\":true}");
}

void NetworkManagerService::handleApiTracksList() {
    uint64_t totalBytes  = 0;
    uint64_t usedBytes   = 0;
    uint64_t tracksBytes = 0;
    float    quotaRatio  = 0.0f;
    bool     sdMounted   = false;
    bool     isRec       = false;
    char     activePaths[6][48] = {};

    {
        StateGuard lock(SystemState::instance());
        const auto& s = SystemState::instance().data().storage;
        sdMounted   = s.isMounted;
        isRec       = s.isRecording;
        totalBytes  = s.totalBytes;
        usedBytes   = s.usedBytes;
        tracksBytes = s.tracksDirBytes;
        quotaRatio  = s.tracksQuotaRatio;
        strncpy(activePaths[0], s.activeSingleWgsPath, sizeof(activePaths[0]) - 1);
        strncpy(activePaths[1], s.activeSingleGcjPath, sizeof(activePaths[1]) - 1);
        strncpy(activePaths[2], s.activeDailyWgsPath,  sizeof(activePaths[2]) - 1);
        strncpy(activePaths[3], s.activeDailyGcjPath,  sizeof(activePaths[3]) - 1);
        strncpy(activePaths[4], s.activeSubWgsPath,    sizeof(activePaths[4]) - 1);
        strncpy(activePaths[5], s.activeSubGcjPath,    sizeof(activePaths[5]) - 1);
    }

    auto checkActive = [&](const String& rel) -> bool {
        if (!isRec) return false;
        for (size_t i = 0; i < 6; ++i) {
            if (activePaths[i][0] != '\0' && rel.equalsIgnoreCase(activePaths[i])) {
                return true;
            }
        }
        return false;
    };

    JsonDocument doc;
    doc["mounted"]     = sdMounted;
    doc["totalBytes"]  = totalBytes;
    doc["usedBytes"]   = usedBytes;
    doc["tracksBytes"] = tracksBytes;
    doc["quotaRatio"]  = quotaRatio;

    JsonArray filesArr = doc["files"].to<JsonArray>();

    if (_sd != nullptr && sdMounted && lockSd(2000)) {
        struct ScanDir {
            const char* fullDir;
            const char* category;
        };
        static constexpr ScanDir kDirs[3] = {
            { SdCardConfig::DIR_SINGLE, "single" },
            { SdCardConfig::DIR_DAILY,  "daily"  },
            { SdCardConfig::DIR_SUB,    "sub"    }
        };

        for (const auto& d : kDirs) {
            FsFile dir;
            if (!dir.open(_sd, d.fullDir, O_RDONLY)) {
                continue;
            }
            FsFile entry;
            char nameBuf[64] = {};
            while (entry.openNext(&dir, O_RDONLY)) {
                const bool isReg = entry.isFile() && !entry.isHidden();
                const uint64_t sz = entry.fileSize();
                nameBuf[0] = '\0';
                if (isReg) {
                    entry.getName(nameBuf, sizeof(nameBuf));
                }
                entry.close();

                if (!isReg || nameBuf[0] == '\0' || nameBuf[0] == '.') {
                    continue;
                }
                const String nameStr(nameBuf);
                if (!endsWithCi(nameStr, ".gpx")) {
                    continue;
                }

                String rel = String(d.category) + "/" + nameStr;
                const bool isGcj = (nameBuf[0] == 'C' || nameBuf[0] == 'c');

                JsonObject fObj = filesArr.add<JsonObject>();
                fObj["name"]     = nameBuf;
                fObj["path"]     = rel;
                fObj["category"] = d.category;
                fObj["size"]     = sz;
                fObj["type"]     = isGcj ? "gcj02" : "wgs84";
                fObj["badge"]    = isGcj ? "国内/高德/两步路版" : "国际/Google Earth版";
                fObj["active"]   = checkActive(rel);
            }
            dir.close();
        }
        unlockSd();
    }

    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

bool NetworkManagerService::resolveSafeTrackPath(const String& reqFile,
                                          String& outFullPath,
                                          String& outRelPath) const {
    String clean = WebServer::urlDecode(reqFile);
    clean.trim();
    while (clean.startsWith("/")) {
        clean.remove(0, 1);
    }
    if (clean.startsWith("tracks/")) {
        clean.remove(0, 7);
    }

    // Strict directory traversal & control-char defense
    if (clean.isEmpty() || clean.indexOf("..") >= 0 || clean.indexOf('\\') >= 0) {
        return false;
    }

    if (clean == "trips.csv") {
        outFullPath = SdCardConfig::FILE_TRIPS_CSV;
        outRelPath  = "trips.csv";
        return true;
    }
    if (clean == "power.log") {
        outFullPath = SdCardConfig::FILE_POWER_LOG;
        outRelPath  = "power.log";
        return true;
    }

    if (clean.startsWith("single/") || clean.startsWith("daily/") || clean.startsWith("sub/")) {
        outFullPath = String(SdCardConfig::DIR_TRACKS_ROOT) + "/" + clean;
        outRelPath  = clean;
        return true;
    }

    // Bare GPX filename fallback (SPEC §4 ?file=W-20261030-090000.gpx): probe single, daily, sub, root
    if (clean.indexOf('/') < 0 && endsWithCi(clean, ".gpx") && _sd != nullptr) {
        static constexpr const char* kProbeSubdirs[4] = { "single", "daily", "sub", "" };
        if (lockSd(1500)) {
            for (const char* sub : kProbeSubdirs) {
                String candidateFull = SdCardConfig::DIR_TRACKS_ROOT;
                candidateFull += "/";
                if (sub[0] != '\0') {
                    candidateFull += sub;
                    candidateFull += "/";
                }
                candidateFull += clean;

                FsFile testFile;
                if (testFile.open(_sd, candidateFull.c_str(), O_RDONLY)) {
                    const bool ok = testFile.isFile();
                    testFile.close();
                    if (ok) {
                        unlockSd();
                        outFullPath = candidateFull;
                        outRelPath  = (sub[0] != '\0') ? (String(sub) + "/" + clean) : clean;
                        return true;
                    }
                }
            }
            unlockSd();
        }
    }
    return false;
}

void NetworkManagerService::handleApiTrackDownload() {
    if (_sd == nullptr || !_webServer.hasArg("file")) {
        _webServer.send(400, "text/plain", "Missing file parameter or SD unavailable");
        return;
    }

    String fullPath, relPath;
    if (!resolveSafeTrackPath(_webServer.arg("file"), fullPath, relPath)) {
        _webServer.send(404, "text/plain", "File not found");
        return;
    }

    uint64_t totalSize = 0;
    bool fileExists    = false;
    if (!lockSd(2000)) {
        _webServer.send(503, "text/plain", "SD busy");
        return;
    }
    {
        FsFile f;
        if (f.open(_sd, fullPath.c_str(), O_RDONLY)) {
            fileExists = f.isFile();
            totalSize  = f.fileSize();
            f.close();
        }
    }
    unlockSd();

    if (!fileExists) {
        _webServer.send(404, "text/plain", "Track file does not exist");
        return;
    }

    // Determine filename & MIME type
    int slashIdx = relPath.lastIndexOf('/');
    String dlName = (slashIdx >= 0) ? relPath.substring(slashIdx + 1) : relPath;
    const char* mime = "application/octet-stream";
    if (endsWithCi(dlName, ".gpx")) {
        mime = "application/gpx+xml";
    } else if (endsWithCi(dlName, ".csv")) {
        mime = "text/csv; charset=utf-8";
    } else if (endsWithCi(dlName, ".log")) {
        mime = "text/plain; charset=utf-8";
    }

    uint8_t* sliceBuf = static_cast<uint8_t*>(
        heap_caps_malloc(STREAM_SLICE_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (sliceBuf == nullptr) {
        sliceBuf = static_cast<uint8_t*>(malloc(4096UL));
    }
    const size_t bufCap = (sliceBuf != nullptr) ? STREAM_SLICE_BYTES : 0;
    if (sliceBuf == nullptr) {
        _webServer.send(500, "text/plain", "Out of memory");
        return;
    }

    _webServer.sendHeader("Content-Disposition",
                          String("attachment; filename=\"") + dlName + "\"", true);
    _webServer.sendHeader("Cache-Control", "no-store");
    _webServer.setContentLength(static_cast<size_t>(totalSize));
    _webServer.send(200, mime, "");

    NetworkClient client = _webServer.client();
    uint64_t offset = 0;

    // Stream in 16 KiB mutex-bounded slices so 1Hz TrackLogger writes never stall!
    while (offset < totalSize && client.connected()) {
        const uint64_t rem = totalSize - offset;
        const size_t toRead = (rem > bufCap) ? bufCap : static_cast<size_t>(rem);
        int bytesRead = -1;

        if (lockSd(2000)) {
            FsFile f;
            if (f.open(_sd, fullPath.c_str(), O_RDONLY)) {
                if (f.seekSet(offset)) {
                    bytesRead = f.read(sliceBuf, toRead);
                }
                f.close();
            }
            unlockSd();
        }

        if (bytesRead <= 0) {
            break;
        }

        size_t sentTotal = 0;
        while (sentTotal < static_cast<size_t>(bytesRead) && client.connected()) {
            const size_t w = client.write(sliceBuf + sentTotal,
                                          static_cast<size_t>(bytesRead) - sentTotal);
            if (w == 0) {
                vTaskDelay(pdMS_TO_TICKS(5));
                const size_t w2 = client.write(sliceBuf + sentTotal,
                                               static_cast<size_t>(bytesRead) - sentTotal);
                if (w2 == 0) {
                    sentTotal = 0;
                    break;
                }
                sentTotal += w2;
            } else {
                sentTotal += w;
            }
        }
        if (sentTotal == 0) {
            break;
        }

        offset += static_cast<uint64_t>(bytesRead);
        vTaskDelay(1);
    }

    free(sliceBuf);
}

void NetworkManagerService::handleApiTrackDelete() {
    String reqPath;
    if (_webServer.hasArg("plain")) {
        JsonDocument doc;
        if (!deserializeJson(doc, _webServer.arg("plain"))) {
            reqPath = doc["path"] | "";
        }
    }
    String fullPath, relPath;
    if (!resolveSafeTrackPath(reqPath, fullPath, relPath) ||
        relPath == "trips.csv" || relPath == "power.log") {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid file\"}");
        return;
    }

    // Refuse deleting actively recorded GPX streams
    bool isActive = false;
    {
        StateGuard lock(SystemState::instance());
        const auto& s = SystemState::instance().data().storage;
        if (s.isRecording) {
            isActive = relPath.equalsIgnoreCase(s.activeSingleWgsPath) ||
                       relPath.equalsIgnoreCase(s.activeSingleGcjPath) ||
                       relPath.equalsIgnoreCase(s.activeDailyWgsPath)  ||
                       relPath.equalsIgnoreCase(s.activeDailyGcjPath)  ||
                       relPath.equalsIgnoreCase(s.activeSubWgsPath)    ||
                       relPath.equalsIgnoreCase(s.activeSubGcjPath);
        }
    }
    if (isActive) {
        _webServer.send(409, "application/json",
                        "{\"success\":false,\"message\":\"Cannot delete active recording stream\"}");
        return;
    }

    bool removed = false;
    if (_sd != nullptr && lockSd(2000)) {
        removed = _sd->remove(fullPath.c_str());
        unlockSd();
    } else {
        StateGuard lock(SystemState::instance());
        auto& s = SystemState::instance().data().storage;
        strncpy(s.cmdDeleteRelPath, relPath.c_str(), sizeof(s.cmdDeleteRelPath) - 1);
        s.cmdDeleteRelPath[sizeof(s.cmdDeleteRelPath) - 1] = '\0';
        removed = true;
    }

    if (removed) {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().storage.cmdRefreshStorage = true;
    }
    _webServer.send(200, "application/json",
                    removed ? "{\"success\":true}" : "{\"success\":false}");
}

void NetworkManagerService::handleApiSdFormat() {
    bool confirmed = false;
    if (_webServer.hasArg("plain")) {
        JsonDocument doc;
        if (!deserializeJson(doc, _webServer.arg("plain"))) {
            confirmed = doc["confirm"] | false;
        }
    }
    if (!confirmed) {
        _webServer.send(400, "application/json", "{\"success\":false,\"message\":\"Confirm required\"}");
        return;
    }

    {
        StateGuard lock(SystemState::instance());
        SystemState::instance().data().storage.cmdFormatFat32 = true;
    }
    _webServer.send(200, "application/json",
                    "{\"success\":true,\"message\":\"SD FAT32 format scheduled\"}");
}

void NetworkManagerService::handleApiTunnelInfo() {
    JsonDocument doc;
    {
        StateGuard lock(SystemState::instance());
        const auto& t = SystemState::instance().data().tunnelDb;
        doc["version"] = t.version;
        doc["count"]   = t.tunnelCount;
        doc["loaded"]  = t.loadedInPsram;
        doc["source"]  = "LittleFS";
    }
    String out;
    serializeJson(doc, out);
    _webServer.send(200, "application/json", out);
}

void NetworkManagerService::handleApiTunnelUploadChunk() {
    HTTPUpload& up = _webServer.upload();
    if (up.status == UPLOAD_FILE_START) {
        _tunnelUploadOk    = false;
        _tunnelUploadBytes = 0;
        bool lfsOk = false;
        {
            StateGuard lock(SystemState::instance());
            lfsOk = SystemState::instance().data().boot.littlefsOk;
        }
        if (lfsOk) {
            _tunnelUploadFile = LittleFS.open(LittleFsConfig::TUNNELS_BIN_REL, "w");
            _tunnelUploadOk   = static_cast<bool>(_tunnelUploadFile);
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (_tunnelUploadOk && _tunnelUploadFile && up.currentSize > 0) {
            const size_t w = _tunnelUploadFile.write(up.buf, up.currentSize);
            if (w == up.currentSize) {
                _tunnelUploadBytes += w;
            } else {
                _tunnelUploadOk = false;
            }
        }
    } else if (up.status == UPLOAD_FILE_END) {
        if (_tunnelUploadFile) {
            _tunnelUploadFile.close();
        }
        if (_tunnelUploadOk && _tunnelUploadBytes > 0) {
            StateGuard lock(SystemState::instance());
            SystemState::instance().data().tunnelDb.cmdReloadFromFlash = true;
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        if (_tunnelUploadFile) {
            _tunnelUploadFile.close();
        }
        _tunnelUploadOk = false;
    }
}

void NetworkManagerService::handleApiTunnelUploadDone() {
    // Also support raw binary POST body when not sent as multipart/form-data
    if (!_tunnelUploadOk && _webServer.hasArg("plain")) {
        const String rawBody = _webServer.arg("plain");
        bool lfsOk = false;
        {
            StateGuard lock(SystemState::instance());
            lfsOk = SystemState::instance().data().boot.littlefsOk;
        }
        if (lfsOk && rawBody.length() > 0) {
            fs::File f = LittleFS.open(LittleFsConfig::TUNNELS_BIN_REL, "w");
            if (f) {
                const size_t w = f.write(reinterpret_cast<const uint8_t*>(rawBody.c_str()),
                                         rawBody.length());
                f.close();
                if (w == rawBody.length()) {
                    _tunnelUploadOk    = true;
                    _tunnelUploadBytes = w;
                    StateGuard lock(SystemState::instance());
                    SystemState::instance().data().tunnelDb.cmdReloadFromFlash = true;
                }
            }
        }
    }

    uint16_t count = 0;
    {
        StateGuard lock(SystemState::instance());
        count = SystemState::instance().data().tunnelDb.tunnelCount;
    }

    JsonDocument doc;
    doc["success"] = _tunnelUploadOk;
    doc["bytes"]   = _tunnelUploadBytes;
    doc["count"]   = count;
    doc["message"] = _tunnelUploadOk ? "Tunnel DB Reloaded to PSRAM" : "Tunnel upload failed";

    String out;
    serializeJson(doc, out);
    _webServer.send(_tunnelUploadOk ? 200 : 500, "application/json", out);
}
