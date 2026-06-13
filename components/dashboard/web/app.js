/* ===== MatrixDebugBoard App ===== */
(function () {
'use strict';

const TABS = ['rs485', 'can', 'uart', 'flash', 'wifi', 'system', 'ota', 'ai'];
let curTab = localStorage.getItem('activeTab') || 'rs485';
let lang = localStorage.getItem('lang') || 'zh';
const _tabCache  = {};   // tab name → saved innerHTML
const _tabLoaded = new Set(); // tabs that have been fully loaded at least once

// ── Log persistence (survives page refresh; cleared when browser tab closes) ──
const _logBuf = { rs485: [], uart0: [], uart1: [], can: [] };
let _logRestoring = false;

function _logPush(ch, entry) {
  if (_logRestoring) return;
  const buf = _logBuf[ch];
  const max = ch === 'can' ? 500 : 1000;
  buf.push(entry);
  if (buf.length > max) buf.shift();
  try { sessionStorage.setItem('log_' + ch, JSON.stringify(buf)); } catch(e) {}
}

function _logClear(ch) {
  _logBuf[ch] = [];
  sessionStorage.removeItem('log_' + ch);
}

function _logRestore(ch) {
  const raw = sessionStorage.getItem('log_' + ch);
  if (!raw) return;
  try {
    const entries = JSON.parse(raw);
    _logBuf[ch] = entries;
    _logRestoring = true;
    if (ch === 'can') {
      entries.forEach(e => appendCanLog(e));
    } else {
      const port = ch === 'uart1' ? 1 : 0;
      entries.forEach(([ts, data, dir]) => {
        if (ch === 'rs485') appendRs485Log(ts, data, dir);
        else appendUartLog(port, ts, data, dir);
      });
    }
  } catch(e) {}
  _logRestoring = false;
}

// ── i18n ──
const I18N = {
  zh: {
    // tabs
    rs485:'RS485', can:'CAN', uart:'UART', flash:'文件管理', wifi:'WiFi', system:'系统', ota:'OTA升级',
    // loading
    loading:'加载中…', loadErr:'加载失败', retry:'重试', refresh:'刷新',
    // generic
    state:'状态', ok:'正常', error:'错误', send:'发送', clear:'清除', apply:'应用',
    export_log:'导出', export_csv:'导出 CSV',
    start_btn:'▶ 开始', stop_btn:'⏹ 停止', pause_btn:'⏸ 暂停', resume_btn:'▶ 继续',
    // serial common
    baud_rate:'波特率', data_bits:'数据位', parity_bits:'校验位', stop_bits:'停止位',
    disp_fmt:'显示格式', send_fmt:'发送格式', eol_ascii:'行尾 (ASCII)',
    port_cfg:'端口设置', txrx_log:'收发日志', auto_send:'自动发送',
    send_hist:'发送历史', no_hist:'暂无历史', frames:'帧',
    running:'运行中', not_ready:'未就绪', not_init:'未初始化',
    frame_fmt:'帧格式', tx_bytes:'TX', rx_bytes:'RX',
    // RS485
    rs485_title:'RS485 调试', rs485_sub:'UART2 · TX=IO8 · RX=IO9 · DE=IO46 · RE=IO16',
    crc16_btn:'CRC16', modbus_read:'Modbus读', modbus_write:'Modbus写',
    // CAN
    can_title:'CAN 总线调试', can_sub:'TWAI · TX=IO6 · RX=IO7 · ISO 11898',
    can_cfg:'CAN 设置', work_mode:'工作模式',
    send_frame:'发送帧', frame_id:'帧 ID (HEX)', data_hex:'数据 (HEX，≤8字节)',
    ext_frame:'扩展帧', rtr_frame:'RTR帧',
    frame_log:'帧日志', id_filter:'ID 过滤 (HEX)', filter_mask:'掩码 (HEX)',
    apply_filter:'应用过滤', recover_bus:'恢复总线', quick_tpl:'快速模板',
    tpl_none:'── 选择模板 ──',
    can_th_ts:'时间 (ms)', can_th_dir:'方向', can_th_id:'ID',
    can_th_type:'类型', can_th_dlc:'DLC', can_th_data:'数据 (HEX)',
    // UART
    uart_title:'UART 串口调试',
    uart_sub:'UART0: TX=IO43 RX=IO44  │  UART1: TX=IO17 RX=IO18',
    // Flash
    flash_title:'外部 SPI Flash',
    flash_sub:'W25Q128JVSIQ · 16 MB · SPI3 · MISO=IO35 CLK=IO36 MOSI=IO37 CS=IO38',
    flash_read:'读取', flash_write:'写入', flash_erase:'擦除',
    start_addr:'起始地址（HEX）', byte_len:'长度（字节，≤1024）',
    erase_type_lbl:'擦除类型', capacity:'容量', page_size:'页大小',
    sector_size:'扇区大小', block_size:'块大小', ready:'就绪',
    // WiFi
    wifi_title:'WiFi 网络', wifi_sub:'2.4 GHz · AP + STA 双模',
    ap_cfg_title:'热点配置', ap_cfg_sub:'修改 AP 模式的名称和密码',
    ssid_name_lbl:'热点名称 (SSID)', ap_pass_lbl:'热点密码（留空保持不变）',
    save_apply:'保存并应用',
    available_nets:'可用网络', connect_title:'连接',
    // System
    sys_info:'系统信息', term_res:'终端电阻控制', sys_ops:'系统操作',
    theme_title:'界面主题色',
    theme_help:'点击色块切换全局主视觉颜色，自动保存。',
    custom_color:'自定义',
    // OTA
    ota_fw_title:'固件版本', fw_cur:'当前版本', fw_srv:'服务器版本',
    upg_state:'升级状态', upg_notes:'更新说明', upg_progress:'升级进度',
    srv_cfg:'服务器配置', manifest_url_lbl:'检测地址 (manifest URL)',
    auto_check_lbl:'自动检测更新',
    interval_prefix:'每隔', interval_suffix:'分钟',
    save_cfg:'保存配置', check_update:'检测更新', update_now:'立即升级',
    ota_idle:'空闲', ota_checking:'检测中…', ota_up_to_date:'已是最新',
    ota_available:'有新版本', ota_updating:'升级中…',
    ota_success:'升级成功', ota_failed:'失败',
    // WiFi
    connect:'连接', disconnect:'断开', clearSaved:'清除已保存',
    ssid:'SSID', password:'密码', scan:'扫描', scanning:'扫描中…',
    connected:'已连接', disconnected:'未连接', connecting:'连接中', ap_mode:'AP模式',
    currentSsid:'当前 SSID', ip:'IP 地址', availableNetworks:'可用网络',
    encrypted:'加密', open:'开放', chip:'芯片', version:'版本',
    reboot:'重启设备', rebootConfirm:'确认重启？所有连接将断开。', rebooting:'重启中…',
    actions:'系统操作',
    // extra toast/confirm strings
    applied:'已应用', cfg_fail:'设置失败', save_fail:'保存失败', saved:'已保存',
    // HTML labels
    parity_none:'无 (None)', parity_even:'偶 (Even)', parity_odd:'奇 (Odd)',
    fmt_hex:'HEX 字节流', fmt_ascii:'ASCII 文本', eol_none:'无',
    can_mode_normal:'Normal（正常收发）', can_mode_listen:'Listen-Only（只收）', can_mode_self:'Self-Test（内部回环）',
    can_mode_help:'Self-Test：无需终端电阻，帧自发自收，适合单节点调试。  Listen-Only：仅接收，不干扰总线。',
    dlc_auto:'自动',
    filter_no:'不过滤',
    flash_click_hint:'（点击读取查看数据）',
    flash_write_warn:'写入前请先手动擦除目标扇区，否则只能将 1 改为 0。',
    flash_data_lbl:'数据（HEX，不含空格，如 DEADBEEF010203）',
    addr_lbl:'地址（HEX）',
    erase_sector:'Sector Erase（4 KB）', erase_block32:'Block Erase（32 KB）',
    erase_block64:'Block Erase（64 KB）', erase_chip:'Chip Erase（全片，耗时约 20s）',
    flash_chip_help:'Chip Erase 会弹出二次确认对话框，擦除全部 16 MB 数据。',
    ap_cfg_help:'修改后立即对新连接生效；已连接设备需重新扫描并加入新热点。',
    ap_pass_ph:'•••••••• (WPA2，至少 8 位)',
    term_res_help:'IO42 继电器控制 CAN 和 RS485 的 120Ω 终端电阻。打开：接入，关闭：断开。',
    term_res_120:'120Ω 终端电阻',
    reboot_help:'重启后设备将自动重新连接上次保存的 WiFi 网络，AP 热点始终保持开启。',
    ota_srv_help:'填写服务器上的版本检测文件地址（JSON）。',
    rs485_input_ph:'HEX: 01 03 00 00 00 01  或切换 ASCII 模式',
    uart_input_ph:'HEX: 01 02 03  或切换 ASCII 模式',
    ws_disconnected:'WebSocket 未连接',
    hist_fill:'填入输入框', hist_del:'删除',
    color_green:'绿色', color_blue:'蓝色', color_purple:'紫色',
    color_cyan:'青色', color_orange:'橙色', color_pink:'粉色',
    invalid_id:'ID 格式错误', bus_recovering:'总线恢复中…',
    max_1024:'最大 1024 字节', enter_hex:'请输入 HEX 数据',
    erase_chip_confirm:'确认全片擦除？此操作不可撤销，耗时约 20 秒。',
    erase_fail:'擦除失败', erase_done:'擦除完成',
    no_networks:'未发现网络', enter_ssid:'请输入 SSID',
    enter_ap_ssid:'请输入热点名称', pwd_min8:'密码至少 8 位',
    ap_saved:'热点配置已保存，新设备连接时生效',
    relay_lbl:'继电器', relay_fail:'继电器控制失败',
    ota_busy:'正在处理中', ota_check_start:'开始检测更新…',
    ota_update_confirm:'确认升级固件？设备将下载并写入新固件后自动重启。',
    ota_start_fail:'升级启动失败', ota_started:'固件升级已开始，请等待设备重启…',
    ota_success_msg:'固件升级成功！设备正在重启…',
    ota_new_ver:'发现新版本，可在 OTA 页面升级：',
    unknown_err:'未知错误',
    flash_read_err:'读取错误', flash_writing:'写入中…',
    // File Manager
    fm_title:'文件管理', fm_sub:'存储在外部 Flash · W25Q128 · 16 MB',
    fm_file_count:'文件数', fm_capacity:'总容量', fm_file_list:'文件列表',
    fm_format_btn:'格式化存储', fm_empty:'暂无文件。可从 RS485 / CAN / UART 页面保存日志。',
    fm_col_name:'文件名', fm_col_fmt:'格式', fm_col_size:'大小', fm_col_date:'保存时间',
    fm_download:'下载', fm_delete:'删除',
    fm_delete_confirm:'确认删除此文件？', fm_deleted:'文件已删除',
    fm_format_confirm:'确认格式化？将删除所有已保存文件，操作不可撤销。',
    fm_formatted:'存储已格式化', fm_loading:'加载中…',
    fm_save_title:'保存日志到 Flash', fm_filename:'文件名（字母/数字/下划线）',
    fm_format_lbl:'文件格式', fm_cancel:'取消', fm_save_btn:'保存',
    fm_saving:'保存中…', fm_saved:'已保存到 Flash', fm_no_data:'日志为空，无数据可保存',
    save_to_flash:'保存到Flash', files:'个文件',
    // AI
    ai:'AI 助手',
    ai_cfg_title:'AI 设置', ai_cfg_sub:'配置 API 提供商、Key 和模型',
    ai_provider:'API 提供商', ai_prov_openai:'OpenAI / 兼容接口', ai_prov_anthropic:'Anthropic (Claude)',
    ai_base_url:'API 端点 URL', ai_key_lbl:'API Key', ai_model_lbl:'模型', ai_sys_lbl:'系统提示',
    ai_cfg_help:'OpenAI: 填写 base_url 和 sk- Key。Anthropic: 选 Anthropic，填 claude- 模型名和 Key。',
    ai_test:'测试连接',
    ai_quick_title:'快速分析', ai_quick_help:'点击自动附加该通道最近的数据帧，发给 AI 分析协议内容。',
    ai_chat_title:'AI 对话', ai_clear:'清空',
    ai_ctx_lbl:'附加数据:', ai_attach:'附加',
    ai_input_ph:'输入问题，例如：分析这段 RS485 数据是什么协议？（Ctrl+Enter 发送）',
    ai_enter_hint:'Ctrl+Enter 发送 · AI 具备连续对话和工具调用能力',
    ai_no_key:'请先在 AI 设置中填写 API Key',
    ai_no_data:'该通道暂无数据',
    ai_thinking:'AI 思考中…',
    ai_api_error:'API 错误',
    ai_cleared:'对话已清空',
    ai_tool_send:'发送命令',
    ai_quick_prompt:'请分析 {ch} 通道的通讯数据，识别协议类型并解释数据含义。',
    ai_sys_entry_title:'AI 通讯分析', ai_sys_entry_help:'对 RS485 / CAN / UART 数据进行智能分析。',
    ai_go_tab:'前往 AI 助手',
    ai_test_ok:'连接成功', ai_test_fail:'连接失败',
    // System stats
    sys_stats_title:'资源占用率',
    stats_cpu:'CPU', stats_mem:'内存 (Heap)', stats_flash:'外部 Flash 存储',
  },
  en: {
    // tabs
    rs485:'RS485', can:'CAN', uart:'UART', flash:'Files', wifi:'WiFi', system:'System', ota:'OTA Update',
    // loading
    loading:'Loading…', loadErr:'Load failed', retry:'Retry', refresh:'Refresh',
    // generic
    state:'State', ok:'OK', error:'Error', send:'Send', clear:'Clear', apply:'Apply',
    export_log:'Export', export_csv:'Export CSV',
    start_btn:'▶ Start', stop_btn:'⏹ Stop', pause_btn:'⏸ Pause', resume_btn:'▶ Resume',
    // serial common
    baud_rate:'Baud Rate', data_bits:'Data Bits', parity_bits:'Parity', stop_bits:'Stop Bits',
    disp_fmt:'Display Format', send_fmt:'Send Format', eol_ascii:'EOL (ASCII)',
    port_cfg:'Port Settings', txrx_log:'TX/RX Log', auto_send:'Auto Send',
    send_hist:'Send History', no_hist:'No history', frames:'frames',
    running:'Running', not_ready:'Not Ready', not_init:'Not Initialized',
    frame_fmt:'Frame Format', tx_bytes:'TX', rx_bytes:'RX',
    // RS485
    rs485_title:'RS485 Debug', rs485_sub:'UART2 · TX=IO8 · RX=IO9 · DE=IO46 · RE=IO16',
    crc16_btn:'CRC16', modbus_read:'Modbus Read', modbus_write:'Modbus Write',
    // CAN
    can_title:'CAN Bus Debug', can_sub:'TWAI · TX=IO6 · RX=IO7 · ISO 11898',
    can_cfg:'CAN Settings', work_mode:'Mode',
    send_frame:'Send Frame', frame_id:'Frame ID (HEX)', data_hex:'Data (HEX, ≤8 bytes)',
    ext_frame:'Ext Frame', rtr_frame:'RTR Frame',
    frame_log:'Frame Log', id_filter:'ID Filter (HEX)', filter_mask:'Mask (HEX)',
    apply_filter:'Apply Filter', recover_bus:'Recover Bus', quick_tpl:'Quick Template',
    tpl_none:'── Select Template ──',
    can_th_ts:'Time (ms)', can_th_dir:'Dir', can_th_id:'ID',
    can_th_type:'Type', can_th_dlc:'DLC', can_th_data:'Data (HEX)',
    // UART
    uart_title:'UART Debug',
    uart_sub:'UART0: TX=IO43 RX=IO44  │  UART1: TX=IO17 RX=IO18',
    // Flash
    flash_title:'External SPI Flash',
    flash_sub:'W25Q128JVSIQ · 16 MB · SPI3 · MISO=IO35 CLK=IO36 MOSI=IO37 CS=IO38',
    flash_read:'Read', flash_write:'Write', flash_erase:'Erase',
    start_addr:'Start Address (HEX)', byte_len:'Length (bytes, ≤1024)',
    erase_type_lbl:'Erase Type', capacity:'Capacity', page_size:'Page Size',
    sector_size:'Sector Size', block_size:'Block Size', ready:'Ready',
    // WiFi
    wifi_title:'WiFi Network', wifi_sub:'2.4 GHz · AP + STA Dual Mode',
    ap_cfg_title:'AP Config', ap_cfg_sub:'Change AP SSID and password',
    ssid_name_lbl:'AP Name (SSID)', ap_pass_lbl:'AP Password (blank = keep)',
    save_apply:'Save & Apply',
    available_nets:'Available Networks', connect_title:'Connect',
    // System
    sys_info:'System Info', term_res:'Terminator Control', sys_ops:'System Operations',
    theme_title:'UI Theme Color',
    theme_help:'Click a swatch to change the accent color. Saved automatically.',
    custom_color:'Custom',
    // OTA
    ota_fw_title:'Firmware Version', fw_cur:'Current Version', fw_srv:'Server Version',
    upg_state:'Update State', upg_notes:'Release Notes', upg_progress:'Update Progress',
    srv_cfg:'Server Config', manifest_url_lbl:'Manifest URL',
    auto_check_lbl:'Auto Check Updates',
    interval_prefix:'Every', interval_suffix:'minutes',
    save_cfg:'Save Config', check_update:'Check Update', update_now:'Update Now',
    ota_idle:'Idle', ota_checking:'Checking…', ota_up_to_date:'Up to Date',
    ota_available:'Update Available', ota_updating:'Updating…',
    ota_success:'Success', ota_failed:'Failed',
    // WiFi
    connect:'Connect', disconnect:'Disconnect', clearSaved:'Clear Saved',
    ssid:'SSID', password:'Password', scan:'Scan', scanning:'Scanning…',
    connected:'Connected', disconnected:'Disconnected', connecting:'Connecting', ap_mode:'AP Mode',
    currentSsid:'Current SSID', ip:'IP', availableNetworks:'Available Networks',
    encrypted:'Secured', open:'Open', chip:'Chip', version:'Version',
    reboot:'Reboot', rebootConfirm:'Confirm reboot? All connections will drop.', rebooting:'Rebooting…',
    actions:'Actions',
    // extra toast/confirm strings
    applied:'applied', cfg_fail:'Config failed', save_fail:'Save failed', saved:'Saved',
    // HTML labels
    parity_none:'None', parity_even:'Even', parity_odd:'Odd',
    fmt_hex:'HEX bytes', fmt_ascii:'ASCII text', eol_none:'None',
    can_mode_normal:'Normal (TX+RX)', can_mode_listen:'Listen-Only (RX only)', can_mode_self:'Self-Test (loopback)',
    can_mode_help:'Self-Test: no terminator needed, frame loops back. Listen-Only: receive only, no bus interference.',
    dlc_auto:'Auto',
    filter_no:'No filter',
    flash_click_hint:'(Click Read to view data)',
    flash_write_warn:'Erase the target sector before writing, otherwise only 1→0 is possible.',
    flash_data_lbl:'Data (HEX, no spaces, e.g. DEADBEEF010203)',
    addr_lbl:'Address (HEX)',
    erase_sector:'Sector Erase (4 KB)', erase_block32:'Block Erase (32 KB)',
    erase_block64:'Block Erase (64 KB)', erase_chip:'Chip Erase (all 16 MB, ~20 s)',
    flash_chip_help:'Chip Erase will show a confirmation dialog before erasing all 16 MB.',
    ap_cfg_help:'Changes take effect for new connections; existing clients must reconnect.',
    ap_pass_ph:'•••••••• (WPA2, min 8 chars)',
    term_res_help:'IO42 relay controls 120Ω terminator for CAN and RS485. ON: connected, OFF: disconnected.',
    term_res_120:'120Ω Terminator',
    reboot_help:'Device will reconnect to the last saved WiFi on reboot. AP hotspot stays on.',
    ota_srv_help:'Enter the URL of the version manifest JSON file on your server.',
    rs485_input_ph:'HEX: 01 03 00 00 00 01  or switch to ASCII mode',
    uart_input_ph:'HEX: 01 02 03  or switch to ASCII mode',
    ws_disconnected:'WebSocket not connected',
    hist_fill:'Fill input', hist_del:'Delete',
    color_green:'Green', color_blue:'Blue', color_purple:'Purple',
    color_cyan:'Cyan', color_orange:'Orange', color_pink:'Pink',
    invalid_id:'Invalid ID format', bus_recovering:'Recovering bus…',
    max_1024:'Max 1024 bytes', enter_hex:'Enter HEX data first',
    erase_chip_confirm:'Confirm full chip erase? This is irreversible, takes ~20 s.',
    erase_fail:'Erase failed', erase_done:'Erase complete',
    no_networks:'No networks found', enter_ssid:'Enter SSID',
    enter_ap_ssid:'Enter AP name', pwd_min8:'Password must be at least 8 chars',
    ap_saved:'AP config saved. New clients will see the new name.',
    relay_lbl:'Relay', relay_fail:'Relay control failed',
    ota_busy:'Already in progress', ota_check_start:'Starting update check…',
    ota_update_confirm:'Confirm firmware update? Device will download and flash new firmware then reboot.',
    ota_start_fail:'Update start failed', ota_started:'Firmware update started, waiting for reboot…',
    ota_success_msg:'Firmware updated! Device is rebooting…',
    ota_new_ver:'New version available, go to OTA tab:',
    unknown_err:'Unknown error',
    flash_read_err:'Read error', flash_writing:'Writing…',
    // File Manager
    fm_title:'File Manager', fm_sub:'Stored on External Flash · W25Q128 · 16 MB',
    fm_file_count:'Files', fm_capacity:'Total Capacity', fm_file_list:'File List',
    fm_format_btn:'Format Storage', fm_empty:'No files. Save logs from RS485 / CAN / UART pages.',
    fm_col_name:'Filename', fm_col_fmt:'Format', fm_col_size:'Size', fm_col_date:'Saved At',
    fm_download:'Download', fm_delete:'Delete',
    fm_delete_confirm:'Confirm delete this file?', fm_deleted:'File deleted',
    fm_format_confirm:'Confirm format? All saved files will be erased. Irreversible.',
    fm_formatted:'Storage formatted', fm_loading:'Loading…',
    fm_save_title:'Save Log to Flash', fm_filename:'Filename (alphanumeric/underscore)',
    fm_format_lbl:'File Format', fm_cancel:'Cancel', fm_save_btn:'Save',
    fm_saving:'Saving…', fm_saved:'Saved to Flash', fm_no_data:'Log is empty, nothing to save',
    save_to_flash:'Save to Flash', files:'files',
    // AI
    ai:'AI Assistant',
    ai_cfg_title:'AI Settings', ai_cfg_sub:'Configure API provider, key and model',
    ai_provider:'API Provider', ai_prov_openai:'OpenAI / Compatible', ai_prov_anthropic:'Anthropic (Claude)',
    ai_base_url:'API Base URL', ai_key_lbl:'API Key', ai_model_lbl:'Model', ai_sys_lbl:'System Prompt',
    ai_cfg_help:'OpenAI: enter base_url and sk- key. Anthropic: select Anthropic, enter claude- model and key.',
    ai_test:'Test Connection',
    ai_quick_title:'Quick Analyze', ai_quick_help:'Click to auto-attach recent frames from that channel and analyze.',
    ai_chat_title:'AI Chat', ai_clear:'Clear',
    ai_ctx_lbl:'Attach data:', ai_attach:'Attach',
    ai_input_ph:'Ask a question, e.g.: What protocol is this RS485 data? (Ctrl+Enter to send)',
    ai_enter_hint:'Ctrl+Enter to send · AI supports continuous conversation and tool calls',
    ai_no_key:'Please configure your API Key in AI Settings first',
    ai_no_data:'No data on this channel yet',
    ai_thinking:'AI thinking…',
    ai_api_error:'API error',
    ai_cleared:'Chat cleared',
    ai_tool_send:'Send Command',
    ai_quick_prompt:'Please analyze the {ch} channel data, identify the protocol and explain the data.',
    ai_sys_entry_title:'AI Communication Analysis', ai_sys_entry_help:'Analyze RS485 / CAN / UART data with AI.',
    ai_go_tab:'Go to AI Assistant',
    ai_test_ok:'Connection OK', ai_test_fail:'Connection failed',
    // System stats
    sys_stats_title:'Resource Usage',
    stats_cpu:'CPU', stats_mem:'Memory (Heap)', stats_flash:'External Flash',
  }
};
const t = k => (I18N[lang] && I18N[lang][k]) || k;

function applyI18n(root) {
  (root || document).querySelectorAll('[data-i18n]').forEach(el => {
    el.textContent = t(el.dataset.i18n);
  });
  (root || document).querySelectorAll('[data-i18n-ph]').forEach(el => {
    el.placeholder = t(el.dataset.i18nPh);
  });
  (root || document).querySelectorAll('[data-i18n-title]').forEach(el => {
    el.title = t(el.dataset.i18nTitle);
  });
}

// ── HTTP helpers ──
async function api(path, opts) {
  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 8000);
  try {
    const r = await fetch(path, Object.assign({ signal: ctrl.signal }, opts || {}));
    const txt = await r.text();
    try { return JSON.parse(txt); } catch (_) { return { _raw: txt }; }
  } catch (e) {
    return { _err: e.name === 'AbortError' ? 'timeout' : (e.message || 'network') };
  } finally { clearTimeout(timer); }
}
const apiGet  = p => api(p);
const apiPost = (p, b) => api(p, { method:'POST', headers:{'Content-Type':'application/json'}, body: JSON.stringify(b||{}) });

// ── Toast ──
function toast(msg, type) {
  const s = document.getElementById('toast-stack');
  if (!s) return;
  const el = document.createElement('div');
  el.className = 'toast' + (type==='success'?' ok':type==='error'?' err':type==='warn'?' warn':'');
  el.textContent = msg;
  s.appendChild(el);
  setTimeout(() => { el.style.transition='opacity .25s'; el.style.opacity='0'; setTimeout(()=>el.remove(),250); }, 2800);
}
function confirmDialog(msg) { return Promise.resolve(window.confirm(msg)); }

// ── Tabs ──
function renderTabs() {
  const nav = document.getElementById('tab-nav');
  if (!nav) return;
  nav.innerHTML = TABS.map(m =>
    `<button class="tab-btn${m===curTab?' active':''}" data-tab="${m}">${t(m)}</button>`
  ).join('') +
  `<select class="lang-sel" id="lang-sel">
    <option value="zh"${lang==='zh'?' selected':''}>中文</option>
    <option value="en"${lang==='en'?' selected':''}>EN</option>
  </select>`;
  nav.querySelectorAll('.tab-btn').forEach(b => b.addEventListener('click', () => switchTab(b.dataset.tab)));
  document.getElementById('lang-sel').addEventListener('change', e => { lang=e.target.value; localStorage.setItem('lang',lang); renderTabs(); switchTab(curTab); });
}

async function switchTab(m) {
  const c = document.getElementById('content');
  // Save departing tab's live DOM (keeps log entries alive)
  if (_tabLoaded.has(curTab)) {
    _tabCache[curTab] = c.innerHTML;
  }
  curTab = m; localStorage.setItem('activeTab', m); renderTabs();
  _stopAllAutoTimers();

  // Restore from cache if available (no fetch needed, logs preserved)
  if (_tabCache[m]) {
    c.innerHTML = _tabCache[m];
    applyI18n(c);
    // Reset any auto-send button that was left in "stop" state
    c.querySelectorAll('[id$="-auto-btn"]').forEach(btn => {
      btn.textContent = t('start_btn');
      btn.classList.remove('btn-danger');
    });
    const fn = window['init_' + m];
    if (typeof fn === 'function') fn();
    return;
  }

  c.innerHTML = `<div class="card"><div class="empty">${t('loading')}</div></div>`;
  try {
    const r = await fetch(`/modules/${m}.html`, { signal: AbortSignal.timeout(6000) });
    if (!r.ok) throw new Error('http ' + r.status);
    c.innerHTML = await r.text();
    applyI18n(c);
    const fn = window['init_' + m];
    if (typeof fn === 'function') fn();
    _tabLoaded.add(m);
  } catch (e) {
    c.innerHTML = `<div class="card"><div class="empty">
      <div style="margin-bottom:10px">${t('loadErr')}</div>
      <button class="btn btn-primary" onclick="switchTab('${m}')">${t('retry')}</button>
    </div></div>`;
  }
}

// ── Status bar ──
async function refreshStatus() {
  const s = await apiGet('/api/system/info');
  const bar = document.getElementById('status-bar');
  if (!bar || s._err) return;
  const wc = s.wifi_status==='connected'?'ok':s.wifi_status==='connecting'?'warn':s.wifi_status==='ap_mode'?'info':'';
  bar.innerHTML =
    `<span class="pill"><span class="dot ${wc}"></span>WiFi · ${s.wifi_status||'?'}</span>` +
    `<span class="pill"><span class="dot info"></span>${s.ip||'192.168.4.1'}</span>` +
    (wsConnected ? `<span class="pill"><span class="dot ok"></span>WS</span>` : `<span class="pill"><span class="dot err"></span>WS</span>`);
}

// ── WebSocket ──
let ws = null, wsConnected = false;
let wsReconnTimer = null;

function wsConnect() {
  if (ws && ws.readyState <= 1) return;
  const host = location.hostname || '192.168.4.1';
  ws = new WebSocket(`ws://${host}/ws`);
  ws.onopen  = () => { wsConnected = true; refreshStatus(); };
  ws.onclose = () => {
    wsConnected = false; refreshStatus();
    clearTimeout(wsReconnTimer);
    wsReconnTimer = setTimeout(wsConnect, 3000);
  };
  ws.onerror = () => ws.close();
  ws.onmessage = e => {
    try { handleWsMsg(JSON.parse(e.data)); } catch (_) {}
  };
}

function wsSend(obj) {
  if (ws && ws.readyState === 1) ws.send(JSON.stringify(obj));
  else toast(t('ws_disconnected'), 'warn');
}

// ── WS message dispatcher ──
function handleWsMsg(m) {
  switch (m.type) {
    case 'rs485_rx':      appendRs485Log(m.ts, m.data, 'RX'); break;
    case 'rs485_tx_done': {
      const data = _rs485TxPending.shift();
      if (data != null) {
        if (m.ok) appendRs485Log(m.ts, data, 'TX');
        else { appendRs485Log(m.ts, data, 'TX-ERR'); toast('RS485 TX failed', 'error'); }
      }
      break;
    }
    case 'can_rx':      appendCanLog(m); break;
    case 'can_status':  updateCanStatus(m); break;
    case 'can_cfg_ack': toast(`CAN: ${m.baud} ${m.mode} ${m.ok?'OK':'FAIL'}`, m.ok?'success':'error'); break;
    case 'uart_rx':     appendUartLog(m.port, m.ts, m.data, 'RX'); break;
    case 'flash_info':  renderFlashInfo(m); break;
    case 'flash_data':  renderFlashDump(m); break;
    case 'flash_ok':    toast(`Flash ${m.op} OK (addr:0x${(m.addr||0).toString(16)})`, 'success'); break;
    case 'flash_err':   toast(`Flash ${t('error')}: ${m.msg}`, 'error'); break;
    case 'ota_state':   handleOtaState(m); break;
    case 'ota_progress': handleOtaProgress(m.percent); break;
  }
}

// ═══════════════════════════════════════
//  Shared utilities
// ═══════════════════════════════════════

// Auto-send timers  { id: intervalHandle }
const _autoTimers = {};

function _stopAllAutoTimers() {
  Object.keys(_autoTimers).forEach(k => { clearInterval(_autoTimers[k]); delete _autoTimers[k]; });
}

function autoToggle(timerId, startBtnId, intervalElId, countElId, sendFn) {
  const btn = document.getElementById(startBtnId);
  if (_autoTimers[timerId]) {
    clearInterval(_autoTimers[timerId]);
    delete _autoTimers[timerId];
    if (btn) { btn.textContent = t('start_btn'); btn.classList.remove('btn-danger'); }
    return;
  }
  const ms = Math.max(20, parseInt(document.getElementById(intervalElId)?.value) || 1000);
  const maxCount = parseInt(document.getElementById(countElId)?.value ?? '-1');
  let sent = 0;
  _autoTimers[timerId] = setInterval(() => {
    sendFn();
    sent++;
    if (maxCount > 0 && sent >= maxCount) {
      clearInterval(_autoTimers[timerId]);
      delete _autoTimers[timerId];
      if (btn) { btn.textContent = t('start_btn'); btn.classList.remove('btn-danger'); }
    }
  }, ms);
  if (btn) { btn.textContent = t('stop_btn'); btn.classList.add('btn-danger'); }
}

function autoToggleNoCount(timerId, startBtnId, intervalElId, sendFn) {
  const btn = document.getElementById(startBtnId);
  if (_autoTimers[timerId]) {
    clearInterval(_autoTimers[timerId]);
    delete _autoTimers[timerId];
    if (btn) { btn.textContent = t('start_btn'); btn.classList.remove('btn-danger'); }
    return;
  }
  const ms = Math.max(10, parseInt(document.getElementById(intervalElId)?.value) || 1000);
  _autoTimers[timerId] = setInterval(sendFn, ms);
  if (btn) { btn.textContent = t('stop_btn'); btn.classList.add('btn-danger'); }
}

// Pause state  { consoleId: bool }
const _paused = {};

function togglePause(consoleId, btnId) {
  _paused[consoleId] = !_paused[consoleId];
  const btn = document.getElementById(btnId);
  if (btn) {
    btn.textContent = _paused[consoleId] ? t('resume_btn') : t('pause_btn');
    btn.style.color = _paused[consoleId] ? 'var(--warn)' : '';
    btn.style.borderColor = _paused[consoleId] ? 'var(--warn)' : '';
  }
}

// Send history  { key: [val, ...] }  max 20  — localStorage + NVS mirror
const _HIST_API = { rs485_hist:'rs485', uart0_hist:'uart0', uart1_hist:'uart1', can_hist:'can' };

function histSyncToDevice(lsKey) {
  const ak = _HIST_API[lsKey]; if (!ak) return;
  fetch('/api/hist?key='+ak, {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: localStorage.getItem(lsKey) || '[]',
  }).catch(()=>{});
}
async function histLoadFromDevice(lsKey) {
  const ak = _HIST_API[lsKey]; if (!ak) return;
  try {
    const r = await fetch('/api/hist?key='+ak);
    if (!r.ok) return;
    const items = await r.json();
    if (Array.isArray(items) && items.length > 0)
      localStorage.setItem(lsKey, JSON.stringify(items));
  } catch(e) {}
}
function histPush(key, val) {
  if (!val) return;
  let h = JSON.parse(localStorage.getItem(key) || '[]');
  h = [val, ...h.filter(x => x !== val)].slice(0, 20);
  localStorage.setItem(key, JSON.stringify(h));
  histSyncToDevice(key);
}
function histGet(key) {
  return JSON.parse(localStorage.getItem(key) || '[]');
}
function histRemove(key, val) {
  let h = JSON.parse(localStorage.getItem(key) || '[]');
  localStorage.setItem(key, JSON.stringify(h.filter(x => x !== val)));
  histSyncToDevice(key);
}
function histRefreshPanel(panelId, histKey, sendFn, fillFn) {
  const panel = document.getElementById(panelId);
  if (!panel) return;
  const h = histGet(histKey);
  if (!h.length) {
    panel.innerHTML = `<div class="hist-empty">${t('no_hist')}</div>`;
    return;
  }
  panel.innerHTML = h.map((v, i) =>
    `<div class="hist-item">` +
    `<button class="hist-send" title="${t('send')}" data-i="${i}">▶</button>` +
    `<span class="hist-data" title="${escHtml(v)}" data-i="${i}">${escHtml(v.length>52?v.slice(0,49)+'…':v)}</span>` +
    `<button class="hist-edit" title="${t('hist_fill')}" data-i="${i}">✎</button>` +
    `<button class="hist-del"  title="${t('hist_del')}"  data-i="${i}">×</button>` +
    `</div>`
  ).join('');
  panel.querySelectorAll('.hist-send').forEach(btn => {
    const v = h[+btn.dataset.i];
    btn.onclick = e => { e.stopPropagation(); sendFn(v); };
  });
  panel.querySelectorAll('.hist-data, .hist-edit').forEach(el => {
    const v = h[+el.dataset.i];
    el.onclick = e => { e.stopPropagation(); fillFn(v); };
  });
  panel.querySelectorAll('.hist-del').forEach(btn => {
    const v = h[+btn.dataset.i];
    btn.onclick = e => {
      e.stopPropagation();
      histRemove(histKey, v);
      histRefreshPanel(panelId, histKey, sendFn, fillFn);
    };
  });
}
function rs485HistRefresh() {
  histRefreshPanel('rs485-hist-panel', 'rs485_hist',
    val => { const e=document.getElementById('rs485-send-input'); if(e){e.value=val; rs485Send();} },
    val => { const e=document.getElementById('rs485-send-input'); if(e) e.value=val; }
  );
}
function uartHistRefresh(p) {
  histRefreshPanel(`uart${p}-hist-panel`, `uart${p}_hist`,
    val => { const e=document.getElementById(`uart${p}-send-input`); if(e){e.value=val; uartSend(p);} },
    val => { const e=document.getElementById(`uart${p}-send-input`); if(e) e.value=val; }
  );
}

// CRC16 Modbus (little-endian append)
function crc16Modbus(hexStr) {
  const clean = hexStr.replace(/\s+/g, '');
  const bytes = [];
  for (let i = 0; i < clean.length - 1; i += 2)
    bytes.push(parseInt(clean.substr(i, 2), 16));
  let crc = 0xFFFF;
  for (const b of bytes) {
    crc ^= b;
    for (let i = 0; i < 8; i++)
      crc = (crc & 1) ? ((crc >>> 1) ^ 0xA001) : (crc >>> 1);
  }
  const lo = (crc & 0xFF).toString(16).padStart(2, '0').toUpperCase();
  const hi = (crc >>> 8).toString(16).padStart(2, '0').toUpperCase();
  return (clean + lo + hi).match(/.{2}/g).join(' ');
}

// Display data with format
function displayData(hex, fmt) {
  const clean = (hex || '').replace(/\s+/g, '');
  const bytes = clean.match(/.{1,2}/g) || [];
  if (fmt === 'ascii') {
    const s = bytes.map(b => { const v=parseInt(b,16); return (v>=32&&v<=126)?String.fromCharCode(v):'.'; }).join('');
    return escHtml(s);
  }
  if (fmt === 'both') {
    const hexPart = bytes.map(b => b.toUpperCase()).join(' ');
    const ascPart = bytes.map(b => { const v=parseInt(b,16); return (v>=32&&v<=126)?String.fromCharCode(v):'.'; }).join('');
    return `<span class="mono">${escHtml(hexPart)}</span> <span style="color:var(--border)">│</span> <span style="color:var(--text2);font-family:var(--mono)">${escHtml(ascPart)}</span>`;
  }
  return `<span class="mono">${escHtml(bytes.map(b=>b.toUpperCase()).join(' '))}</span>`;
}

// Append EOL to hex string for ASCII-mode sends
function appendEol(hexData, eolHex) {
  if (!eolHex) return hexData;
  const clean = hexData.replace(/\s+/g, '');
  const eol = eolHex.replace(/\s+/g, '');
  return (clean + eol).match(/.{2}/g).join(' ');
}

// ═══════════════════════════════════════
//  RS485
// ═══════════════════════════════════════
window.init_rs485 = function() {
  _logRestore('rs485');
  const g = id => document.getElementById(id);
  if (g('rs485-send-btn'))   g('rs485-send-btn').onclick   = rs485Send;
  if (g('rs485-clear-btn'))  g('rs485-clear-btn').onclick  = () => clearConsoleEx('rs485-console', 'rs485-log-count');
  if (g('rs485-apply-btn'))  g('rs485-apply-btn').onclick  = rs485ApplyCfg;
  if (g('rs485-export-btn')) g('rs485-export-btn').onclick = () => exportLog('rs485-console','rs485');
  if (g('rs485-pause-btn'))  g('rs485-pause-btn').onclick  = () => togglePause('rs485-console','rs485-pause-btn');
  if (g('rs485-crc-btn'))    g('rs485-crc-btn').onclick    = rs485AppendCRC;
  if (g('rs485-auto-btn'))   g('rs485-auto-btn').onclick   = () => autoToggle('rs485','rs485-auto-btn','rs485-interval','rs485-auto-count', rs485Send);
  if (g('rs485-send-input')) g('rs485-send-input').addEventListener('keydown', e => { if (e.key==='Enter'&&!e.shiftKey){ e.preventDefault(); rs485Send(); }});

  // Restore localStorage
  ['rs485-disp-sel','rs485-fmt-sel','rs485-eol-sel','rs485-baud-sel','rs485-bits-sel','rs485-parity-sel','rs485-stop-sel'].forEach(id => {
    const el = g(id); if (!el) return;
    const v = localStorage.getItem(id); if (v) el.value = v;
    el.onchange = () => localStorage.setItem(id, el.value);
  });
  if (g('rs485-hist-clear-btn'))
    g('rs485-hist-clear-btn').onclick = () => {
      localStorage.removeItem('rs485_hist');
      histSyncToDevice('rs485_hist');
      rs485HistRefresh();
    };
  histLoadFromDevice('rs485_hist').then(() => rs485HistRefresh());
  rs485RefreshStatus();
};

window.rs485FillPreset = function(v) {
  const el = document.getElementById('rs485-send-input'); if(el) el.value = v;
};

function rs485AppendCRC() {
  const inp = document.getElementById('rs485-send-input');
  if (!inp || !inp.value.trim()) { toast(t('enter_hex'), 'warn'); return; }
  inp.value = crc16Modbus(inp.value.trim());
}

async function rs485RefreshStatus() {
  const s = await apiGet('/api/rs485/status');
  const el = document.getElementById('rs485-stat');
  if (!el || s._err) return;
  el.innerHTML =
    `<div><dt>${t('baud_rate')}</dt><dd>${s.baud}</dd></div>` +
    `<div><dt>${t('frame_fmt')}</dt><dd><code>${s.format||'8N1'}</code></dd></div>` +
    `<div><dt>${t('tx_bytes')}</dt><dd>${s.tx_bytes} B · ${s.tx_frames} ${t('frames')}</dd></div>` +
    `<div><dt>${t('rx_bytes')}</dt><dd>${s.rx_bytes} B · ${s.rx_frames} ${t('frames')}</dd></div>` +
    `<div><dt>${t('state')}</dt><dd><span class="tag ${s.running?'ok':'err'}">${s.running?t('running'):t('not_ready')}</span></dd></div>`;
  const g = id => document.getElementById(id);
  if (g('rs485-baud-sel')   && s.baud)   g('rs485-baud-sel').value   = s.baud;
  if (g('rs485-bits-sel')   && s.bits)   g('rs485-bits-sel').value   = s.bits;
  if (g('rs485-parity-sel') && s.parity!=null) g('rs485-parity-sel').value = s.parity;
  if (g('rs485-stop-sel')   && s.stop)   g('rs485-stop-sel').value   = s.stop;
}

async function rs485ApplyCfg() {
  const g = id => document.getElementById(id);
  const baud   = parseInt(g('rs485-baud-sel')?.value   || '9600');
  const bits   = parseInt(g('rs485-bits-sel')?.value   || '8');
  const parity = parseInt(g('rs485-parity-sel')?.value || '0');
  const stop   = parseInt(g('rs485-stop-sel')?.value   || '1');
  wsSend({ type:'rs485_cfg', baud, bits, parity, stop });
  const r = await apiPost('/api/rs485/config', { baud, bits, parity, stop });
  if (!r._err) {
    toast(`RS485 ${baud} · ${bits}${['N','E','O'][parity]||'N'}${stop} ${t('applied')}`, 'success');
    localStorage.setItem('rs485-baud-sel', String(baud));
    rs485RefreshStatus();
  } else { toast(t('cfg_fail'), 'error'); }
}

/* Queue of pending TX data waiting for rs485_tx_done confirmation from ESP32 */
const _rs485TxPending = [];

function rs485Send() {
  const inp  = document.getElementById('rs485-send-input');
  const fmt  = document.getElementById('rs485-fmt-sel')?.value || 'hex';
  const eol  = document.getElementById('rs485-eol-sel')?.value || '';
  if (!inp || !inp.value.trim()) return;
  let data = inp.value.trim();
  if (fmt === 'ascii') { data = asciiToHex(data); if (eol) data = appendEol(data, eol); }
  histPush('rs485_hist', inp.value.trim());
  rs485HistRefresh();
  wsSend({ type:'rs485_send', data });
  /* TX log is written when ESP32 confirms via rs485_tx_done, not here */
  _rs485TxPending.push(data);
  inp.value = '';
}

function appendRs485Log(ts, data, dir) {
  if (_paused['rs485-console']) return;
  _logPush('rs485', [ts, data, dir]);
  const c = document.getElementById('rs485-console');
  if (!c) return;
  const fmt = document.getElementById('rs485-disp-sel')?.value || 'hex';
  const byteCount = (data.replace(/\s+/g,'').length / 2) | 0;
  const timeStr = tsStr(ts);
  const row = document.createElement('div');
  row.className = `log-row ${dir==='TX'?'tx':'rx'}`;
  row.innerHTML = `<span class="ts">[${timeStr}]</span> <strong>${dir}</strong> <span class="byte-cnt">[${byteCount}B]</span> ${displayData(data, fmt)}`;
  c.appendChild(row);
  while (c.children.length > 1000) c.removeChild(c.firstChild);
  c.scrollTop = c.scrollHeight;
  const cnt = document.getElementById('rs485-log-count');
  if (cnt) cnt.textContent = c.children.length + ' ' + t('frames');
}

// ═══════════════════════════════════════
//  CAN
// ═══════════════════════════════════════
let _canFilter = null; // { id, mask }

window.init_can = function() {
  _logRestore('can');
  const g = id => document.getElementById(id);
  if (g('can-send-btn'))         g('can-send-btn').onclick         = canSend;
  if (g('can-apply-btn'))        g('can-apply-btn').onclick        = canApplyCfg;
  if (g('can-recover-btn'))      g('can-recover-btn').onclick      = canRecover;
  if (g('can-clear-btn'))        g('can-clear-btn').onclick        = () => clearCanLog();
  if (g('can-export-btn'))       g('can-export-btn').onclick       = () => exportCanLog();
  if (g('can-pause-btn'))        g('can-pause-btn').onclick        = () => togglePause('can-log-body','can-pause-btn');
  if (g('can-auto-btn'))         g('can-auto-btn').onclick         = () => autoToggleNoCount('can','can-auto-btn','can-interval', canSend);
  if (g('can-filter-btn'))       g('can-filter-btn').onclick       = canFilterApply;
  if (g('can-filter-clear-btn')) g('can-filter-clear-btn').onclick = canFilterClear;
  if (g('can-preset-sel'))       g('can-preset-sel').onchange      = canLoadPreset;
  // RTR disables data input
  if (g('can-rtr-chk')) g('can-rtr-chk').onchange = () => {
    const rtr = g('can-rtr-chk').checked;
    if (g('can-data-input')) g('can-data-input').disabled = rtr;
  };
  canRefreshStatus();
};

function canFilterApply() {
  const idStr   = document.getElementById('can-filter-id')?.value.trim();
  const maskStr = document.getElementById('can-filter-mask')?.value.trim();
  if (!idStr) { canFilterClear(); return; }
  const id   = parseInt(idStr, 16);
  const mask = maskStr ? parseInt(maskStr, 16) : 0x1FFFFFFF;
  if (isNaN(id)) { toast(t('invalid_id'), 'warn'); return; }
  _canFilter = { id, mask };
  const tag = document.getElementById('can-filter-tag');
  if (tag) {
    tag.style.display = '';
    tag.innerHTML = `<span class="tag info">ID=0x${id.toString(16).toUpperCase()} &amp; 0x${mask.toString(16).toUpperCase()}</span>`;
  }
  toast(`CAN filter: ID=0x${id.toString(16).toUpperCase()}`, 'success');
}

function canFilterClear() {
  _canFilter = null;
  const tag = document.getElementById('can-filter-tag');
  if (tag) tag.style.display = 'none';
  const idEl = document.getElementById('can-filter-id');
  const maskEl = document.getElementById('can-filter-mask');
  if (idEl) idEl.value = '';
  if (maskEl) maskEl.value = '';
}

function canLoadPreset() {
  const sel = document.getElementById('can-preset-sel');
  if (!sel || !sel.value) return;
  const [idStr, dlcStr, dataStr] = sel.value.split('|');
  const id = document.getElementById('can-id-input');
  const dlc = document.getElementById('can-dlc-input');
  const data = document.getElementById('can-data-input');
  if (id) id.value = idStr || '';
  if (dlc) dlc.value = dlcStr || '';
  if (data) data.value = (dataStr||'').match(/.{1,2}/g)?.join(' ') || '';
  sel.value = '';
}

async function canRefreshStatus() {
  const s = await apiGet('/api/can/status');
  if (s._err) return;
  updateCanStatus(s);
  const bsel = document.getElementById('can-baud-sel');
  const msel = document.getElementById('can-mode-sel');
  if (bsel && s.baud) bsel.value = s.baud;
  if (msel && s.mode) msel.value = s.mode;
}

function updateCanStatus(s) {
  const dot   = document.getElementById('can-state-dot');
  const label = document.getElementById('can-state-label');
  const txerr = document.getElementById('can-tx-err');
  const rxerr = document.getElementById('can-rx-err');
  const txcnt = document.getElementById('can-tx-cnt');
  const rxcnt = document.getElementById('can-rx-cnt');
  if (!dot) return;
  const stateClass = s.state==='running'?'ok': s.state==='bus_off'?'err': s.state==='warning'?'warn':'';
  dot.className = `dot ${stateClass}`;
  if (label) label.textContent = s.state || '—';
  if (txerr) txerr.textContent = `TX Err: ${s.tx_err||0}`;
  if (rxerr) rxerr.textContent = `RX Err: ${s.rx_err||0}`;
  if (txcnt) txcnt.textContent = `↑ TX ${s.tx_cnt||0}`;
  if (rxcnt) rxcnt.textContent = `↓ RX ${s.rx_cnt||0}`;
}

async function canApplyCfg() {
  const baud = parseInt(document.getElementById('can-baud-sel')?.value || '500000');
  const mode = document.getElementById('can-mode-sel')?.value || 'normal';
  wsSend({ type:'can_cfg', baud, mode });
  toast(`CAN ${baud} ${mode}…`, 'info');
}

async function canRecover() {
  const r = await apiPost('/api/can/recover');
  toast(r._err ? t('error') : t('bus_recovering'), r._err ? 'error' : 'info');
}

function canSend() {
  const id   = parseInt(document.getElementById('can-id-input')?.value || '0', 16);
  const ext  = document.getElementById('can-ext-chk')?.checked || false;
  const rtr  = document.getElementById('can-rtr-chk')?.checked || false;
  const dlcVal = document.getElementById('can-dlc-input')?.value;
  const dlc  = dlcVal !== '' && dlcVal != null ? parseInt(dlcVal) : -1;
  const data = rtr ? '' : (document.getElementById('can-data-input')?.value || '').replace(/\s/g,'');
  if (isNaN(id)) { toast(t('invalid_id'), 'warn'); return; }
  wsSend({ type:'can_send', id, ext, rtr, dlc, data });
  const dlcDisplay = dlc >= 0 ? dlc : data.length / 2 | 0;
  appendCanLog({ ts: Date.now(), id, ext, rtr, dlc: dlcDisplay, data, _tx: true });
}

let _canLogCount = 0;

function clearCanLog() {
  const tb = document.getElementById('can-log-body');
  if (tb) tb.innerHTML = '';
  _canLogCount = 0;
  _logClear('can');
  const cnt = document.getElementById('can-log-count');
  if (cnt) cnt.textContent = '0 ' + t('frames');
}

function appendCanLog(m) {
  if (_paused['can-log-body']) return;
  if (_canFilter) {
    const fid = _canFilter.id, fmask = _canFilter.mask;
    if (((m.id || 0) & fmask) !== (fid & fmask)) return;
  }
  _logPush('can', { ts: m.ts, id: m.id, ext: m.ext, rtr: m.rtr, dlc: m.dlc, data: m.data, _tx: m._tx });
  const tbody = document.getElementById('can-log-body');
  if (!tbody) return;
  const ts    = tsStr(m.ts);
  const idHex = '0x' + (m.id || 0).toString(16).toUpperCase().padStart(m.ext?8:3,'0');
  const typeStr = m.rtr ? (m.ext?'RTR-Ext':'RTR-Std') : (m.ext?'Ext':'Std');
  const dataDisp = m.rtr ? '<span style="color:var(--text2)">—</span>' :
    `<span class="mono">${escHtml((m.data||'').match(/.{1,2}/g)?.map(b=>b.toUpperCase()).join(' ')||'')}</span>`;
  const row = document.createElement('tr');
  row.className = m._tx ? 'tx-row' : 'rx-row';
  row.innerHTML = `<td>${ts}</td><td>${m._tx?'↑TX':'↓RX'}</td><td class="mono">${idHex}</td><td>${typeStr}</td><td>${m.dlc||0}</td><td>${dataDisp}</td>`;
  tbody.appendChild(row);
  while (tbody.children.length > 500) tbody.removeChild(tbody.firstChild);
  tbody.parentElement.scrollTop = tbody.parentElement.scrollHeight;
  _canLogCount++;
  const cnt = document.getElementById('can-log-count');
  if (cnt) cnt.textContent = _canLogCount + ' ' + t('frames');
}

// ═══════════════════════════════════════
//  File Manager  (Flash tab)
// ═══════════════════════════════════════
let _fmSaveChannel = '';

window.init_flash = function() {
  const g = id => document.getElementById(id);
  if (g('fm-refresh-btn')) g('fm-refresh-btn').onclick = fmLoadList;
  if (g('fm-format-btn'))  g('fm-format-btn').onclick  = fmFormatStorage;
  fmLoadList();
};

async function fmLoadList() {
  const listEl  = document.getElementById('fm-file-list');
  const countEl = document.getElementById('fm-count');
  if (!listEl) return;
  listEl.innerHTML = `<div class="empty">${t('fm_loading')}</div>`;

  const r = await apiGet('/api/files');
  if (r._err || !r.files) {
    listEl.innerHTML = `<div class="empty">${t('error')}</div>`;
    return;
  }

  if (countEl) countEl.textContent = `${r.count} / ${r.total} ${t('files')}`;

  if (!r.files.length) {
    listEl.innerHTML = `<div class="empty">${t('fm_empty')}</div>`;
    return;
  }

  const rows = r.files.map(f => {
    const ts = f.ts || '';
    const dateStr = ts.length === 14
      ? `${ts.slice(0,4)}-${ts.slice(4,6)}-${ts.slice(6,8)} ${ts.slice(8,10)}:${ts.slice(10,12)}:${ts.slice(12,14)}`
      : ts;
    const displayName = `${ts.slice(0,8)}_${f.name}`;
    const sizeStr = f.size >= 1024 ? `${(f.size / 1024).toFixed(1)} KB` : `${f.size} B`;
    const fmtCls  = f.format === 'json' ? 'info' : '';
    return `<tr>
      <td class="mono" style="font-size:12px">${escHtml(displayName)}</td>
      <td><span class="tag ${fmtCls}">${f.format.toUpperCase()}</span></td>
      <td style="white-space:nowrap">${sizeStr}</td>
      <td style="font-size:12px;white-space:nowrap">${escHtml(dateStr)}</td>
      <td style="white-space:nowrap">
        <button class="btn btn-sm btn-ghost" onclick="fmDownload(${f.idx})" data-i18n="fm_download">下载</button>
        <button class="btn btn-sm btn-danger" onclick="fmDelete(${f.idx})" data-i18n="fm_delete" style="margin-left:4px">删除</button>
      </td>
    </tr>`;
  }).join('');

  listEl.innerHTML = `<div style="overflow-x:auto">
    <table class="can-table" style="width:100%">
      <thead><tr>
        <th data-i18n="fm_col_name">文件名</th>
        <th data-i18n="fm_col_fmt">格式</th>
        <th data-i18n="fm_col_size">大小</th>
        <th data-i18n="fm_col_date">保存时间</th>
        <th data-i18n="actions">操作</th>
      </tr></thead>
      <tbody>${rows}</tbody>
    </table>
  </div>`;
  applyI18n(listEl);
}

window.fmDownload = function(idx) {
  const a = document.createElement('a');
  a.href = `/api/file?idx=${idx}`;
  a.click();
};

window.fmDelete = async function(idx) {
  if (!await confirmDialog(t('fm_delete_confirm'))) return;
  const r = await api('/api/file/delete?idx=' + idx, { method: 'POST' });
  if (!r._err && r.ok) { toast(t('fm_deleted'), 'success'); fmLoadList(); }
  else toast(t('error'), 'error');
};

async function fmFormatStorage() {
  if (!await confirmDialog(t('fm_format_confirm'))) return;
  const r = await api('/api/files/format', { method: 'POST' });
  if (!r._err && r.ok) { toast(t('fm_formatted'), 'success'); fmLoadList(); }
  else toast(t('error'), 'error');
}

/* Show save-to-flash modal (created lazily, works from any tab) */
window.showSaveModal = function(channel) {
  _fmSaveChannel = channel;
  const buf = channel === 'can' ? _logBuf.can : (_logBuf[channel] || []);
  if (!buf.length) { toast(t('fm_no_data'), 'warn'); return; }

  if (!document.getElementById('fm-modal')) {
    const overlay = document.createElement('div');
    overlay.id = 'fm-modal';
    overlay.style.cssText = 'display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.55);z-index:1000;align-items:center;justify-content:center';
    overlay.innerHTML = `
      <div style="background:var(--card);border:1px solid var(--border);border-radius:10px;padding:20px;width:340px;max-width:90vw;box-shadow:0 8px 32px rgba(0,0,0,.4)">
        <div class="card-title" style="margin-bottom:14px" id="fm-modal-title"></div>
        <div class="field" style="margin-bottom:10px">
          <span id="fm-modal-fn-lbl"></span>
          <input type="text" id="fm-name-input" maxlength="30" placeholder="log_name" style="font-family:var(--mono)">
          <span style="font-size:11px;color:var(--text2)" id="fm-name-hint"></span>
        </div>
        <div class="field" style="margin-bottom:16px">
          <span id="fm-modal-fmt-lbl"></span>
          <select id="fm-fmt-sel">
            <option value="csv" selected>CSV</option>
            <option value="json">JSON</option>
          </select>
        </div>
        <div style="display:flex;gap:8px;justify-content:flex-end">
          <button class="btn btn-ghost" id="fm-modal-cancel"></button>
          <button class="btn btn-primary" id="fm-modal-confirm"></button>
        </div>
      </div>`;
    document.body.appendChild(overlay);
    document.getElementById('fm-modal-cancel').onclick  = fmHideModal;
    document.getElementById('fm-modal-confirm').onclick = fmConfirmSave;
    overlay.onclick = e => { if (e.target === overlay) fmHideModal(); };
  }

  const chanNames = { rs485:'RS485', uart0:'UART0', uart1:'UART1', can:'CAN' };
  const nameEl = document.getElementById('fm-name-input');
  const hintEl = document.getElementById('fm-name-hint');
  const modal  = document.getElementById('fm-modal');
  document.getElementById('fm-modal-title').textContent   = t('fm_save_title');
  document.getElementById('fm-modal-fn-lbl').textContent  = t('fm_filename');
  document.getElementById('fm-modal-fmt-lbl').textContent = t('fm_format_lbl');
  document.getElementById('fm-modal-cancel').textContent  = t('fm_cancel');
  document.getElementById('fm-modal-confirm').textContent = t('fm_save_btn');
  if (nameEl) nameEl.value = chanNames[channel] || channel;
  if (hintEl) hintEl.textContent = `${buf.length} ${t('frames')}`;
  modal.style.display = 'flex';
  if (nameEl) { nameEl.focus(); nameEl.select(); }
};

function fmHideModal() {
  const m = document.getElementById('fm-modal');
  if (m) m.style.display = 'none';
}

async function fmConfirmSave() {
  const nameEl = document.getElementById('fm-name-input');
  const fmtSel = document.getElementById('fm-fmt-sel');
  if (!nameEl) return;

  const name = (nameEl.value || 'log').replace(/[^a-zA-Z0-9_\-]/g, '_').slice(0, 30) || 'log';
  const fmt  = fmtSel?.value || 'csv';

  const now = new Date();
  const ts  = now.getFullYear() +
    String(now.getMonth()+1).padStart(2,'0') +
    String(now.getDate()).padStart(2,'0') +
    String(now.getHours()).padStart(2,'0') +
    String(now.getMinutes()).padStart(2,'0') +
    String(now.getSeconds()).padStart(2,'0');

  fmHideModal();
  toast(t('fm_saving'), 'info');

  const content = fmGenerateContent(_fmSaveChannel, fmt);
  if (!content) { toast(t('fm_no_data'), 'warn'); return; }

  /* Body: "name|format|ts\n<file content>" */
  const body = `${name}|${fmt}|${ts}\n${content}`;

  const ctrl = new AbortController();
  const timer = setTimeout(() => ctrl.abort(), 30000);
  let r;
  try {
    const resp = await fetch('/api/file/save', {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
      body: body,
      signal: ctrl.signal,
    });
    clearTimeout(timer);
    r = await resp.json();
  } catch (e) {
    clearTimeout(timer);
    toast(`${t('save_fail')}: ${e.message || 'network'}`, 'error');
    return;
  }

  if (r && r.ok) toast(`${t('fm_saved')}: ${ts.slice(0,8)}_${name}.${fmt}`, 'success');
  else toast(`${t('save_fail')}: ${r?.err || ''}`, 'error');
}

function fmGenerateContent(channel, fmt) {
  const MAX = channel === 'can' ? 300 : 500;

  if (channel === 'rs485' || channel === 'uart0' || channel === 'uart1') {
    const entries = (_logBuf[channel] || []).slice(-MAX);
    if (!entries.length) return null;
    if (fmt === 'csv') {
      const rows = ['Timestamp,Direction,Data(HEX),Bytes'];
      entries.forEach(([ts, data, dir]) => {
        const d = new Date(typeof ts === 'number' && ts > 1e9 ? ts : Date.now());
        const ds = d.toISOString().replace('T', ' ').slice(0, 19);
        const bytes = Math.floor((data || '').replace(/\s+/g, '').length / 2);
        rows.push(`${ds},${dir},"${data}",${bytes}`);
      });
      return rows.join('\r\n');
    } else {
      const chanName = channel.toUpperCase();
      const obj = {
        channel: chanName,
        saved_at: new Date().toISOString(),
        entries: entries.map(([ts, data, dir]) => ({
          ts: typeof ts === 'number' && ts > 1e9 ? ts : Date.now(),
          dir, data,
          bytes: Math.floor((data || '').replace(/\s+/g, '').length / 2)
        }))
      };
      return JSON.stringify(obj, null, 2);
    }
  }

  if (channel === 'can') {
    const entries = (_logBuf.can || []).slice(-MAX);
    if (!entries.length) return null;
    if (fmt === 'csv') {
      const rows = ['Timestamp,Direction,ID,Type,DLC,Data(HEX)'];
      entries.forEach(m => {
        const d = new Date(typeof m.ts === 'number' && m.ts > 1e9 ? m.ts : Date.now());
        const ds = d.toISOString().replace('T', ' ').slice(0, 19);
        const dir    = m._tx ? 'TX' : 'RX';
        const idHex  = '0x' + (m.id || 0).toString(16).toUpperCase().padStart(m.ext ? 8 : 3, '0');
        const type   = m.rtr ? (m.ext ? 'RTR-Ext' : 'RTR-Std') : (m.ext ? 'Ext' : 'Std');
        rows.push(`${ds},${dir},${idHex},${type},${m.dlc || 0},"${m.data || ''}"`);
      });
      return rows.join('\r\n');
    } else {
      const obj = {
        channel: 'CAN',
        saved_at: new Date().toISOString(),
        entries: entries.map(m => ({
          ts: m.ts, dir: m._tx ? 'TX' : 'RX',
          id: '0x' + (m.id || 0).toString(16).toUpperCase(),
          ext: !!m.ext, rtr: !!m.rtr, dlc: m.dlc || 0, data: m.data || ''
        }))
      };
      return JSON.stringify(obj, null, 2);
    }
  }

  return null;
}

// ═══════════════════════════════════════
//  UART
// ═══════════════════════════════════════
let _uartActivePort = 0;

window.uartSwitchPort = function(port) {
  _uartActivePort = port;
  [0,1].forEach(p => {
    const tab   = document.getElementById(`uart-tab-${p}`);
    const panel = document.getElementById(`uart-panel-${p}`);
    if (tab)   tab.className   = 'tab-btn' + (p===port?' active':'');
    if (panel) panel.style.display = p===port ? '' : 'none';
  });
};

window.uartRefreshAll = function() { uartRefreshStatus(0); uartRefreshStatus(1); };

window.uartFill = function(port, val) {
  const el = document.getElementById(`uart${port}-send-input`); if(el) el.value = val;
};

window.init_uart = function() {
  _logRestore('uart0');
  _logRestore('uart1');
  [0,1].forEach(p => {
    const ids = ['baud','bits','parity','stop','disp','fmt','eol'].map(k=>`uart${p}-${k}-sel`);
    ids.forEach(id => {
      const el = document.getElementById(id); if (!el) return;
      const v = localStorage.getItem(id); if (v) el.value = v;
      el.onchange = () => localStorage.setItem(id, el.value);
    });
    const inp = document.getElementById(`uart${p}-send-input`);
    if (inp) inp.addEventListener('keydown', e => { if (e.key==='Enter'&&!e.shiftKey){ e.preventDefault(); uartSend(p); }});
    const autoBtn = document.getElementById(`uart${p}-auto-btn`);
    if (autoBtn) autoBtn.onclick = () => autoToggle(`uart${p}`,`uart${p}-auto-btn`,`uart${p}-interval`,`uart${p}-auto-count`, () => uartSend(p));
    const pauseBtn = document.getElementById(`uart${p}-pause-btn`);
    if (pauseBtn) pauseBtn.onclick = () => togglePause(`uart${p}-console`,`uart${p}-pause-btn`);
    const clrBtn = document.getElementById(`uart${p}-hist-clear-btn`);
    if (clrBtn) clrBtn.onclick = () => {
      localStorage.removeItem(`uart${p}_hist`);
      histSyncToDevice(`uart${p}_hist`);
      uartHistRefresh(p);
    };
    histLoadFromDevice(`uart${p}_hist`).then(() => uartHistRefresh(p));
  });
  uartRefreshAll();
};

async function uartRefreshStatus(port) {
  const s = await apiGet('/api/uart/status');
  if (s._err || !s.ports) return;
  const ch = s.ports[port];
  if (!ch) return;
  const el = document.getElementById(`uart${port}-stat`);
  if (!el) return;
  el.innerHTML =
    `<div><dt>${t('baud_rate')}</dt><dd>${ch.baud}</dd></div>` +
    `<div><dt>${t('frame_fmt')}</dt><dd><code>${ch.format||'8N1'}</code></dd></div>` +
    `<div><dt>${t('tx_bytes')}</dt><dd>${ch.tx_bytes} B · ${ch.tx_frames||0} ${t('frames')}</dd></div>` +
    `<div><dt>${t('rx_bytes')}</dt><dd>${ch.rx_bytes} B · ${ch.rx_frames||0} ${t('frames')}</dd></div>` +
    `<div><dt>${t('state')}</dt><dd><span class="tag ${ch.running?'ok':'err'}">${ch.running?t('running'):t('not_init')}</span></dd></div>`;
  const g = id => document.getElementById(id);
  if (g(`uart${port}-baud-sel`)   && ch.baud)       g(`uart${port}-baud-sel`).value   = ch.baud;
  if (g(`uart${port}-bits-sel`)   && ch.bits)       g(`uart${port}-bits-sel`).value   = ch.bits;
  if (g(`uart${port}-parity-sel`) && ch.parity!=null) g(`uart${port}-parity-sel`).value = ch.parity;
  if (g(`uart${port}-stop-sel`)   && ch.stop)       g(`uart${port}-stop-sel`).value   = ch.stop;
}

window.uartApplyCfg = async function(port) {
  const g = id => document.getElementById(id);
  const baud   = parseInt(g(`uart${port}-baud-sel`)?.value   || '115200');
  const bits   = parseInt(g(`uart${port}-bits-sel`)?.value   || '8');
  const parity = parseInt(g(`uart${port}-parity-sel`)?.value || '0');
  const stop   = parseInt(g(`uart${port}-stop-sel`)?.value   || '1');
  wsSend({ type:'uart_cfg', port, baud, bits, parity, stop });
  const r = await apiPost('/api/uart/config', { port, baud, bits, parity, stop });
  if (!r._err) {
    localStorage.setItem(`uart${port}-baud-sel`, String(baud));
    toast(`UART${port} ${baud} · ${bits}${['N','E','O'][parity]||'N'}${stop} ${t('applied')}`, 'success');
    uartRefreshStatus(port);
  } else { toast(t('cfg_fail'), 'error'); }
};

window.uartSend = function(port) {
  const inp  = document.getElementById(`uart${port}-send-input`);
  const fmt  = document.getElementById(`uart${port}-fmt-sel`)?.value || 'hex';
  const eol  = document.getElementById(`uart${port}-eol-sel`)?.value || '';
  if (!inp || !inp.value.trim()) return;
  let data = inp.value.trim();
  if (fmt === 'ascii') { data = asciiToHex(data); if (eol) data = appendEol(data, eol); }
  histPush(`uart${port}_hist`, inp.value.trim());
  uartHistRefresh(port);
  wsSend({ type:'uart_send', port, data });
  appendUartLog(port, Date.now(), data, 'TX');
  inp.value = '';
};

function appendUartLog(port, ts, data, dir) {
  const consId = `uart${port}-console`;
  if (_paused[consId]) return;
  _logPush(port === 0 ? 'uart0' : 'uart1', [ts, data, dir]);
  const c = document.getElementById(consId);
  if (!c) return;
  const fmt = document.getElementById(`uart${port}-disp-sel`)?.value || 'hex';
  const byteCount = (data.replace(/\s+/g,'').length / 2) | 0;
  const row = document.createElement('div');
  row.className = `log-row ${dir==='TX'?'tx':'rx'}`;
  row.innerHTML = `<span class="ts">[${tsStr(ts)}]</span> <strong>${dir}</strong> <span class="byte-cnt">[${byteCount}B]</span> ${displayData(data, fmt)}`;
  c.appendChild(row);
  while (c.children.length > 1000) c.removeChild(c.firstChild);
  c.scrollTop = c.scrollHeight;
  const cnt = document.getElementById(`uart${port}-log-count`);
  if (cnt) cnt.textContent = c.children.length + ' ' + t('frames');
}

// ═══════════════════════════════════════
//  WiFi
// ═══════════════════════════════════════
window.init_wifi = function() {
  const g = id => document.getElementById(id);
  if (g('btn-scan'))           g('btn-scan').onclick           = wifiScan;
  if (g('btn-connect'))        g('btn-connect').onclick        = wifiConnect;
  if (g('btn-disconnect'))     g('btn-disconnect').onclick     = wifiDisconnect;
  if (g('btn-clear-wifi'))     g('btn-clear-wifi').onclick     = wifiClear;
  if (g('btn-refresh-wifi'))   g('btn-refresh-wifi').onclick   = wifiRefreshStatus;
  if (g('btn-save-ap-config')) g('btn-save-ap-config').onclick = wifiSaveApConfig;
  wifiRefreshStatus();
};

async function wifiRefreshStatus() {
  const s = await apiGet('/api/wifi/status');
  const el = document.getElementById('wifi-status');
  if (!el || s._err) return;
  const cls = s.state==='connected'?'ok':s.state==='connecting'?'warn':s.state==='ap_mode'?'info':'';
  el.innerHTML =
    `<div><dt>${t('state')}</dt><dd><span class="tag ${cls}">${s.state||'—'}</span></dd></div>` +
    `<div><dt>${t('currentSsid')}</dt><dd>${escHtml(s.ssid||'—')}</dd></div>` +
    `<div><dt>${t('ip')}</dt><dd>${s.ip||'—'}</dd></div>` +
    `<div><dt>AP SSID</dt><dd>${s.ap_ssid||'MatrixDebug'}</dd></div>`;
  const apDisplay = document.getElementById('ap-ssid-display');
  if (apDisplay) apDisplay.textContent = s.ap_ssid || 'MatrixDebug';
  const apInput = document.getElementById('ap-ssid-input');
  if (apInput && !apInput.value) apInput.value = s.ap_ssid || '';
  refreshStatus();
}

async function wifiScan() {
  const list = document.getElementById('wifi-list');
  const btn  = document.getElementById('btn-scan');
  if (!list) return;
  list.innerHTML = `<li class="empty"><span class="spinner"></span>${t('scanning')}</li>`;
  if (btn) { btn.disabled=true; btn.innerHTML=`<span class="spinner"></span>${t('scanning')}`; }
  const r = await apiGet('/api/wifi/scan');
  if (btn) { btn.disabled=false; btn.textContent=t('scan'); }
  if (r._err||!r.networks) { list.innerHTML=`<li class="empty">${t('error')}</li>`; return; }
  if (!r.networks.length)  { list.innerHTML=`<li class="empty">${t('no_networks')}</li>`; return; }
  list.innerHTML = r.networks.map(n => {
    const bars = n.rssi>-55?'▂▄▆█':n.rssi>-70?'▂▄▆_':n.rssi>-85?'▂▄__':'▂___';
    return `<li data-ssid="${escHtml(n.ssid)}">
      <div>${escHtml(n.ssid)} ${n.auth?'<span class="tag">🔒</span>':''}</div>
      <div class="meta">${bars} ${n.rssi} dBm · ${n.auth?t('encrypted'):t('open')}</div>
    </li>`;
  }).join('');
  list.querySelectorAll('li[data-ssid]').forEach(li => li.addEventListener('click', () => {
    const i = document.getElementById('wifi-ssid'); if(i) { i.value=li.dataset.ssid; i.focus(); }
  }));
}

async function wifiConnect() {
  const ssid = document.getElementById('wifi-ssid')?.value||'';
  const pwd  = document.getElementById('wifi-pwd')?.value||'';
  if (!ssid) { toast(t('enter_ssid'), 'warn'); return; }
  const r = await apiPost('/api/wifi/connect', { ssid, password: pwd });
  if (r._err) { toast(t('error'), 'error'); return; }
  toast(t('connecting'), 'info');
  setTimeout(wifiRefreshStatus, 3000);
  setTimeout(wifiRefreshStatus, 7000);
}

async function wifiDisconnect() {
  const r = await apiPost('/api/wifi/disconnect');
  toast(r._err?t('error'):t('disconnected'), r._err?'error':'success');
  setTimeout(wifiRefreshStatus, 600);
}

async function wifiClear() {
  if (!await confirmDialog(t('clearSaved')+'?')) return;
  const r = await apiPost('/api/wifi/clear');
  toast(r._err?t('error'):t('ok'), r._err?'error':'success');
  setTimeout(wifiRefreshStatus, 600);
}

async function wifiSaveApConfig() {
  const ssid = document.getElementById('ap-ssid-input')?.value?.trim() || '';
  const pass = document.getElementById('ap-pass-input')?.value || '';
  if (!ssid) { toast(t('enter_ap_ssid'), 'warn'); return; }
  if (pass && pass.length < 8) { toast(t('pwd_min8'), 'warn'); return; }
  const r = await apiPost('/api/wifi/ap_config', { ap_ssid: ssid, ap_password: pass });
  if (r._err) { toast(t('error'), 'error'); return; }
  toast(t('ap_saved'), 'success');
  const apDisplay = document.getElementById('ap-ssid-display');
  if (apDisplay) apDisplay.textContent = ssid;
  document.getElementById('ap-pass-input').value = '';
}

// ═══════════════════════════════════════
//  System
// ═══════════════════════════════════════
window.init_system = function() {
  const g = id => document.getElementById(id);
  if (g('btn-reboot'))       g('btn-reboot').onclick       = reboot;
  if (g('btn-sys-refresh'))  g('btn-sys-refresh').onclick  = sysRefresh;
  if (g('btn-stats-refresh'))g('btn-stats-refresh').onclick= sysStatsRefresh;
  if (g('relay-chk'))        g('relay-chk').addEventListener('change', e => relaySet(e.target.checked?1:0));
  themeRenderPalette();
  sysRefresh();
  relayRefresh();
  sysStatsRefresh();
};

async function relayRefresh() {
  const r = await apiGet('/api/relay/status');
  if (r._err) return;
  const chk = document.getElementById('relay-chk');
  const tag = document.getElementById('relay-state');
  if (chk) chk.checked = !!r.relay;
  if (tag) { tag.textContent = r.relay ? 'ON' : 'OFF'; tag.className = 'tag' + (r.relay ? ' ok' : ''); }
}

async function sysRefresh() {
  const s = await apiGet('/api/system/info');
  const el = document.getElementById('sys-info');
  if (!el||s._err) return;
  el.innerHTML =
    `<div><dt>${t('chip')}</dt><dd>${escHtml(s.chip||'—')}</dd></div>` +
    `<div><dt>${t('version')}</dt><dd>${escHtml(s.version||'—')}</dd></div>` +
    `<div><dt>${t('ip')}</dt><dd>${s.ip||'192.168.4.1'}</dd></div>` +
    `<div><dt>AP IP</dt><dd>192.168.4.1</dd></div>` +
    `<div><dt>WiFi</dt><dd>${escHtml(s.wifi_status||'—')}</dd></div>`;
}

function _setBar(barId, pctId, detailId, pct, detail) {
  const bar = document.getElementById(barId);
  const pctEl = document.getElementById(pctId);
  const detEl = document.getElementById(detailId);
  if (bar) { bar.style.width = pct + '%'; bar.className = 'stats-bar' + (pct > 85 ? ' err' : pct > 65 ? ' warn' : ''); }
  if (pctEl) pctEl.textContent = pct + '%';
  if (detEl && detail != null) detEl.textContent = detail;
}

function _fmtBytes(b) {
  if (b >= 1048576) return (b / 1048576).toFixed(1) + ' MB';
  if (b >= 1024)    return (b / 1024).toFixed(1) + ' KB';
  return b + ' B';
}

async function sysStatsRefresh() {
  const s = await apiGet('/api/system/stats');
  if (s._err) return;
  const cpuPct  = Math.min(100, s.cpu_pct || 0);
  const memPct  = s.heap_total > 0 ? Math.round(s.heap_used * 100 / s.heap_total) : 0;
  const flashPct= s.fm_total  > 0 ? Math.round(s.fm_used  * 100 / s.fm_total)  : 0;
  _setBar('stats-cpu-bar',   'stats-cpu-pct',   null,               cpuPct,   null);
  _setBar('stats-mem-bar',   'stats-mem-pct',   'stats-mem-detail', memPct,
    `${_fmtBytes(s.heap_used)} / ${_fmtBytes(s.heap_total)}  (${s.task_count} tasks)`);
  _setBar('stats-flash-bar', 'stats-flash-pct', 'stats-flash-detail', flashPct,
    `${_fmtBytes(s.fm_used)} / ${_fmtBytes(s.fm_total)}  (${s.fm_files} files)`);
}

async function reboot() {
  if (!await confirmDialog(t('rebootConfirm'))) return;
  toast(t('rebooting'), 'info');
  await apiPost('/api/system/reboot');
  setTimeout(() => location.reload(), 6000);
}

async function relaySet(v) {
  const r = await apiPost('/api/relay/set', { value: v });
  const ok = !r._err;
  if (ok) {
    const tag = document.getElementById('relay-state');
    if (tag) { tag.textContent = v ? 'ON' : 'OFF'; tag.className = 'tag' + (v ? ' ok' : ''); }
    toast(`${t('relay_lbl')} → ${v?'ON':'OFF'}`, 'success');
  } else {
    toast(t('relay_fail'), 'error');
    const chk = document.getElementById('relay-chk');
    if (chk) chk.checked = !v;
  }
}

// ── Log helpers ──
function clearConsoleEx(consoleId, countId) {
  const c = document.getElementById(consoleId); if(c) c.innerHTML='';
  const cnt = document.getElementById(countId);  if(cnt) cnt.textContent='0 '+t('frames');
  const ch = {'rs485-console':'rs485','uart0-console':'uart0','uart1-console':'uart1'}[consoleId];
  if (ch) _logClear(ch);
}
window.clearConsoleEx = clearConsoleEx;

function clearConsole(id) {
  const c = document.getElementById(id); if(c) c.innerHTML='';
}

function exportLog(consoleId, name) {
  const c = document.getElementById(consoleId); if(!c) return;
  const lines = [...c.querySelectorAll('.log-row')].map(r => r.textContent.trim()).join('\n');
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([lines], {type:'text/plain'}));
  a.download = `${name}_${Date.now()}.txt`;
  a.click();
}

