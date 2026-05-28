let exec = null;
let toastNative = (msg) => console.log(msg);

const MOD = '/data/adb/modules/turbo-charge';
const CONFIG = `${MOD}/option.txt`;
const LOG = `${MOD}/log.txt`;
const BYPASS = `${MOD}/bypass_charge.txt`;

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
            try { window.ksu.toast(msg); } catch (e) {}
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

async function sh(cmd) {
    return await exec(cmd);
}

async function shOut(cmd) {
    const { stdout } = await sh(cmd);
    return stdout || '';
}

async function readFile(path) {
    const { errno, stdout } = await sh(`cat '${path}' 2>/dev/null`);
    if (errno !== 0) return '';
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
    CHARGE_STOP: 95, CHARGE_START: 80,
    TEMP_MAX: 52, HIGHEST_TEMP_CURRENT: 2000000, RECHARGE_TEMP: 45,
    TEMP_SIMULATE: 0, TEMP_SIMULATE_VALUE: 28,
    BYPASS_CHARGE: 0
};

let st = { lv: 0, status: '--', tmp: 0, inp: 0, out: 0, pwr: 0, volt: 0 };

const MAX_POINTS = 120;
let chartData = { labels: [], input: [], output: [] };

async function init() {
    initKsu();
    await loadCfg();
    await refresh();
    await refreshLog();
    setInterval(refresh, 5000);
    setInterval(refreshLog, 10000);
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
    try {
        const text = await readFile(CONFIG);
        if (text) {
            text.split('\n').forEach(line => {
                line = line.trim();
                if (line && !line.startsWith('#')) {
                    const match = line.match(/^(\w+)=(\d+)$/);
                    if (match) cfg[match[1]] = parseInt(match[2]);
                }
            });
        }
    } catch (e) {}
    
    document.getElementById('i-cur').value = cfg.CURRENT_MAX / 1000;
    document.getElementById('i-tmax').value = cfg.TEMP_MAX;
    document.getElementById('i-trcv').value = cfg.RECHARGE_TEMP;
    document.getElementById('i-cstp').value = cfg.CHARGE_STOP;
    document.getElementById('i-csta').value = cfg.CHARGE_START;
    document.getElementById('i-tsim').value = cfg.TEMP_SIMULATE_VALUE;
    
    ['TEMP_SIMULATE', 'STEP_CHARGING_DISABLED', 'TEMP_CTRL', 'POWER_CTRL', 'BYPASS_CHARGE'].forEach(k => {
        const el = document.getElementById('sw-' + k);
        if (el) el.checked = !!cfg[k];
    });
}

