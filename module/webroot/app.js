let exec = null;
let toastNative = (msg) => console.log(msg);

const MOD = '/data/adb/modules/turbo-charge';
const CONFIG = `${MOD}/option.txt`;
const LOG = `${MOD}/log.txt`;
const BYPASS = `${MOD}/bypass_charge.txt`;

const RING_CIRCUMFERENCE = 2 * Math.PI * 86;

function getCurrentDir(){
  if (document.currentScript && document.currentScript.src) {
    return document.currentScript.src.substring(0, document.currentScript.src.lastIndexOf('/') + 1);
  }
  const scripts = document.getElementsByTagName('script');
  for (let i = scripts.length - 1; i >= 0; i--) {
    const src = scripts[i].src || '';
    if (src.includes('app.js')) return src.substring(0, src.lastIndexOf('/') + 1);
  }
  return './';
}

const BASE_DIR = getCurrentDir();
const BG_CANDIDATES = [
  `${BASE_DIR}background.jpg`,
  `${BASE_DIR}background.jpeg`,
  `${BASE_DIR}background.png`,
  `${BASE_DIR}background.webp`
];

function initKsu() {
  if (typeof window.ksu !== 'undefined' && window.ksu.exec) {
    exec = (cmd) => new Promise((resolve, reject) => {
      const cb = 'cb_' + Date.now() + '_' + Math.floor(Math.random() * 9999);
      window[cb] = (errno, stdout, stderr) => {
        resolve({ errno, stdout, stderr });
        delete window[cb];
      };
      try {
        window.ksu.exec(cmd, "{}", cb);
      } catch (e) {
        delete window[cb];
        reject(e);
      }
    });
    toastNative = (msg) => {
      try {
        window.ksu.toast(msg);
      } catch (e) {
        console.warn('ksu.toast 调用失败', e);
      }
    };
  } else {
    exec = async () => ({ errno: 1, stdout: '', stderr: 'ksu unavailable' });
  }
}

function toast(msg) {
  toastNative(msg);
  const el = document.getElementById('toast');
  if (!el) return;
  el.textContent = msg;
  el.classList.add('show');
  clearTimeout(el._timer);
  el._timer = setTimeout(() => el.classList.remove('show'), 1800);
}

function renderIcons() {
  if (window.lucide && window.lucide.createIcons) {
    window.lucide.createIcons();
  }
}

async function sh(cmd) { return await exec(cmd); }

async function readFile(path) {
  const { errno, stdout, stderr } = await sh(`cat '${path}'`);
  if (errno !== 0) throw new Error((stderr || '').trim() || `读取 ${path} 失败`);
  return stdout;
}

// 节点读不到时返回 null，避免把失败当成 0 显示
async function readNode(path) {
  const { errno, stdout } = await sh(`cat '${path}'`);
  if (errno !== 0) return null;
  return stdout;
}

async function writeFile(path, content) {
  const text = String(content ?? '');
  let marker = '';
  do {
    marker = '__EOF_' + Date.now().toString(36) + '_' + Math.random().toString(36).slice(2, 10) + '__';
  } while (text.split(/\r?\n/).includes(marker));
  const { errno, stderr } = await sh(`cat > '${path}' <<'${marker}'
${text}
${marker}
chmod 0644 '${path}'`);
  if (errno !== 0) throw new Error(stderr || 'write failed');
}

let cfg = {
  CYCLE_TIME: 1, CURRENT_MAX: 50000000,
  STEP_CHARGING_DISABLED: 0, STEP_CHARGING_DISABLED_THRESHOLD: 15,
  TEMP_CTRL: 1, POWER_CTRL: 0,
  CHARGE_STOP: 95, CHARGE_START: 80, POWER_CTRL_MODE: 0,
  TEMP_LEVEL1: 45, TEMP_LEVEL1_CURRENT: 3000000,
  TEMP_LEVEL2: 50, TEMP_LEVEL2_CURRENT: 1000000,
  TEMP_MAX: 52,
  TEMP_SIMULATE: 0, TEMP_SIMULATE_MOUNT_MODE: 0, TEMP_SIMULATE_VALUE: 28,
  THERMAL_MOUNT_MODE: 0,
  MEIZU_DEVICE: 0, MEIZU_CHARGE_LEVEL: 10, MEIZU_THERMAL_SCHEME: 2,
  BYPASS_CHARGE: 0
};