function exportCanLog() {
  const tb = document.getElementById('can-log-body'); if(!tb) return;
  const header = `${t('can_th_ts')}\t${t('can_th_dir')}\t${t('can_th_id')}\t${t('can_th_type')}\t${t('can_th_dlc')}\t${t('can_th_data')}\n`;
  const rows = [...tb.querySelectorAll('tr')].map(r =>
    [...r.querySelectorAll('td')].map(c=>c.textContent.trim()).join('\t')
  ).join('\n');
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([header+rows], {type:'text/csv'}));
  a.download = `can_${Date.now()}.csv`;
  a.click();
}

// ── Helpers ──
function tsStr(ts) {
  return new Date(typeof ts==='number'&&ts>1e9?ts:Date.now()).toLocaleTimeString('zh-CN',{hour12:false,hour:'2-digit',minute:'2-digit',second:'2-digit'});
}

function escHtml(s) {
  return String(s==null?'':s).replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'})[c]);
}
function hexToAscii(hex) {
  return (hex||'').trim().replace(/\s+/g,'').match(/.{2}/g)?.map(b => {
    const v=parseInt(b,16); return (v>=32&&v<=126)?String.fromCharCode(v):'.';
  }).join('')||hex;
}
function asciiToHex(s) {
  return [...s].map(c=>c.charCodeAt(0).toString(16).padStart(2,'0').toUpperCase()).join('');
}