async function refresh() {
    try {
        const [cap, status, temp, volt, cur] = await Promise.all([
            shOut('cat /sys/class/power_supply/battery/capacity'),
            shOut('cat /sys/class/power_supply/battery/status'),
            shOut('cat /sys/class/power_supply/battery/temp'),
            shOut('cat /sys/class/power_supply/battery/voltage_now'),
            shOut('cat /sys/class/power_supply/battery/current_now')
        ]);
        
        st.lv = parseInt(cap) || 0;
        const s = (status || '').trim();
        st.status = s === 'Charging' ? '充电中' : s === 'Discharging' ? '放电中' : s === 'Full' ? '已充满' : s;
        st.tmp = (parseInt(temp) || 0) / 10;
        st.volt = (parseInt(volt) || 0) / 1000000;
        
        const curUA = parseInt(cur) || 0;
        if (st.status === '充电中') {
            st.inp = Math.abs(curUA) / 1000000;
            st.out = 0;
        } else if (st.status === '放电中') {
            st.inp = 0;
            st.out = Math.abs(curUA) / 1000000;
        } else {
            st.inp = 0;
            st.out = 0;
        }
        
        st.pwr = (st.inp > 0 ? st.inp * st.volt : st.out * st.volt).toFixed(1);
        updateChartData();
    } catch (err) {}
    updateUI();
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
    document.getElementById('v-lv').textContent = st.lv;
    
    const dot = document.getElementById('runDot');
    const stEl = document.getElementById('v-st');
    stEl.textContent = st.status;
    dot.className = 'dot ' + (st.status === '充电中' ? 'ok' : st.status === '放电中' ? 'fail' : '');
    
    document.getElementById('v-tmp').textContent = st.tmp.toFixed(1);
    document.getElementById('v-pwr').textContent = st.pwr;
    document.getElementById('v-inp').textContent = st.inp.toFixed(2);
    document.getElementById('v-out').textContent = st.out.toFixed(2);
    
    document.getElementById('b-lv').style.width = st.lv + '%';
    document.getElementById('p-lv').textContent = st.lv + '%';
    
    const tmpPct = Math.min(100, st.tmp / 60 * 100);
    document.getElementById('b-tmp').style.width = tmpPct + '%';
    document.getElementById('p-tmp').textContent = st.tmp.toFixed(1) + '℃';
    
    const pwrPct = Math.min(100, parseFloat(st.pwr) / 50 * 100);
    document.getElementById('b-pwr').style.width = pwrPct + '%';
    document.getElementById('p-pwr').textContent = st.pwr + 'W';
    
    document.getElementById('m-lv').textContent = st.status;
    document.getElementById('m-tmp').textContent = st.tmp > 45 ? '温度偏高' : '正常';
    
    const chartCur = document.getElementById('chart-cur');
    if (chartCur) {
        const curVal = st.inp > 0 ? st.inp : st.out;
        chartCur.textContent = curVal.toFixed(2) + ' A';
    }
    const chartPwr = document.getElementById('chart-pwr');
    if (chartPwr) chartPwr.textContent = st.pwr + ' W';
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
    
    const w = rect.width;
    const h = rect.height;
    const padding = { top: 30, right: 20, bottom: 40, left: 45 };
    const chartW = w - padding.left - padding.right;
    const chartH = h - padding.top - padding.bottom;
    
    ctx.clearRect(0, 0, w, h);
    
    if (chartData.labels.length < 2) {
        ctx.fillStyle = 'rgba(255,255,255,0.5)';
        ctx.textAlign = 'center';
        ctx.font = '14px sans-serif';
        ctx.fillText('等待数据...', w / 2, h / 2);
        return;
    }
    
    const allValues = [...chartData.input, ...chartData.output];
    const dataMax = Math.max(...allValues, 0.5);
    let maxVal;
    if (dataMax <= 1) maxVal = 1.5;
    else if (dataMax <= 3) maxVal = 4;
    else if (dataMax <= 5) maxVal = 6;
    else if (dataMax <= 8) maxVal = 10;
    else if (dataMax <= 12) maxVal = 15;
    else maxVal = Math.ceil(dataMax / 5) * 5;
    
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 0.5;
    ctx.fillStyle = 'rgba(255,255,255,0.5)';
    ctx.font = '10px sans-serif';
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
    
    ctx.fillStyle = 'rgba(255,255,255,0.5)';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    const xInterval = Math.max(1, Math.floor(chartData.labels.length / 6));
    for (let i = 0; i < chartData.labels.length; i += xInterval) {
        const x = padding.left + (chartW / (chartData.labels.length - 1)) * i;
        ctx.fillText(chartData.labels[i], x, h - padding.bottom + 15);
    }
    
    // 充电曲线（红色）
    ctx.strokeStyle = '#ff4757';
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < chartData.input.length; i++) {
        const x = padding.left + (chartW / (chartData.input.length - 1)) * i;
        const y = padding.top + chartH - (chartData.input[i] / maxVal * chartH);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();
    
    ctx.fillStyle = 'rgba(255, 71, 87, 0.1)';
    ctx.beginPath();
    ctx.moveTo(padding.left, padding.top + chartH);
    for (let i = 0; i < chartData.input.length; i++) {
        const x = padding.left + (chartW / (chartData.input.length - 1)) * i;
        const y = padding.top + chartH - (chartData.input[i] / maxVal * chartH);
        ctx.lineTo(x, y);
    }
    ctx.lineTo(padding.left + chartW, padding.top + chartH);
    ctx.closePath();
    ctx.fill();
    
    // 放电曲线（蓝色）
    ctx.strokeStyle = '#00d4ff';
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let i = 0; i < chartData.output.length; i++) {
        const x = padding.left + (chartW / (chartData.output.length - 1)) * i;
        const y = padding.top + chartH - (chartData.output[i] / maxVal * chartH);
        i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();
    
    ctx.fillStyle = 'rgba(0, 212, 255, 0.1)';
    ctx.beginPath();
    ctx.moveTo(padding.left, padding.top + chartH);
    for (let i = 0; i < chartData.output.length; i++) {
        const x = padding.left + (chartW / (chartData.output.length - 1)) * i;
        const y = padding.top + chartH - (chartData.output[i] / maxVal * chartH);
        ctx.lineTo(x, y);
    }
    ctx.lineTo(padding.left + chartW, padding.top + chartH);
    ctx.closePath();
    ctx.fill();
    
    // 图例
    ctx.fillStyle = '#ff4757';
    ctx.fillRect(padding.left, 10, 12, 12);
    ctx.fillStyle = 'rgba(255,255,255,0.8)';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText('充电', padding.left + 16, 20);
    
    ctx.fillStyle = '#00d4ff';
    ctx.fillRect(padding.left + 60, 10, 12, 12);
    ctx.fillStyle = 'rgba(255,255,255,0.8)';
    ctx.fillText('放电', padding.left + 76, 20);
    
    ctx.fillStyle = 'rgba(255,255,255,0.6)';
    ctx.font = '12px sans-serif';
    ctx.textAlign = 'right';
    const currentVal = st.inp > 0 ? st.inp : st.out;
    ctx.fillText(currentVal.toFixed(2) + 'A', w - padding.right, 20);
}