let st = { lv: 0, status: '--', tmp: 0, inp: 0, out: 0, pwr: 0, volt: 0 };
const MAX_POINTS = 120;
let chartData = { labels: [], input: [], output: [] };

// ===== 外观设置 =====
const UI_KEY = 'tc_ui_settings';
let ui = { theme: 'dark', panelOpacity: 80, bgBlur: 2, dualCell: 0 };

function loadUi() {
  try {
    const raw = localStorage.getItem(UI_KEY);
    if (raw) Object.assign(ui, JSON.parse(raw));
  } catch (e) {}
  applyUi();
  const slOp = document.getElementById('sl-panel-opacity');
  const slBg = document.getElementById('sl-bg-blur');
  if (slOp) slOp.value = ui.panelOpacity;
  if (slBg) slBg.value = ui.bgBlur;
  document.getElementById('v-panel-opacity').textContent = ui.panelOpacity + '%';
  document.getElementById('v-bg-blur').textContent = ui.bgBlur;
  const dual = document.getElementById('sw-dual-cell');
  if (dual) dual.checked = !!ui.dualCell;
  const dualLabel = document.getElementById('label-dual-cell');
  if (dualLabel) dualLabel.textContent = ui.dualCell ? '开' : '关闭';
}

function saveUi() {
  try { localStorage.setItem(UI_KEY, JSON.stringify(ui)); } catch (e) {}
}

function applyUi() {
  document.body.classList.toggle('dark-mode', ui.theme === 'dark');
  document.documentElement.style.setProperty('--panel-opacity', (ui.panelOpacity / 100).toFixed(2));
  document.documentElement.style.setProperty('--bg-blur', ui.bgBlur + 'px');
  updateThemeBtn();
}

function updateThemeBtn() {
  const btn = document.getElementById('btn-theme');
  if (!btn) return;
  btn.innerHTML = `<i data-lucide="${ui.theme === 'dark' ? 'moon' : 'sun'}"></i>`;
  renderIcons();
}

function toggleTheme() {
  ui.theme = ui.theme === 'dark' ? 'light' : 'dark';
  applyUi();
  saveUi();
}

function setPanelOpacity(v) {
  ui.panelOpacity = parseInt(v);
  document.getElementById('v-panel-opacity').textContent = v + '%';
  applyUi();
  saveUi();
}

function setBgBlur(v) {
  ui.bgBlur = parseInt(v);
  document.getElementById('v-bg-blur').textContent = v;
  applyUi();
  saveUi();
}

function displayMultiplier() {
  return ui.dualCell ? 2 : 1;
}

function toggleDualCell() {
  const dual = document.getElementById('sw-dual-cell');
  ui.dualCell = dual && dual.checked ? 1 : 0;
  const label = document.getElementById('label-dual-cell');
  if (label) label.textContent = ui.dualCell ? '开' : '关闭';
  saveUi();
  updateUI();
  drawChart();
}

function loadBackgroundImage() {
  const bg = document.getElementById('bgLayer');
  if (!bg) return;
  let index = 0;
  const tryNext = () => {
    if (index >= BG_CANDIDATES.length) { bg.style.backgroundImage = 'none'; return; }
    const url = BG_CANDIDATES[index++];
    const img = new Image();
    img.onload = () => { bg.style.backgroundImage = `url("${url}")`; };
    img.onerror = tryNext;
    img.src = url;
  };
  tryNext();
}