// ── Expose globals needed by HTML onclick ──
window.rs485RefreshStatus = rs485RefreshStatus;
window.canRefreshStatus   = canRefreshStatus;
window.rs485FillPreset    = window.rs485FillPreset;
window.uartFill           = window.uartFill;
window.exportLog          = exportLog;
window.fmLoadList         = fmLoadList;

// ═══════════════════════════════════════
//  OTA
// ═══════════════════════════════════════

const OTA_STATE_MAP = {
  idle:'ota_idle', checking:'ota_checking', up_to_date:'ota_up_to_date',
  available:'ota_available', updating:'ota_updating', success:'ota_success', failed:'ota_failed',
};
const OTA_STATE_CLS = {
  idle:'', checking:'warn', up_to_date:'ok', available:'warn',
  updating:'warn', success:'ok', failed:'err',
};

window.init_ota = function() {
  const g = id => document.getElementById(id);
  if (g('ota-refresh-btn')) g('ota-refresh-btn').onclick = otaRefresh;
  if (g('ota-save-btn'))    g('ota-save-btn').onclick    = otaSave;
  if (g('ota-check-btn'))   g('ota-check-btn').onclick   = otaCheck;
  if (g('ota-update-btn'))  g('ota-update-btn').onclick  = otaUpdate;
  otaRefresh();
};