function buildConfigText() {
    return `# 配置更改实时生效
# CYCLE_TIME 必须大于 0，其他配置必须大于等于 0
# 所有配置都必须为整数，最大值为 2147483647

# 循环间隔时间，单位：秒
# 默认 1 秒，不建议大于 5 秒
CYCLE_TIME=${cfg.CYCLE_TIME}

# 最大充电电流，单位：μA
# 经测试超过手机理论最大充电电流一般不会出现问题，所以保持默认即可
CURRENT_MAX=${cfg.CURRENT_MAX}

# 是否关闭阶梯式充电
# 1 为关闭，其他数字为不关闭
STEP_CHARGING_DISABLED=${cfg.STEP_CHARGING_DISABLED}

# 关闭阶梯式充电的电量阈值
# 电量大于等于此值时关闭阶梯式充电
STEP_CHARGING_DISABLED_THRESHOLD=${cfg.STEP_CHARGING_DISABLED_THRESHOLD}

# 是否启用温控限流
# 1 为启用，其他数字为禁用
TEMP_CTRL=${cfg.TEMP_CTRL}

# 是否启用电量控制
# 1 为启用，其他数字为禁用
POWER_CTRL=${cfg.POWER_CTRL}

# 停止充电的电量阈值
# POWER_CTRL=1 时生效
CHARGE_STOP=${cfg.CHARGE_STOP}

# 恢复充电的电量阈值
# POWER_CTRL=1 时生效
CHARGE_START=${cfg.CHARGE_START}

# 降低充电电流的温度阈值，单位：℃
TEMP_MAX=${cfg.TEMP_MAX}

# 高于温度阈值时的最大充电电流，单位：μA
HIGHEST_TEMP_CURRENT=${cfg.HIGHEST_TEMP_CURRENT}

# 恢复快充的温度阈值，单位：℃
RECHARGE_TEMP=${cfg.RECHARGE_TEMP}

# 是否启用电池温度模拟功能
# 1 为启用，其他数字为禁用
TEMP_SIMULATE=${cfg.TEMP_SIMULATE}

# 模拟的电池温度，单位：℃
# TEMP_SIMULATE=1 时生效
TEMP_SIMULATE_VALUE=${cfg.TEMP_SIMULATE_VALUE}

# 是否启用“伪”旁路供电功能
# 1 为启用，其他数字为禁用
# 开启后请配置 /data/adb/modules/turbo-charge/bypass_charge.txt
BYPASS_CHARGE=${cfg.BYPASS_CHARGE}
`;
}

async function saveCfg() {
    cfg.CURRENT_MAX = parseInt(document.getElementById('i-cur').value) * 1000;
    cfg.TEMP_MAX = parseInt(document.getElementById('i-tmax').value);
    cfg.RECHARGE_TEMP = parseInt(document.getElementById('i-trcv').value);
    cfg.CHARGE_STOP = parseInt(document.getElementById('i-cstp').value);
    cfg.CHARGE_START = parseInt(document.getElementById('i-csta').value);
    cfg.TEMP_SIMULATE_VALUE = parseInt(document.getElementById('i-tsim').value);
    
    try {
        await writeFile(CONFIG, buildConfigText());
        toast('✅ 配置已保存');
    } catch (e) {
        toast('❌ 保存失败');
    }
}

async function toggleSw(k) {
    cfg[k] = document.getElementById('sw-' + k).checked ? 1 : 0;
    try {
        await writeFile(CONFIG, buildConfigText());
        toast(cfg[k] ? '✅ 已开启' : '❌ 已关闭');
    } catch (e) {
        cfg[k] = cfg[k] === 1 ? 0 : 1;
        document.getElementById('sw-' + k).checked = !!cfg[k];
        toast('❌ 设置失败');
    }
}

async function refreshLog() {
    const box = document.getElementById('log-box');
    try {
        const text = await shOut(`tail -50 '${LOG}'`);
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
        box.innerHTML = '<div style="color:var(--fail)">读取日志失败</div>';
    }
}

async function clearLog() {
    try {
        await sh(`: > '${LOG}'`);
        document.getElementById('log-box').innerHTML = '<div style="text-align:center;color:var(--text-3)">已清空</div>';
        toast('✅ 日志已清空');
    } catch (e) {
        toast('❌ 清空失败');
    }
}

async function resetCfg() {
    if (!confirm('确定恢复默认配置？')) return;
    cfg = {
        CYCLE_TIME: 1, CURRENT_MAX: 50000000,
        STEP_CHARGING_DISABLED: 0, STEP_CHARGING_DISABLED_THRESHOLD: 15,
        TEMP_CTRL: 1, POWER_CTRL: 0,
        CHARGE_STOP: 95, CHARGE_START: 80,
        TEMP_MAX: 52, HIGHEST_TEMP_CURRENT: 2000000, RECHARGE_TEMP: 45,
        TEMP_SIMULATE: 0, TEMP_SIMULATE_VALUE: 28,
        BYPASS_CHARGE: 0
    };
    try {
        await writeFile(CONFIG, buildConfigText());
        await loadCfg();
        toast('✅ 已恢复默认');
    } catch (e) {
        toast('❌ 恢复失败');
    }
}

document.addEventListener('DOMContentLoaded', init);
