# WebUI Dashboard Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Polish the module WebUI into a liquid-glass charging dashboard without changing KernelSU behavior.

**Architecture:** Keep the existing static HTML/CSS/JS structure. Update markup and styles for typography, Lucide icons, layout, and responsive polish; keep `app.js` business logic intact except for icon/theme button text handling.

**Tech Stack:** Static HTML, CSS, vanilla JavaScript, KernelSU WebUI bridge, Google Fonts, Lucide browser script.

---

## File Structure

- Modify: `charge-boost/webroot/index.html`
  - Add Google Fonts and Lucide script includes.
  - Replace emoji labels with icon elements and accessible text.
  - Replace inline layout styles with semantic classes.
- Modify: `charge-boost/webroot/style.css`
  - Add font tokens, icon styles, topbar actions, panel headers, compact metric grid behavior, and non-emoji navigation styling.
  - Remove system-font-first stack and tighten mobile layout.
- Modify: `charge-boost/webroot/app.js`
  - Change theme button from emoji text to Lucide icon names.
  - Initialize Lucide icons after DOM updates where needed.

### Task 1: HTML Structure And Icon Markup

**Files:**
- Modify: `charge-boost/webroot/index.html`

- [ ] **Step 1: Add external visual resources**

Add these elements in `<head>` before `style.css`:

```html
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Manrope:wght@500;600;700;800&family=Outfit:wght@600;700;800&display=swap" rel="stylesheet">
<script src="https://unpkg.com/lucide@latest/dist/umd/lucide.min.js" defer></script>
```

- [ ] **Step 2: Replace topbar emoji and inline styles**

Change the topbar avatar and action wrapper to:

```html
<div class="topbar-avatar"><i data-lucide="zap"></i></div>
<div class="topbar-actions">
  <button class="btn icon-btn ghost" id="btn-theme" onclick="toggleTheme()" aria-label="切换主题"><i data-lucide="moon"></i></button>
  <button class="btn" onclick="refresh()"><i data-lucide="rotate-cw"></i><span>刷新</span></button>
</div>
```

- [ ] **Step 3: Replace panel-title emoji labels**

Use this pattern for every title:

```html
<div class="panel-title"><i data-lucide="battery-charging"></i><span>电池信息</span></div>
```

Use `activity` for the chart page, `settings` for basic parameters, `sliders-horizontal` for switches, `palette` for appearance, and `file-text` for logs.

- [ ] **Step 4: Replace card and nav emoji icons**

Replace metric icon text with Lucide icons:

```html
<div class="icon temp"><i data-lucide="thermometer"></i></div>
<div class="icon power"><i data-lucide="zap"></i></div>
<div class="icon input"><i data-lucide="arrow-down"></i></div>
<div class="icon output"><i data-lucide="arrow-up"></i></div>
```

Replace bottom nav labels with icon plus text:

```html
<button class="nav-item active" onclick="switchPage('status',this)"><i data-lucide="gauge"></i><span>状态</span></button>
```

Use `activity`, `settings`, and `file-text` for the remaining nav items.

### Task 2: CSS Polish

**Files:**
- Modify: `charge-boost/webroot/style.css`

- [ ] **Step 1: Update font stack**

Set the body font stack to:

```css
font-family:"Manrope","Noto Sans SC","Microsoft YaHei",sans-serif;
```

Set headings and large numeric display to:

```css
font-family:"Outfit","Manrope","Microsoft YaHei",sans-serif;
```

- [ ] **Step 2: Add icon sizing classes**

Add:

```css
i[data-lucide]{
  width:1em;
  height:1em;
  stroke-width:2.25;
  display:block;
}

.topbar-actions{
  display:flex;
  align-items:center;
  gap:8px;
}

.icon-btn{
  width:36px;
  padding:0;
  display:inline-flex;
  align-items:center;
  justify-content:center;
}

.btn{
  display:inline-flex;
  align-items:center;
  justify-content:center;
  gap:7px;
}
```

- [ ] **Step 3: Tighten metric grid responsiveness**

Change the mobile rule so `.info-grid` remains two columns on normal phones:

```css
@media(max-width:420px){
  .info-grid{grid-template-columns:1fr}
}
```

Keep `.setting-item` stacking on small screens.

- [ ] **Step 4: Update panel title layout**

Add:

```css
.panel-title{
  display:flex;
  align-items:center;
  gap:8px;
}

.panel-title i{
  color:var(--accent);
  flex:0 0 auto;
}
```

### Task 3: JavaScript Icon Lifecycle

**Files:**
- Modify: `charge-boost/webroot/app.js`

- [ ] **Step 1: Add icon renderer helper**

Add:

```js
function renderIcons() {
  if (window.lucide && window.lucide.createIcons) {
    window.lucide.createIcons();
  }
}
```

- [ ] **Step 2: Update theme button icon**

Replace `updateThemeBtn()` text assignment with:

```js
function updateThemeBtn() {
  const btn = document.getElementById('btn-theme');
  if (!btn) return;
  btn.innerHTML = `<i data-lucide="${ui.theme === 'dark' ? 'moon' : 'sun'}"></i>`;
  renderIcons();
}
```

- [ ] **Step 3: Render icons during init**

Call `renderIcons()` at the end of `init()` after refresh and log loading:

```js
renderIcons();
```

### Task 4: Verification

**Files:**
- Check: `charge-boost/webroot/index.html`
- Check: `charge-boost/webroot/style.css`
- Check: `charge-boost/webroot/app.js`

- [ ] **Step 1: Search for emoji icon remnants**

Run:

```powershell
Select-String -Path 'charge-boost\webroot\index.html','charge-boost\webroot\app.js' -Pattern '⚡|🌙|☀️|🌡|📊|📈|⚙️|🎛️|🎨|📝|✅|❌'
```

Expected: Only toast success/failure symbols may remain in `app.js`; no UI labels or navigation should use emoji.

- [ ] **Step 2: Verify module config keys did not change**

Run:

```powershell
Select-String -Path 'charge-boost\webroot\app.js' -Pattern 'CURRENT_MAX|TEMP_MAX|RECHARGE_TEMP|CHARGE_STOP|CHARGE_START|TEMP_SIMULATE|BYPASS_CHARGE'
```

Expected: All existing keys are present.

- [ ] **Step 3: Verify files are syntactically readable**

Run:

```powershell
Get-Content -LiteralPath 'charge-boost\webroot\index.html' -Encoding UTF8 | Out-Null
Get-Content -LiteralPath 'charge-boost\webroot\style.css' -Encoding UTF8 | Out-Null
Get-Content -LiteralPath 'charge-boost\webroot\app.js' -Encoding UTF8 | Out-Null
```

Expected: No PowerShell errors.