async function otaRefresh() {
  const s = await apiGet('/api/ota/status');
  if (s._err) return;
  _otaRenderStatus(s);
}

function _otaRenderStatus(s) {
  const g = id => document.getElementById(id);
  if (g('ota-local-ver'))  g('ota-local-ver').textContent  = s.local_version  || '—';
  if (g('ota-remote-ver')) g('ota-remote-ver').textContent = s.remote_version || '—';
  if (g('ota-notes'))      g('ota-notes').textContent      = s.notes          || '—';

  const stKey = OTA_STATE_MAP[s.state];
  const stCls = OTA_STATE_CLS[s.state] || '';
  const stEl = g('ota-state-cell');
  if (stEl) stEl.innerHTML = `<span class="tag ${stCls}">${stKey ? t(stKey) : (s.state||'—')}</span>`;

  const progCard = g('ota-progress-card');
  if (progCard) progCard.style.display = (s.state === 'updating') ? '' : 'none';
  _otaSetProgress(s.progress || 0);

  const updateBtn = g('ota-update-btn');
  if (updateBtn) updateBtn.style.display = (s.state === 'available') ? '' : 'none';

  const msgEl = g('ota-status-msg');
  if (msgEl) msgEl.textContent = s.error ? (t('error') + ': ' + s.error) : '';

  /* Fill config fields only once (don't overwrite user edits) */
  const urlEl = g('ota-url-input');
  if (urlEl && !urlEl.dataset.loaded && s.check_url) {
    urlEl.value = s.check_url;
    urlEl.dataset.loaded = '1';
  }
  const autoEl = g('ota-auto-chk');
  if (autoEl && !autoEl.dataset.loaded) {
    autoEl.checked = !!s.auto_check;
    autoEl.dataset.loaded = '1';
  }
  const ivEl = g('ota-interval-input');
  if (ivEl && !ivEl.dataset.loaded && s.interval_min) {
    ivEl.value = s.interval_min;
    ivEl.dataset.loaded = '1';
  }
}