// ===== 主逻辑 =====
async function init() {
  initKsu();
  loadUi();
  loadBackgroundImage();
  await loadCfg();
  await refresh();
  await refreshLog();
  renderIcons();
  setInterval(refresh, 1000);
  setInterval(refreshLog, 10000);
  // 配置页自动保存
  const cfgMap = {
    'i-cur': { key: 'CURRENT_MAX', mul: 1000 },
    'i-lv1': { key: 'TEMP_LEVEL1' },
    'i-lv1cur': { key: 'TEMP_LEVEL1_CURRENT', mul: 1000 },
    'i-lv2': { key: 'TEMP_LEVEL2' },
    'i-lv2cur': { key: 'TEMP_LEVEL2_CURRENT', mul: 1000 },
    'i-tmax': { key: 'TEMP_MAX' },
    'i-tsim': { key: 'TEMP_SIMULATE_VALUE' },
    'i-cstp': { key: 'CHARGE_STOP' },
    'i-csta': { key: 'CHARGE_START' }
  };
  const selMap = {
    'sel-TEMP_SIMULATE_MOUNT_MODE': 'TEMP_SIMULATE_MOUNT_MODE',
    'sel-THERMAL_MOUNT_MODE': 'THERMAL_MOUNT_MODE',
    'sel-POWER_CTRL_MODE': 'POWER_CTRL_MODE',
    'sel-MEIZU_CHARGE_LEVEL': 'MEIZU_CHARGE_LEVEL',
    'sel-MEIZU_THERMAL_SCHEME': 'MEIZU_THERMAL_SCHEME'
  };
  document.getElementById('page-config').addEventListener('input', e => {
    const el = e.target;
    if (el.type === 'range') return;
    if (cfgMap[el.id]) {
      const { key, mul } = cfgMap[el.id];
      cfg[key] = parseInt(el.value) * (mul || 1);
      saveOneCfg(key, cfg[key]);
    }
  });
  document.getElementById('page-config').addEventListener('change', e => {
    const el = e.target;
    if (el.matches('select') && selMap[el.id]) {
      cfg[selMap[el.id]] = parseInt(el.value);
      saveOneCfg(selMap[el.id], cfg[selMap[el.id]]);
    } else if (el.id === 'sw-dual-cell') {
      toggleDualCell();
    } else if (el.type === 'checkbox' && el.id.startsWith('sw-')) {
      toggleSw(el.id.slice(3));
    }
  });
}

function switchPage(p, el) {
  document.querySelectorAll('.page').forEach(x => x.classList.remove('active'));
  document.querySelectorAll('.nav-item').forEach(x => x.classList.remove('active'));
  document.getElementById('page-' + p).classList.add('active');
  if (el) el.classList.add('active');
  if (p === 'chart') drawChart();
  if (p === 'log') refreshLog();
}

async function loadCfg() {
  let text = '';

  try {
    text = await readFile(CONFIG);
  } catch (e) {
    toast('读取配置失败，界面显示的是默认值：' + e.message);
  }

  text.split('\n').forEach(line => {
    line = line.trim();
    if (line && !line.startsWith('#')) {
      const match = line.match(/^(\w+)=(\d+)$/);
      if (match) cfg[match[1]] = parseInt(match[2]);
    }
  });

  if (cfg.MEIZU_CHARGE_LEVEL < 1 || cfg.MEIZU_CHARGE_LEVEL > 10) cfg.MEIZU_CHARGE_LEVEL = 10;
  if (cfg.MEIZU_THERMAL_SCHEME < 1 || cfg.MEIZU_THERMAL_SCHEME > 2) cfg.MEIZU_THERMAL_SCHEME = 2;

  document.getElementById('i-cur').value = cfg.CURRENT_MAX / 1000;
  document.getElementById('i-tmax').value = cfg.TEMP_MAX;
  document.getElementById('i-cstp').value = cfg.CHARGE_STOP;
  document.getElementById('i-csta').value = cfg.CHARGE_START;
  document.getElementById('i-tsim').value = cfg.TEMP_SIMULATE_VALUE;
  document.getElementById('i-lv1').value = cfg.TEMP_LEVEL1;
  document.getElementById('i-lv1cur').value = cfg.TEMP_LEVEL1_CURRENT / 1000;
  document.getElementById('i-lv2').value = cfg.TEMP_LEVEL2;
  document.getElementById('i-lv2cur').value = cfg.TEMP_LEVEL2_CURRENT / 1000;

  ['TEMP_SIMULATE', 'STEP_CHARGING_DISABLED', 'TEMP_CTRL', 'POWER_CTRL', 'BYPASS_CHARGE', 'MEIZU_DEVICE'].forEach(k => {
    const el = document.getElementById('sw-' + k);
    const label = document.getElementById('label-' + k);
    if (el) el.checked = !!cfg[k];
    if (label) label.textContent = cfg[k] ? '开' : '关闭';
  });

  const simModeEl = document.getElementById('sel-TEMP_SIMULATE_MOUNT_MODE');
  if (simModeEl) simModeEl.value = cfg.TEMP_SIMULATE_MOUNT_MODE;
  const thModeEl = document.getElementById('sel-THERMAL_MOUNT_MODE');
  if (thModeEl) thModeEl.value = cfg.THERMAL_MOUNT_MODE;

  const modeEl = document.getElementById('sel-POWER_CTRL_MODE');
  if (modeEl) modeEl.value = cfg.POWER_CTRL_MODE;
  const meizuLevelEl = document.getElementById('sel-MEIZU_CHARGE_LEVEL');
  if (meizuLevelEl) meizuLevelEl.value = cfg.MEIZU_CHARGE_LEVEL;
  const meizuThermalEl = document.getElementById('sel-MEIZU_THERMAL_SCHEME');
  if (meizuThermalEl) meizuThermalEl.value = cfg.MEIZU_THERMAL_SCHEME;
}