function _otaSetProgress(pct) {
  const bar = document.getElementById('ota-prog-bar');
  const txt = document.getElementById('ota-prog-text');
  if (bar) bar.style.width = pct + '%';
  if (txt) txt.textContent = pct + '%';
}

async function otaSave() {
  const g = id => document.getElementById(id);
  const url       = (g('ota-url-input')?.value || '').trim();
  const auto_check = g('ota-auto-chk')?.checked ?? false;
  const interval  = parseInt(g('ota-interval-input')?.value) || 60;
  const r = await apiPost('/api/ota/config', { url, auto_check, interval });
  toast(r._err ? t('save_fail') : t('saved'), r._err ? 'error' : 'success');
}

async function otaCheck() {
  const btn = document.getElementById('ota-check-btn');
  if (btn) { btn.disabled = true; btn.textContent = t('ota_checking'); }
  const r = await apiPost('/api/ota/check');
  if (r.status === 'busy') toast(t('ota_busy'), 'warn');
  else toast(t('ota_check_start'), 'info');
  if (btn) { btn.disabled = false; btn.textContent = t('check_update'); }
  /* Poll for result */
  setTimeout(otaRefresh, 2000);
  setTimeout(otaRefresh, 6000);
  setTimeout(otaRefresh, 12000);
}

async function otaUpdate() {
  if (!await confirmDialog(t('ota_update_confirm'))) return;
  const btn = document.getElementById('ota-update-btn');
  if (btn) { btn.disabled = true; btn.textContent = t('ota_updating'); }
  const r = await apiPost('/api/ota/update');
  if (r._err || r.status === 'error') {
    toast(t('ota_start_fail') + ': ' + (r.msg || r._err || ''), 'error');
    if (btn) { btn.disabled = false; btn.textContent = t('update_now'); }
  } else {
    toast(t('ota_started'), 'info');
    /* Show progress card */
    const pc = document.getElementById('ota-progress-card');
    if (pc) pc.style.display = '';
  }
}

/* WS push handlers */
function handleOtaState(m) {
  if (curTab === 'ota') _otaRenderStatus(m);
  if (m.state === 'success') {
    toast(t('ota_success_msg'), 'success');
    setTimeout(() => location.reload(), 8000);
  } else if (m.state === 'failed') {
    toast(t('ota_failed') + ': ' + (m.error || t('unknown_err')), 'error');
  } else if (m.state === 'available') {
    toast(t('ota_new_ver') + ' ' + (m.remote_version || ''), 'warn');
  }
}

function handleOtaProgress(pct) {
  if (curTab === 'ota') _otaSetProgress(pct);
}