async function refresh() {
  try {
    const [cap, status, temp, volt, cur] = await Promise.all([
      readNode('/sys/class/power_supply/battery/capacity'),
      readNode('/sys/class/power_supply/battery/status'),
      readNode('/sys/class/power_supply/battery/temp'),
      readNode('/sys/class/power_supply/battery/voltage_now'),
      readNode('/sys/class/power_supply/battery/current_now')
    ]);

    if (status === null && cap === null && temp === null) {
      // 节点全部读不到时保留上一次数值，并在界面上说明原因
      st.status = '读取失败';
      reportRefreshFailure('无法读取电池节点，请确认已授予 root 权限');
      updateUI();
      return;
    }

    refreshFailureReported = false;

    if (cap !== null) st.lv = parseInt(cap) || 0;
    const s = (status || '').trim();
    st.status = s === 'Charging' ? '充电中' : s === 'Discharging' ? '放电中' : s === 'Full' ? '已充满' : (s || '未知');
    if (temp !== null) st.tmp = (parseInt(temp) || 0) / 10;
    if (volt !== null) st.volt = (parseInt(volt) || 0) / 1000000;

    let curUA = parseInt(cur) || 0;
    if (curUA !== 0 && Math.abs(curUA) < 50000) curUA *= 1000;
    // current_now 在部分设备充电时为负数，按 status 为准取绝对值
    if (st.status === '充电中') { st.inp = Math.abs(curUA) / 1000000; st.out = 0; }
    else if (st.status === '放电中') { st.inp = 0; st.out = Math.abs(curUA) / 1000000; }
    else { st.inp = 0; st.out = 0; }

    st.pwr = st.inp > 0 ? st.inp * st.volt : st.out * st.volt;
    updateChartData();
  } catch (err) {
    reportRefreshFailure('刷新电池状态失败：' + (err && err.message ? err.message : err));
  }
  updateUI();
}

let refreshFailureReported = false;

// 刷新每秒执行，同一种失败只提示一次，但不能完全吞掉
function reportRefreshFailure(msg) {
  console.warn(msg);
  if (refreshFailureReported) return;
  refreshFailureReported = true;
  toast(msg);
}

function updateChartData() {
  const now = new Date();
  const timeStr = now.getHours().toString().padStart(2, '0') + ':' +
                  now.getMinutes().toString().padStart(2, '0') + ':' +
                  now.getSeconds().toString().padStart(2, '0');
  chartData.labels.push(timeStr);
  chartData.input.push(st.inp);
  chartData.output.push(st.out);
  if (chartData.labels.length > MAX_POINTS) {
    chartData.labels.shift();
    chartData.input.shift();
    chartData.output.shift();
  }
}