// ── Theme ──
const THEME_PALETTE = [
  { key:'color_green',  accent:'#22c55e', accent2:'#16a34a' },
  { key:'color_blue',   accent:'#3b82f6', accent2:'#1d4ed8' },
  { key:'color_purple', accent:'#8b5cf6', accent2:'#6d28d9' },
  { key:'color_cyan',   accent:'#06b6d4', accent2:'#0891b2' },
  { key:'color_orange', accent:'#f59e0b', accent2:'#d97706' },
  { key:'color_pink',   accent:'#ec4899', accent2:'#be185d' },
];

function themeSetAccent(accent, accent2, save) {
  document.documentElement.style.setProperty('--accent', accent);
  document.documentElement.style.setProperty('--accent2', accent2);
  if (save !== false) {
    localStorage.setItem('theme_accent', accent);
    localStorage.setItem('theme_accent2', accent2);
  }
  document.querySelectorAll('.theme-swatch').forEach(el => {
    el.classList.toggle('active', el.dataset.accent === accent);
  });
}

function themeRenderPalette() {
  const el = document.getElementById('theme-palette');
  if (!el) return;
  const cur = localStorage.getItem('theme_accent') || '#22c55e';
  el.innerHTML = THEME_PALETTE.map(p =>
    `<div class="theme-swatch${p.accent===cur?' active':''}" title="${t(p.key)}"
      style="background:${p.accent}" data-accent="${p.accent}" data-accent2="${p.accent2}"
      onclick="themeSetAccent('${p.accent}','${p.accent2}')"></div>`
  ).join('') + `<label class="btn btn-sm btn-ghost" style="cursor:pointer;padding:4px 8px" title="${t('custom_color')}">
    <input type="color" id="theme-custom-pick" style="width:0;height:0;opacity:0;position:absolute" onchange="themeCustomPick(this.value)">
    ${t('custom_color')}
  </label>`;
}

function themeCustomPick(hex) {
  const darken = h => {
    const n = parseInt(h.slice(1),16);
    const r = Math.max(0, (n>>16) - 40), g = Math.max(0, ((n>>8)&0xff) - 40), b = Math.max(0, (n&0xff) - 40);
    return '#' + [r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('');
  };
  themeSetAccent(hex, darken(hex));
}

function themeInit() {
  const a  = localStorage.getItem('theme_accent');
  const a2 = localStorage.getItem('theme_accent2');
  if (a && a2) themeSetAccent(a, a2, false);
}

// ═══════════════════════════════════════════════
//  AI Assistant Module
// ═══════════════════════════════════════════════

const AI_DEFAULTS = {
  provider: 'openai',
  base_url: 'https://api.openai.com/v1',
  api_key: '',
  model: 'gpt-4o',
  system_prompt: '你是一个工业通讯协议分析专家，擅长分析 RS485 / CAN / UART 通讯数据帧。能够识别 Modbus RTU、CANopen、J1939、自定义帧等常见协议，给出清晰的技术解释和操作建议。你可以通过工具获取实时通讯数据，也可以直接向硬件发送测试命令。回答请简洁专业，必要时使用代码块格式化数据。',
  max_history: 20,
};

let _aiHistory = [];
let _aiRunning = false;

function aiLoadSettings() {
  try {
    return Object.assign({}, AI_DEFAULTS, JSON.parse(localStorage.getItem('ai_settings') || '{}'));
  } catch { return Object.assign({}, AI_DEFAULTS); }
}
function aiSaveSettings(s) { localStorage.setItem('ai_settings', JSON.stringify(s)); }

// ── Agent tool definitions ──
const AI_TOOLS_DEF = [
  {
    name: 'get_comm_log',
    description: 'Get recent communication frames from a channel. Returns timestamped TX/RX hex data.',
    parameters: {
      type: 'object',
      properties: {
        channel: { type: 'string', enum: ['rs485', 'uart0', 'uart1', 'can'], description: 'Communication channel' },
        count: { type: 'integer', description: 'Number of recent frames to retrieve (1-100)', default: 30 }
      },
      required: ['channel']
    }
  },
  {
    name: 'send_command',
    description: 'Send a hex command to a hardware communication interface (RS485, UART0, UART1). Use this to send Modbus queries, protocol test frames, etc.',
    parameters: {
      type: 'object',
      properties: {
        channel: { type: 'string', enum: ['rs485', 'uart0', 'uart1'], description: 'Target channel' },
        data: { type: 'string', description: 'Hex bytes separated by spaces, e.g. "01 03 00 00 00 01 84 0A"' }
      },
      required: ['channel', 'data']
    }
  },
  {
    name: 'get_system_info',
    description: 'Get current ESP32 system information: chip model, firmware version, IP address, WiFi status.',
    parameters: { type: 'object', properties: {} }
  }
];

function _aiToolsOpenAI() {
  return AI_TOOLS_DEF.map(d => ({ type: 'function', function: { name: d.name, description: d.description, parameters: d.parameters } }));
}
function _aiToolsAnthropic() {
  return AI_TOOLS_DEF.map(d => ({ name: d.name, description: d.description, input_schema: d.parameters }));
}

// ── Tool execution ──
async function aiExecuteTool(name, args) {
  try {
    if (name === 'get_comm_log') {
      const ch = args.channel || 'rs485';
      const cnt = Math.min(parseInt(args.count) || 30, 100);
      const buf = _logBuf[ch];
      if (!buf || !buf.length) return `[${ch.toUpperCase()}] No data captured yet.`;
      const frames = buf.slice(-cnt);
      let lines;
      if (ch === 'can') {
        lines = frames.map(f => `[${f.ts||0}ms] ${f._tx?'TX':'RX'} ID=0x${(f.id||0).toString(16).toUpperCase().padStart(3,'0')} DLC=${f.dlc||0} ${f.data||''}`);
      } else {
        lines = frames.map(([ts, data, dir]) => `[${ts||0}ms] ${dir||'RX'} ${data}`);
      }
      return `[${ch.toUpperCase()}] Last ${frames.length} frames:\n` + lines.join('\n');
    }

    if (name === 'send_command') {
      const ch = (args.channel || '').toLowerCase();
      const data = (args.data || '').trim();
      if (!data) return 'Error: data is empty';
      if (!['rs485', 'uart0', 'uart1'].includes(ch)) return `Error: channel "${ch}" not supported for send_command`;
      if (ch === 'rs485') {
        wsSend({ type: 'rs485_send', data });
      } else {
        wsSend({ type: 'uart_send', port: ch === 'uart1' ? 1 : 0, data });
      }
      return `Sent to ${ch.toUpperCase()}: ${data}`;
    }

    if (name === 'get_system_info') {
      const [info, stats] = await Promise.all([apiGet('/api/system/info'), apiGet('/api/system/stats')]);
      if (info._err) return 'Error: ' + info._err;
      const result = Object.assign({}, info);
      if (!stats._err) {
        result.cpu_pct       = (stats.cpu_pct || 0) + '%';
        result.heap_used     = _fmtBytes(stats.heap_used || 0);
        result.heap_total    = _fmtBytes(stats.heap_total || 0);
        result.heap_free     = _fmtBytes(stats.heap_free || 0);
        result.heap_used_pct = stats.heap_total > 0 ? Math.round((stats.heap_used||0)*100/stats.heap_total) + '%' : '?';
        result.flash_used    = _fmtBytes(stats.fm_used || 0);
        result.flash_total   = _fmtBytes(stats.fm_total || 0);
        result.flash_files   = stats.fm_files || 0;
        result.task_count    = stats.task_count || 0;
      }
      return JSON.stringify(result, null, 2);
    }

    return `Unknown tool: ${name}`;
  } catch (e) {
    return 'Tool execution error: ' + e.message;
  }
}

// ── OpenAI format API call ──
async function _aiCallOpenAI(messages, settings) {
  const url = (settings.base_url || 'https://api.openai.com/v1').replace(/\/$/, '') + '/chat/completions';
  const res = await fetch(url, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + settings.api_key },
    body: JSON.stringify({ model: settings.model, messages, tools: _aiToolsOpenAI(), tool_choice: 'auto', max_tokens: 2048, temperature: 0.2 })
  });
  if (!res.ok) { const e = await res.text(); throw new Error(`HTTP ${res.status}: ${e.slice(0, 240)}`); }
  const data = await res.json();
  const msg = data.choices?.[0]?.message;
  if (!msg) throw new Error('Empty response from API');
  if (msg.tool_calls?.length) {
    return { type: 'tool_calls', raw: msg, calls: msg.tool_calls.map(tc => ({ id: tc.id, name: tc.function.name, args: JSON.parse(tc.function.arguments || '{}') })) };
  }
  return { type: 'text', text: msg.content || '' };
}