function updateUI() {
  const pct = Math.max(0, Math.min(100, st.lv));
  const offset = RING_CIRCUMFERENCE * (1 - pct / 100);
  const ringFill = document.getElementById('ring-fill');
  const ringGlow = document.getElementById('ring-glow');

  let ringColor = 'var(--accent)';
  if (st.status === '充电中') ringColor = 'var(--ok)';
  else if (st.status === '放电中') ringColor = 'var(--fail)';

  ringFill.style.strokeDashoffset = offset;
  ringFill.style.stroke = ringColor;
  ringGlow.style.strokeDashoffset = offset;
  ringGlow.style.stroke = ringColor;
  ringGlow.classList.toggle('ring-pulse', st.status === '充电中');

  document.getElementById('ring-pct').textContent = pct;
  const statusEl = document.getElementById('ring-status');
  statusEl.textContent = st.status;
  statusEl.className = 'battery-ring-status ' +
    (st.status === '充电中' ? 'charging' : st.status === '放电中' ? 'discharging' : 'full');

  document.getElementById('v-tmp').textContent = st.tmp.toFixed(1);
  const mul = displayMultiplier();
  const displayInp = st.inp * mul;
  const displayOut = st.out * mul;
  const displayPwr = st.pwr * mul;
  document.getElementById('v-pwr').textContent = displayPwr.toFixed(1);
  document.getElementById('v-inp').textContent = displayInp.toFixed(2);
  document.getElementById('v-out').textContent = displayOut.toFixed(2);

  document.getElementById('p-lv').textContent = pct + '%';
  document.getElementById('b-lv').style.width = pct + '%';
  document.getElementById('m-lv').textContent = st.status;

  const tmpPct = Math.min(100, st.tmp / 60 * 100);
  document.getElementById('b-tmp').style.width = tmpPct + '%';
  document.getElementById('p-tmp').textContent = st.tmp.toFixed(1) + '℃';
  document.getElementById('m-tmp').textContent = st.tmp > 45 ? '温度偏高' : '正常';

  const pwrPct = Math.min(100, displayPwr / 50 * 100);
  document.getElementById('b-pwr').style.width = pwrPct + '%';
  document.getElementById('p-pwr').textContent = displayPwr.toFixed(1) + 'W';

  const chartCur = document.getElementById('chart-cur');
  if (chartCur) {
    const curVal = st.inp > 0 ? displayInp : displayOut;
    chartCur.textContent = curVal.toFixed(2) + ' A';
  }
  const chartPwr = document.getElementById('chart-pwr');
  if (chartPwr) chartPwr.textContent = displayPwr.toFixed(1) + ' W';
  const chartPts = document.getElementById('chart-pts');
  if (chartPts) chartPts.textContent = chartData.labels.length;
}