// ── Anthropic format API call ──
async function _aiCallAnthropic(messages, settings) {
  let sysContent = '';
  const convMsgs = messages.filter(m => { if (m.role === 'system') { sysContent = m.content; return false; } return true; });
  const res = await fetch('https://api.anthropic.com/v1/messages', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'x-api-key': settings.api_key,
      'anthropic-version': '2023-06-01',
      'anthropic-dangerous-direct-browser-access': 'true'
    },
    body: JSON.stringify({ model: settings.model, max_tokens: 2048, system: sysContent, messages: convMsgs, tools: _aiToolsAnthropic() })
  });
  if (!res.ok) { const e = await res.text(); throw new Error(`HTTP ${res.status}: ${e.slice(0, 240)}`); }
  const data = await res.json();
  const toolBlocks = (data.content || []).filter(b => b.type === 'tool_use');
  if (toolBlocks.length) {
    return { type: 'tool_calls', raw: { role: 'assistant', content: data.content }, calls: toolBlocks.map(b => ({ id: b.id, name: b.name, args: b.input || {} })) };
  }
  return { type: 'text', text: (data.content || []).filter(b => b.type === 'text').map(b => b.text).join('\n') };
}

// ── Build messages array for API ──
function _aiBuildMessages(settings, extraCtx) {
  const msgs = [{ role: 'system', content: settings.system_prompt }];
  const hist = _aiHistory.slice(-(settings.max_history * 2));
  for (const h of hist) msgs.push({ role: h.role, content: h.content });
  if (extraCtx && msgs.length && msgs[msgs.length - 1].role === 'user') {
    msgs[msgs.length - 1] = { role: 'user', content: extraCtx + '\n\n---\n' + msgs[msgs.length - 1].content };
  }
  return msgs;
}

// ── Markdown renderer ──
function aiRenderMarkdown(text) {
  const parts = text.split(/(```[\s\S]*?```)/);
  return parts.map((p, i) => {
    if (i % 2 === 1) {
      const code = p.slice(3, -3).replace(/^[a-z0-9_+-]*\n/, '');
      return `<pre class="ai-code">${escHtml(code.trim())}</pre>`;
    }
    return escHtml(p)
      .replace(/`([^`\n]+)`/g, '<code>$1</code>')
      .replace(/\*\*([^*\n]+)\*\*/g, '<strong>$1</strong>')
      .replace(/\n/g, '<br>');
  }).join('');
}

// ── UI helpers ──
function _aiLog() { return document.getElementById('ai-chat-log'); }

function aiAppendBubble(role, text) {
  const log = _aiLog(); if (!log) return;
  const div = document.createElement('div');
  div.className = 'ai-bubble ai-bubble-' + (role === 'user' ? 'user' : 'assistant');
  if (role === 'error') {
    div.className = 'ai-bubble ai-bubble-assistant';
    div.innerHTML = `<div class="ai-bubble-text ai-error">${escHtml(text)}</div>`;
  } else if (role === 'user') {
    div.innerHTML = `<div class="ai-bubble-label">You</div><div class="ai-bubble-text">${escHtml(text)}</div>`;
  } else {
    div.innerHTML = `<div class="ai-bubble-label">AI</div><div class="ai-bubble-text ai-markdown">${aiRenderMarkdown(text)}</div>`;
  }
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

function aiAppendUserBubble(text, ctxInfo) {
  const log = _aiLog(); if (!log) return;
  const div = document.createElement('div');
  div.className = 'ai-bubble ai-bubble-user';
  let html = `<div class="ai-bubble-label">You</div>`;
  if (ctxInfo) html += `<div class="ai-ctx-badge">${escHtml(ctxInfo.channel.toUpperCase())} ×${ctxInfo.frames}</div>`;
  html += `<div class="ai-bubble-text">${escHtml(text)}</div>`;
  div.innerHTML = html;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

function aiAppendToolCall(name, args) {
  const log = _aiLog(); if (!log) return;
  const div = document.createElement('div');
  div.className = 'ai-tool-call';
  div.innerHTML = `<span class="ai-tool-icon">⚙</span><code>${escHtml(name)}(${escHtml(JSON.stringify(args))})</code>`;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

function aiAppendToolResult(name, result) {
  const log = _aiLog(); if (!log) return;
  const div = document.createElement('div');
  div.className = 'ai-tool-result';
  const preview = result.length > 300 ? result.slice(0, 300) + '…' : result;
  div.innerHTML = `<span class="ai-tool-icon">↩</span><code>${escHtml(preview)}</code>`;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

// ── Agent loop (core) ──
async function aiRunAgent(userMsg, ctxData) {
  const settings = aiLoadSettings();
  if (!settings.api_key) { aiAppendBubble('error', t('ai_no_key')); return; }

  _aiHistory.push({ role: 'user', content: userMsg });

  const ctxText = ctxData
    ? `[通讯数据上下文 - ${ctxData.channel.toUpperCase()} - ${ctxData.frames}帧]\n${ctxData.text}`
    : null;

  const messages = _aiBuildMessages(settings, ctxText);
  const callFn = settings.provider === 'anthropic' ? _aiCallAnthropic : _aiCallOpenAI;

  for (let loop = 0; loop < 6; loop++) {
    let resp;
    try { resp = await callFn(messages, settings); }
    catch (e) {
      aiAppendBubble('error', t('ai_api_error') + ': ' + e.message);
      _aiHistory.pop();
      return;
    }

    if (resp.type === 'text') {
      _aiHistory.push({ role: 'assistant', content: resp.text });
      aiAppendBubble('assistant', resp.text);
      return;
    }

    if (resp.type === 'tool_calls') {
      messages.push(resp.raw);
      const results = [];
      for (const call of resp.calls) {
        aiAppendToolCall(call.name, call.args);
        const result = await aiExecuteTool(call.name, call.args);
        aiAppendToolResult(call.name, result);
        results.push({ id: call.id, result });
      }
      if (settings.provider === 'anthropic') {
        messages.push({ role: 'user', content: results.map(r => ({ type: 'tool_result', tool_use_id: r.id, content: r.result })) });
      } else {
        for (const r of results) messages.push({ role: 'tool', tool_call_id: r.id, content: r.result });
      }
    }
  }
}

// ── Quick analyze (called from buttons) ──
function aiQuickAnalyze(channel) {
  const buf = _logBuf[channel];
  if (!buf || !buf.length) { toast(t('ai_no_data'), 'warn'); return; }
  if (curTab !== 'ai') { window.App.switchTab('ai'); setTimeout(() => aiQuickAnalyze(channel), 400); return; }
  const inp = document.getElementById('ai-input');
  if (inp) inp.value = t('ai_quick_prompt').replace('{ch}', channel.toUpperCase());
  const chSel = document.getElementById('ai-ctx-channel');
  if (chSel) { chSel.value = channel; document.getElementById('ai-attach-btn')?.click(); }
}
window.aiQuickAnalyze = aiQuickAnalyze;

// ── Tab init ──
window.init_ai = function() {
  const g = id => document.getElementById(id);

  // Config panel toggle
  const hdr = g('ai-cfg-hdr'), body = g('ai-cfg-body'), arrow = g('ai-cfg-arrow');
  if (hdr) hdr.onclick = () => {
    const open = body.style.display !== 'none';
    body.style.display = open ? 'none' : '';
    if (arrow) arrow.style.transform = open ? '' : 'rotate(180deg)';
  };

  // Load saved settings
  const s = aiLoadSettings();
  if (g('ai-provider-sel')) g('ai-provider-sel').value = s.provider;
  if (g('ai-base-url'))     g('ai-base-url').value     = s.base_url;
  if (g('ai-api-key'))      g('ai-api-key').value      = s.api_key;
  if (g('ai-model'))        g('ai-model').value        = s.model;
  if (g('ai-sys-prompt'))   g('ai-sys-prompt').value   = s.system_prompt;

  // Provider change → update hint URL
  if (g('ai-provider-sel')) g('ai-provider-sel').onchange = function() {
    if (this.value === 'anthropic') {
      if (g('ai-base-url') && (!g('ai-base-url').value || g('ai-base-url').value.includes('openai'))) {
        g('ai-base-url').value = 'https://api.anthropic.com/v1';
      }
      if (g('ai-model') && (!g('ai-model').value || g('ai-model').value.startsWith('gpt'))) {
        g('ai-model').value = 'claude-sonnet-4-6';
      }
    } else {
      if (g('ai-base-url') && (!g('ai-base-url').value || g('ai-base-url').value.includes('anthropic'))) {
        g('ai-base-url').value = 'https://api.openai.com/v1';
      }
    }
  };

  // Save config
  if (g('ai-save-cfg')) g('ai-save-cfg').onclick = () => {
    aiSaveSettings({
      provider:       g('ai-provider-sel')?.value || 'openai',
      base_url:       (g('ai-base-url')?.value || '').trim(),
      api_key:        (g('ai-api-key')?.value  || '').trim(),
      model:          (g('ai-model')?.value    || '').trim(),
      system_prompt:  g('ai-sys-prompt')?.value || AI_DEFAULTS.system_prompt,
      max_history:    20,
    });
    toast(t('saved'), 'success');
  };

  // Test connection
  if (g('ai-test-btn')) g('ai-test-btn').onclick = async () => {
    const st = aiLoadSettings();
    if (!st.api_key) { toast(t('ai_no_key'), 'warn'); return; }
    g('ai-test-btn').disabled = true;
    const testMsgs = [{ role: 'system', content: 'You are a test bot.' }, { role: 'user', content: 'Reply: OK' }];
    const fn = st.provider === 'anthropic' ? _aiCallAnthropic : _aiCallOpenAI;
    try {
      await fn(testMsgs, st);
      toast(t('ai_test_ok'), 'success');
    } catch (e) {
      toast(t('ai_test_fail') + ': ' + e.message.slice(0, 80), 'error');
    }
    g('ai-test-btn').disabled = false;
  };

  // Clear chat
  if (g('ai-clear-btn')) g('ai-clear-btn').onclick = () => {
    _aiHistory = [];
    const log = _aiLog(); if (log) log.innerHTML = '';
    toast(t('ai_cleared'), 'success');
  };

  // Context attach
  let _pendingCtx = null;
  if (g('ai-attach-btn')) g('ai-attach-btn').onclick = () => {
    const ch = g('ai-ctx-channel')?.value;
    const cnt = parseInt(g('ai-ctx-count')?.value) || 20;
    if (!ch) return;
    const buf = _logBuf[ch];
    if (!buf || !buf.length) { toast(t('ai_no_data'), 'warn'); return; }
    const frames = buf.slice(-cnt);
    let text;
    if (ch === 'can') {
      text = frames.map(f => `[${f.ts||0}ms] ${f._tx?'TX':'RX'} ID=0x${(f.id||0).toString(16).toUpperCase().padStart(3,'0')} DLC=${f.dlc||0} ${f.data||''}`).join('\n');
    } else {
      text = frames.map(([ts, data, dir]) => `[${ts||0}ms] ${dir||'RX'} ${data}`).join('\n');
    }
    _pendingCtx = { channel: ch, frames: frames.length, text };
    g('ai-attach-btn').textContent = `✓ ${ch.toUpperCase()} ×${frames.length}`;
    g('ai-attach-btn').classList.add('active');
  };

  // Send message
  const doSend = async () => {
    if (_aiRunning) return;
    const inp = g('ai-input');
    const msg = (inp?.value || '').trim();
    if (!msg) return;
    inp.value = '';

    const ctx = _pendingCtx;
    _pendingCtx = null;
    if (g('ai-attach-btn')) { g('ai-attach-btn').textContent = t('ai_attach'); g('ai-attach-btn').classList.remove('active'); }

    aiAppendUserBubble(msg, ctx);

    // Thinking indicator
    const thinkEl = document.createElement('div');
    thinkEl.className = 'ai-thinking'; thinkEl.id = 'ai-thinking-ind'; thinkEl.textContent = t('ai_thinking');
    const _logEl = _aiLog();
    if (_logEl) { _logEl.appendChild(thinkEl); _logEl.scrollTop = _logEl.scrollHeight; }

    _aiRunning = true;
    if (g('ai-send-btn')) { g('ai-send-btn').disabled = true; g('ai-send-btn').textContent = '…'; }
    try {
      await aiRunAgent(msg, ctx);
    } finally {
      _aiRunning = false;
      document.getElementById('ai-thinking-ind')?.remove();
      if (g('ai-send-btn')) { g('ai-send-btn').disabled = false; g('ai-send-btn').textContent = t('send'); }
    }
  };

  if (g('ai-send-btn')) g('ai-send-btn').onclick = doSend;
  if (g('ai-input')) g('ai-input').onkeydown = e => { if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); doSend(); } };
};

// ── Boot ──
function init() {
  themeInit();
  renderTabs();
  refreshStatus();
  setInterval(refreshStatus, 10000);
  wsConnect();
  switchTab(curTab);
}

window.App = { switchTab, toast, t, wsSend, escHtml };
window.switchTab = switchTab;
window.themeSetAccent = themeSetAccent;
window.themeCustomPick = themeCustomPick;

document.addEventListener('DOMContentLoaded', init);
})();