function drawChart() {
  const canvas = document.getElementById('chartCanvas');
  if (!canvas) return;
  const ctx = canvas.getContext('2d');
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  canvas.width = rect.width * dpr;
  canvas.height = rect.height * dpr;
  ctx.scale(dpr, dpr);

  const w = rect.width, h = rect.height;
  const padding = { top: 20, right: 16, bottom: 32, left: 40 };
  const chartW = w - padding.left - padding.right;
  const chartH = h - padding.top - padding.bottom;
  ctx.clearRect(0, 0, w, h);

  const isLight = !document.body.classList.contains('dark-mode');
  const cText = isLight ? 'rgba(0,0,0,.35)' : 'rgba(255,255,255,.35)';
  const cText2 = isLight ? 'rgba(0,0,0,.5)' : 'rgba(255,255,255,.5)';
  const cGrid = isLight ? 'rgba(0,0,0,.05)' : 'rgba(255,255,255,.05)';

  if (chartData.labels.length < 2) {
    ctx.fillStyle = cText;
    ctx.textAlign = 'center';
    ctx.font = '13px -apple-system,sans-serif';
    ctx.fillText('等待数据...', w / 2, h / 2);
    return;
  }

  const mul = displayMultiplier();
  const displayInput = chartData.input.map(v => v * mul);
  const displayOutput = chartData.output.map(v => v * mul);
  const allValues = [...displayInput, ...displayOutput];
  const dataMax = Math.max(...allValues, 0.5);
  let maxVal;
  if (dataMax <= 1) maxVal = 1.5;
  else if (dataMax <= 3) maxVal = 4;
  else if (dataMax <= 5) maxVal = 6;
  else if (dataMax <= 8) maxVal = 10;
  else if (dataMax <= 12) maxVal = 15;
  else maxVal = Math.ceil(dataMax / 5) * 5;

  ctx.strokeStyle = cGrid;
  ctx.lineWidth = 0.5;
  ctx.fillStyle = cText;
  ctx.font = '10px -apple-system,sans-serif';
  ctx.textAlign = 'right';

  const ySteps = 5;
  for (let i = 0; i <= ySteps; i++) {
    const y = padding.top + (chartH / ySteps) * i;
    ctx.beginPath();
    ctx.moveTo(padding.left, y);
    ctx.lineTo(w - padding.right, y);
    ctx.stroke();

    const val = maxVal - (maxVal / ySteps) * i;
    ctx.fillText(val.toFixed(1), padding.left - 5, y + 3);
  }

  ctx.fillStyle = cText;
  ctx.font = '10px -apple-system,sans-serif';
  ctx.textAlign = 'center';
  const xInterval = Math.max(1, Math.floor(chartData.labels.length / 6));
  for (let i = 0; i < chartData.labels.length; i += xInterval) {
    const x = padding.left + (chartW / (chartData.labels.length - 1)) * i;
    ctx.fillText(chartData.labels[i], x, h - padding.bottom + 14);
  }

  ctx.strokeStyle = '#ff4757';
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  ctx.beginPath();
  for (let i = 0; i < displayInput.length; i++) {
    const x = padding.left + (chartW / (displayInput.length - 1)) * i;
    const y = padding.top + chartH - (displayInput[i] / maxVal * chartH);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();

  ctx.fillStyle = 'rgba(255,71,87,.10)';
  ctx.beginPath();
  ctx.moveTo(padding.left, padding.top + chartH);
  for (let i = 0; i < displayInput.length; i++) {
    const x = padding.left + (chartW / (displayInput.length - 1)) * i;
    const y = padding.top + chartH - (displayInput[i] / maxVal * chartH);
    ctx.lineTo(x, y);
  }
  ctx.lineTo(padding.left + chartW, padding.top + chartH);
  ctx.closePath();
  ctx.fill();

  ctx.strokeStyle = '#00d4ff';
  ctx.lineWidth = 2;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';
  ctx.beginPath();
  for (let i = 0; i < displayOutput.length; i++) {
    const x = padding.left + (chartW / (displayOutput.length - 1)) * i;
    const y = padding.top + chartH - (displayOutput[i] / maxVal * chartH);
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();

  ctx.fillStyle = 'rgba(0,212,255,.10)';
  ctx.beginPath();
  ctx.moveTo(padding.left, padding.top + chartH);
  for (let i = 0; i < displayOutput.length; i++) {
    const x = padding.left + (chartW / (displayOutput.length - 1)) * i;
    const y = padding.top + chartH - (displayOutput[i] / maxVal * chartH);
    ctx.lineTo(x, y);
  }
  ctx.lineTo(padding.left + chartW, padding.top + chartH);
  ctx.closePath();
  ctx.fill();

  ctx.fillStyle = '#ff4757';
  ctx.beginPath();
  ctx.roundRect(padding.left, 6, 10, 3, 1.5);
  ctx.fill();
  ctx.fillStyle = cText2;
  ctx.font = '11px -apple-system,sans-serif';
  ctx.textAlign = 'left';
  ctx.fillText('充电', padding.left + 14, 13);

  ctx.fillStyle = '#00d4ff';
  ctx.beginPath();
  ctx.roundRect(padding.left + 52, 6, 10, 3, 1.5);
  ctx.fill();
  ctx.fillStyle = cText2;
  ctx.fillText('放电', padding.left + 66, 13);

  ctx.fillStyle = cText2;
  ctx.font = '11px -apple-system,sans-serif';
  ctx.textAlign = 'right';
  const curVal = (st.inp > 0 ? st.inp : st.out) * mul;
  ctx.fillText(curVal.toFixed(2) + 'A', w - padding.right, 13);
}

// 读不到现有配置时必须放弃写入，否则会把整个配置文件覆盖成只包含本次修改的一行
async function saveCfgValues(entries) {
  let text;

  try {
    text = await readFile(CONFIG);
  } catch (e) {
    toast('保存失败，无法读取现有配置：' + e.message);
    return false;
  }

  let out = text.split('\n');

  for (const [key, value] of entries) {
    let found = false;
    out = out.map(line => {
      const m = line.match(/^(\w+)=/);
      if (m && m[1] === key) { found = true; return key + '=' + value; }
      return line;
    });
    if (!found) out.push(key + '=' + value);
  }

  try {
    await writeFile(CONFIG, out.join('\n'));
  } catch (e) {
    toast('保存失败：' + e.message);
    return false;
  }

  return true;
}

async function saveOneCfg(key, value) {
  if (await saveCfgValues([[key, value]])) {
    toast('已保存');
    return true;
  }

  return false;
}

async function toggleSw(k) {
  cfg[k] = document.getElementById('sw-' + k).checked ? 1 : 0;
  const label = document.getElementById('label-' + k);
  if (label) label.textContent = cfg[k] ? '开' : '关闭';
  await saveOneCfg(k, cfg[k]);
}

async function refreshLog() {
  const box = document.getElementById('log-box');
  try {
    const { errno, stdout, stderr } = await sh(`tail -50 '${LOG}'`);
    if (errno !== 0) throw new Error((stderr || '').trim() || `读取 ${LOG} 失败`);

    const text = stdout || '';
    if (text && text.trim()) {
      const lines = text.split('\n').filter(x => x.trim());
      box.innerHTML = lines.map(x => {
        let cls = '';
        if (x.includes('失效') || x.includes('错误') || x.includes('失败')) cls = 'e';
        else if (x.includes('警告') || x.includes('限制')) cls = 'w';
        else if (x.includes('启动') || x.includes('检测') || x.includes('找到')) cls = 'i';
        x = x.replace(/(\d{4}\.\d{2}\.\d{2}T\d{2}:\d{2}:\d{2})/, '<span class="t">$1</span>');
        return `<div class="${cls}">${x}</div>`;
      }).join('');
      box.scrollTop = box.scrollHeight;
    } else {
      box.innerHTML = '<div style="text-align:center;color:var(--text-3)">暂无日志</div>';
    }
  } catch (e) {
    box.innerHTML = `<div style="color:var(--fail)">读取日志失败：${e.message}</div>`;
  }
}

async function clearLog() {
  try {
    const { errno, stderr } = await sh(`: > '${LOG}'`);
    if (errno !== 0) throw new Error((stderr || '').trim() || '清空日志失败');

    document.getElementById('log-box').innerHTML = '<div style="text-align:center;color:var(--text-3)">已清空</div>';
    toast('日志已清空');
  } catch (e) {
    toast('清空失败：' + e.message);
  }
}

async function resetCfg() {
  if (!confirm('确定恢复默认配置？')) return;
  const defaults = {
    CYCLE_TIME: 1, CURRENT_MAX: 50000000,
    STEP_CHARGING_DISABLED: 0, STEP_CHARGING_DISABLED_THRESHOLD: 15,
    TEMP_CTRL: 1, POWER_CTRL: 0,
    CHARGE_STOP: 95, CHARGE_START: 80, POWER_CTRL_MODE: 0,
    TEMP_LEVEL1: 45, TEMP_LEVEL1_CURRENT: 3000000,
    TEMP_LEVEL2: 50, TEMP_LEVEL2_CURRENT: 1000000,
    TEMP_MAX: 52,
    TEMP_SIMULATE: 0, TEMP_SIMULATE_MOUNT_MODE: 0, TEMP_SIMULATE_VALUE: 28,
    THERMAL_MOUNT_MODE: 0,
    MEIZU_DEVICE: 0, MEIZU_CHARGE_LEVEL: 10, MEIZU_THERMAL_SCHEME: 2,
    BYPASS_CHARGE: 0
  };
  // 一次写入全部默认值，避免中途失败时只写进一半
  if (!await saveCfgValues(Object.entries(defaults))) return;

  Object.assign(cfg, defaults);
  await loadCfg();
  toast('已恢复默认配置');
}

document.addEventListener('DOMContentLoaded', init);
