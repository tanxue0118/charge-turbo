# UI.md

UI编写准则，旨在美化输出的UI。可根据具体项目需求进行合并调整。

## 1. 字体规则

禁止 Inter / Arial / Roboto 等默认字体
必须 Goole Fonts 选展示字体 + 正文字体

## 2. 颜色规则

禁止 纯黑 #000000，必须带色温倾向
禁止 OKLCH 色彩空间，主色大胆 + 强调色尖锐
禁止 紫粉渐变 （ #8b5cf6 → #ec4899 ）

## 3. 布局规则

禁止 单调布局，单纯卡片对称布局

## 4. 整体风格

**制作UI前先询问要做哪种风格**

1. iOS液态玻璃风格

/* ═══════════════════════════════════════════════════════════════
   iOS 风格玻璃效果 / 模糊效果 / 动画效果 合集
   ═══════════════════════════════════════════════════════════════ */

/* ── CSS 变量 ── */
:root {
  --bg: #ffffff;
  --tx: #141416;
  --tx2: #282830;
  --tx3: #595966;
  --ac: #141416;
  --ok: #16a34a;
  --wa: #d97706;
  --er: #dc2626;
  --blur: 18px;
  --r-sm: 6px;
  --r-md: 14px;
  --r-lg: 18px;
  --fs-xs: clamp(9px, 2.56vw, 11px);
  --fs-sm: clamp(11px, 3.08vw, 13px);
  --fs-md: clamp(13px, 3.59vw, 15px);
  --fs-lg: clamp(16px, 4.62vw, 20px);
  --ton: rgba(30, 30, 36, .28);
  --tonb: rgba(30, 30, 36, .45);
  --sl-fill: rgba(0, 0, 0, .38);
}

* { box-sizing: border-box; margin: 0; padding: 0; -webkit-tap-highlight-color: transparent; }


/* ═══════════════════════════════════════════════════
   1. 毛玻璃卡片 (Frosted Glass Card)
   ═══════════════════════════════════════════════════ */

.card {
  position: relative;
  background: rgba(255, 255, 255, .08);
  backdrop-filter: blur(var(--blur)) saturate(180%) brightness(1.06);
  -webkit-backdrop-filter: blur(var(--blur)) saturate(180%) brightness(1.06);
  border-radius: var(--r-md);
  padding: clamp(10px, 3.33vw, 15px) clamp(11px, 3.59vw, 16px);
  margin-bottom: 10px;
  overflow: clip;
  opacity: 0;
  transform: translateY(10px) scale(.988) translateZ(0);
  transition: box-shadow .08s linear;
  isolation: isolate;
  will-change: transform, opacity;
}

/* 高光渐变上半部分 */
.card::before {
  content: '';
  position: absolute; inset: 0;
  border-radius: inherit;
  background: linear-gradient(
    135deg,
    rgba(255, 255, 255, .28) 0%,
    rgba(255, 255, 255, .10) 35%,
    transparent 65%
  );
  pointer-events: none;
  z-index: 0;
}

/* 顶部光晕 */
.card::after {
  content: '';
  position: absolute;
  top: 0; left: 0; right: 0;
  height: 50%;
  border-radius: var(--r-md) var(--r-md) 0 0;
  background: linear-gradient(180deg, rgba(255, 255, 255, .12) 0%, transparent 100%);
  pointer-events: none;
  z-index: 0;
}

.card > * { position: relative; z-index: 2 }
.card:active { transform: scale(.995) }


/* ═══════════════════════════════════════════════════
   2. 毛玻璃按钮 (Frosted Glass Button)
   ═══════════════════════════════════════════════════ */

.btn {
  display: inline-flex;
  align-items: center; justify-content: center;
  padding: clamp(10px, 3.08vw, 14px) clamp(13px, 4.1vw, 18px);
  border-radius: var(--r-md);
  border: none;
  font-size: var(--fs-md); font-weight: 600;
  cursor: pointer; outline: none;
  -webkit-user-select: none; user-select: none;
  position: relative; overflow: hidden; flex: 1;
  transform: translateZ(0);
  background: rgba(255, 255, 255, .18);
  backdrop-filter: blur(var(--blur)) saturate(180%);
  -webkit-backdrop-filter: blur(var(--blur)) saturate(180%);
  color: var(--tx2);
  box-shadow: none;
  transition: transform .18s cubic-bezier(.34, 1.56, .64, 1);
}

/* 按钮顶部高光 */
.btn::before {
  content: ''; position: absolute;
  top: 0; left: 0; right: 0; height: 52%;
  border-radius: var(--r-md) var(--r-md) 0 0;
  background: linear-gradient(to bottom, rgba(255, 255, 255, .38), transparent);
  pointer-events: none;
}

/* 按钮按压暗层 */
.btn::after {
  content: ''; position: absolute; inset: 0;
  background: rgba(0, 0, 0, .06);
  opacity: 0; border-radius: inherit;
  pointer-events: none; transition: opacity .1s;
}

.btn:active { transform: scale(.93) !important }
.btn:active::after { opacity: 1 }

.btn.pop { animation: pop .26s cubic-bezier(.34, 1.7, .64, 1) }

/* 主要按钮变体 */
.btn-pri {
  background: rgba(20, 20, 22, .10);
  color: var(--tx);
}

/* 危险按钮变体 */
.btn-stop {
  background: rgba(220, 38, 38, .12);
  color: var(--er);
}


/* ═══════════════════════════════════════════════════
   3. 毛玻璃底部导航栏 (Frosted Tab Bar)
   ═══════════════════════════════════════════════════ */

.tabbar-wrap {
  position: fixed; bottom: 0; left: 0; right: 0; z-index: 200;
  display: flex; justify-content: center; align-items: flex-end;
  padding-bottom: calc(14px + env(safe-area-inset-bottom));
  pointer-events: none;
}

.tabbar {
  opacity: 0;
  pointer-events: auto;
  display: flex; align-items: stretch;
  position: relative;
  background:
    rgba(210, 215, 220, .55),
    rgba(255, 255, 255, .18);
  backdrop-filter: blur(calc(var(--blur) + 2px)) saturate(160%) brightness(1.05);
  -webkit-backdrop-filter: blur(calc(var(--blur) + 2px)) saturate(160%) brightness(1.05);
  border: 1px solid rgba(255, 255, 255, .38);
  border-radius: 999px;
  padding: 5px;
  width: min(92vw, 420px);
  transform: translateZ(0);
  will-change: transform;
}

/* 滑动指示器 */
.tab-indicator {
  position: absolute;
  top: 5px; bottom: 5px; left: 5px;
  width: 25%;
  border-radius: 999px;
  background: rgba(210, 215, 220, .40);
  backdrop-filter: blur(calc(var(--blur) + 4px)) saturate(160%) brightness(1.05);
  -webkit-backdrop-filter: blur(calc(var(--blur) + 4px)) saturate(160%) brightness(1.05);
  border: 1px solid rgba(0, 0, 0, .07);
  box-shadow: 0 2px 8px rgba(0, 0, 0, .10);
  transition: left .45s cubic-bezier(.34, 1.28, .64,1),
              width .45s cubic-bezier(.34, 1.28, .64,1);
  will-change: left, width;
  pointer-events: none; z-index: 0;
  transform: translateZ(0);
}

.tab {
  flex: 1;
  display: flex; flex-direction: column;
  align-items: center; justify-content: center;
  gap: 3px; padding: 9px 6px;
  cursor: pointer;
  -webkit-user-select: none; user-select: none;
  position: relative; z-index: 1;
  border-radius: 999px;
  transform: translateZ(0);
}

.tab.active { z-index: 2 }

.tab-icon {
  font-size: var(--fs-lg); line-height: 1;
  transition: transform .35s cubic-bezier(.34, 1.56, .64,1), opacity .2s;
  opacity: .45;
}
.tab.active .tab-icon { transform: scale(1.15); opacity: 1 }

.tab-label {
  font-size: var(--fs-xs); font-weight: 700; letter-spacing: .1px;
  color: rgba(40, 48, 65, .55);
  transition: color .22s, transform .3s cubic-bezier(.34, 1.56, .64,1);
  white-space: nowrap;
}
.tab.active .tab-label { color: rgba(10, 14, 28, .85); transform: scale(1.06) }


/* ═══════════════════════════════════════════════════
   4. 毛玻璃弹窗 (Frosted Modal / Overlay)
   ═══════════════════════════════════════════════════ */

.modal-overlay {
  position: fixed; inset: 0; z-index: 9998;
  display: flex; align-items: center; justify-content: center;
  padding: clamp(14px, 4vw, 24px);
  background: rgba(0, 0, 0, .58);
  backdrop-filter: blur(12px);
  -webkit-backdrop-filter: blur(12px);
  opacity: 0; pointer-events: none;
  transition: opacity .28s ease;
}

.modal-overlay.show { opacity: 1; pointer-events: auto }

.modal-card {
  width: 100%; max-width: 480px; max-height: 82vh;
  display: flex; flex-direction: column;
  border-radius: var(--r-md);
  background: rgba(14, 14, 18, .58);
  backdrop-filter: blur(42px) saturate(200%) brightness(.88);
  -webkit-backdrop-filter: blur(42px) saturate(200%) brightness(.88);
  box-shadow: 0 0 0 1px rgba(255, 255, 255, .16),
              inset 0 1px 0 rgba(255, 255, 255, .22),
              0 24px 64px rgba(0, 0, 0, .48);
  overflow: hidden;
  transform: translateY(14px) scale(.96);
  transition: transform .38s cubic-bezier(.34, 1.56, .64,1);
}

.modal-overlay.show .modal-card { transform: none }


/* ═══════════════════════════════════════════════════
   5. iOS 风格开关 (iOS Toggle Switch)
   ═══════════════════════════════════════════════════ */

/* 小尺寸开关 */
.tog {
  position: relative; display: inline-block;
  width: 38px; height: 22px;
  cursor: pointer; flex-shrink: 0;
}

.tog input { opacity: 0; width: 0; height: 0; position: absolute }

.tog-track {
  position: absolute; inset: 0;
  background: rgba(120, 140, 160, .25);
  border-radius: 999px;
  border: 1px solid rgba(255, 255, 255, .35);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .3),
              inset 0 -1px 0 rgba(0, 0, 0, .05);
  transition: background .24s, border-color .24s, box-shadow .24s;
}

.tog-track::after {
  content: ''; position: absolute;
  width: 16px; height: 16px; left: 2px; top: 2px;
  background: linear-gradient(145deg, #fff, rgba(240, 245, 255, .9));
  border-radius: 50%;
  box-shadow: 0 1px 4px rgba(0, 0, 0, .18),
              0 0 0 1px rgba(255, 255, 255, .6) inset;
  transition: transform .26s cubic-bezier(.34, 1.56, .64,1);
}

.tog input:checked + .tog-track {
  background: var(--ton);
  border-color: var(--tonb);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .35),
              0 0 0 3px color-mix(in srgb, var(--ac) 16%, transparent);
}

.tog input:checked + .tog-track::after { transform: translateX(16px) }

/* 大尺寸开关 */
.tog-lg {
  position: relative; display: inline-block;
  width: 52px; height: 30px;
  cursor: pointer; flex-shrink: 0;
}

.tog-lg input { opacity: 0; width: 0; height: 0; position: absolute }

.tog-lg-track {
  position: absolute; inset: 0;
  background: rgba(120, 140, 160, .25);
  border-radius: 999px;
  border: 1px solid rgba(255, 255, 255, .35);
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .3);
  transition: background .26s, border-color .26s, box-shadow .26s;
}

.tog-lg-track::after {
  content: ''; position: absolute;
  width: 22px; height: 22px; left: 3px; top: 3px;
  background: linear-gradient(145deg, #fff, rgba(235, 242, 255, .95));
  border-radius: 50%;
  box-shadow: 0 2px 8px rgba(0, 0, 0, .22),
              0 0 0 1px rgba(255, 255, 255, .6) inset;
  transition: transform .28s cubic-bezier(.34, 1.56, .64,1);
}

.tog-lg input:checked + .tog-lg-track {
  background: var(--ton);
  border-color: var(--tonb);
  box-shadow: 0 0 0 3.5px color-mix(in srgb, var(--ac) 16%, transparent),
              inset 0 1px 0 rgba(255, 255, 255, .3);
}

.tog-lg input:checked + .tog-lg-track::after { transform: translateX(22px) }


/* ═══════════════════════════════════════════════════
   6. iOS 风格滑块 (iOS Slider)
   ═══════════════════════════════════════════════════ */

.ios-slider {
  -webkit-appearance: none; appearance: none;
  display: block; width: 100%; height: 44px;
  background: transparent; outline: none;
  cursor: ew-resize; margin: 0; padding: 0;
}

.ios-slider::-webkit-slider-runnable-track {
  height: 5px; border-radius: 3px; box-shadow: none;
}

.ios-slider::-webkit-slider-runnable-track {
  background: linear-gradient(to right, var(--sl-fill) var(--p, 0%), rgba(255, 255, 255, .22) var(--p, 0%));
}

.ios-slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 26px; height: 26px; border-radius: 50%;
  background: linear-gradient(145deg, #fff, rgba(230, 240, 255, .9));
  box-shadow: none;
  cursor: ew-resize;
  margin-top: -10.5px;
  transition: transform .38s cubic-bezier(.34, 1.56, .64,1);
}

.ios-slider:active::-webkit-slider-thumb {
  transform: scale(1.34);
  transition: transform .1s cubic-bezier(.25, .46, .45, .94);
}

.ios-slider:focus:not(:active)::-webkit-slider-thumb { transform: scale(1.18) }


/* ═══════════════════════════════════════════════════
   7. Toast 提示 (Glass Toast)
   ═══════════════════════════════════════════════════ */

.glass-toast {
  position: fixed; top: 50%; left: 50%;
  transform: translate(-50%, -50%) scale(.92);
  background: rgba(255, 255, 255, .13);
  backdrop-filter: blur(28px) saturate(200%) brightness(1.08);
  -webkit-backdrop-filter: blur(28px) saturate(200%) brightness(1.08);
  color: var(--tx);
  font-size: var(--fs-md); font-weight: 700;
  padding: 16px 28px;
  border-radius: var(--r-md);
  white-space: nowrap; z-index: 9999;
  pointer-events: none;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, .22),
              inset 0 1px 0 rgba(255, 255, 255, .55),
              0 8px 32px rgba(0, 0, 0, .12);
  transition: opacity .22s ease, transform .22s ease;
  opacity: 0;
}

.glass-toast.show {
  opacity: 1;
  transform: translate(-50%, -50%) scale(1);
}


/* ═══════════════════════════════════════════════════
   8. 状态指示器 (Status Dot)
   ═══════════════════════════════════════════════════ */

.status-dot {
  width: 8px; height: 8px; border-radius: 50%;
  background: var(--tx3);
  transition: background .3s, box-shadow .3s;
  flex-shrink: 0;
}

.status-dot.ok {
  background: var(--ok);
  box-shadow: 0 0 8px color-mix(in srgb, var(--ok) 60%, transparent);
}

.status-dot.error { background: var(--er) }

.status-dot.pending {
  background: var(--wa);
  animation: blink 1.2s ease-in-out infinite;
}


/* ═══════════════════════════════════════════════════
   9. 徽章 (Badge)
   ═══════════════════════════════════════════════════ */

.badge {
  font-size: var(--fs-xs); font-weight: 700;
  padding: 2px 8px; border-radius: var(--r-lg);
  letter-spacing: .3px; text-transform: uppercase;
  transition: all .22s;
}

.badge-on {
  background: color-mix(in srgb, var(--ok) 12%, transparent);
  color: var(--ok);
}

.badge-off {
  background: rgba(255, 255, 255, .10);
  color: var(--tx3);
}


/* ═══════════════════════════════════════════════════
   10. 关键帧动画 (Keyframe Animations)
   ═══════════════════════════════════════════════════ */

/* 闪烁 */
@keyframes blink {
  0%, 100% { opacity: .35 }
  50%      { opacity: 1 }
}

/* 弹跳 */
@keyframes pop {
  0%  { transform: scale(1) }
  20% { transform: scale(.87) }
  55% { transform: scale(1.07) }
  80% { transform: scale(.98) }
  100%{ transform: scale(1) }
}

/* 底栏入场 */
@keyframes tabbar-in {
  0%  { opacity: 0; transform: translateY(24px) scale(.92) }
  65% { opacity: 1; transform: translateY(-4px) scale(1.01) }
  82% { transform: translateY(2px) scale(.995) }
  100%{ opacity: 1; transform: translateY(0) scale(1) }
}

/* 卡片缩放入场 */
@keyframes card-zoom-in {
  0%  { opacity: 0; transform: scale(.76) translateZ(0) }
  100%{ opacity: 1; transform: scale(1) translateZ(0) }
}

/* 面板退出 */
@keyframes panel-out {
  0%  { opacity: 1; transform: translateY(0) scale(1) }
  100%{ opacity: 0; transform: translateY(-8px) scale(.97) }
}

/* 面板入场（带弹性） */
@keyframes panel-in {
  0%  { opacity: 0; transform: translateY(14px) scale(.95) }
  55% { opacity: 1 }
  72% { transform: translateY(-4px) scale(1.015) }
  86% { transform: translateY(2px) scale(.997) }
  100%{ opacity: 1; transform: translateY(0) scale(1) }
}

/* 弹窗遮罩入场 */
@keyframes overlay-in {
  from { opacity: 0 }
  to   { opacity: 1 }
}

/* 弹窗遮罩退出 */
@keyframes overlay-out {
  from { opacity: 1 }
  to   { opacity: 0 }
}

/* 弹窗卡片入场（弹性缩放 + 上移） */
@keyframes modal-card-in {
  0%  { opacity: 0; transform: scale(.82) translateY(24px) }
  65% { opacity: 1; transform: scale(1.03) translateY(-4px) }
  82% { transform: scale(.985) translateY(2px) }
  100%{ opacity: 1; transform: scale(1) translateY(0) }
}

/* 弹窗卡片退出 */
@keyframes modal-card-out {
  from { opacity: 1; transform: scale(1) translateY(0) }
  to   { opacity: 0; transform: scale(.88) translateY(18px) }
}


/* ═══════════════════════════════════════════════════
   11. 动画工具类 (Animation Utility Classes)
   ═══════════════════════════════════════════════════ */

.card.in,
.card-anim.in {
  animation: card-zoom-in .38s cubic-bezier(.34, 1.32, .64, 1) var(--del, 0ms) both;
  will-change: transform, opacity;
}

.tabbar.in {
  animation: tabbar-in .45s cubic-bezier(.34, 1.32, .64, 1) .18s both;
  pointer-events: auto;
}

.panel-out { animation: panel-out .13s ease-in both; pointer-events: none }
.panel-in  { animation: panel-in .42s cubic-bezier(.34, 1.56, .64, 1) both }

.modal-overlay.in {
  display: flex !important;
  animation: overlay-in .22s ease both;
}

.modal-overlay.in .modal-card {
  animation: modal-card-in .38s cubic-bezier(.34, 1.42, .64, 1) both;
}

.modal-overlay.out {
  animation: overlay-out .2s ease both;
  pointer-events: none;
}

.modal-overlay.out .modal-card {
  animation: modal-card-out .18s ease both;
}

.btn.pop { animation: pop .26s cubic-bezier(.34, 1.7, .64, 1) }


/* ═══════════════════════════════════════════════════
   12. 可折叠面板动画 (Collapsible Panel)
   ═══════════════════════════════════════════════════ */

.collapsible-body {
  overflow: hidden;
  max-height: 2000px;
  opacity: 1;
  transition: max-height .4s cubic-bezier(.4, 0, .2, 1),
              opacity .3s ease;
}

.collapsible-body.collapsed {
  max-height: 0;
  opacity: 0;
}


/* ═══════════════════════════════════════════════════
   13. 温度指示点 (Temperature Dot)
   ═══════════════════════════════════════════════════ */

.temp-dot {
  width: 7px; height: 7px; border-radius: 50%;
  background: var(--tx3); opacity: .35; flex-shrink: 0;
  transition: background .4s, opacity .4s;
}

.temp-dot.cool { background: #22c55e; opacity: .8 }
.temp-dot.warm { background: #f59e0b; opacity: .9 }
.temp-dot.hot  { background: #ef4444; opacity: 1 }


/* ═══════════════════════════════════════════════════
   14. 选择按钮组 (Segmented Control)
   ═══════════════════════════════════════════════════ */

.seg-btns {
  display: grid; grid-template-columns: repeat(6, 1fr);
  gap: 5px;
}

.seg-btn {
  border: none; outline: none; cursor: pointer;
  padding: 8px 0; border-radius: var(--r-sm);
  font-size: var(--fs-xs); font-weight: 700; letter-spacing: .2px;
  background: rgba(255, 255, 255, .18); color: var(--tx3);
  box-shadow: none;
  transition: transform .15s cubic-bezier(.34, 1.56, .64,1),
              background .15s, color .15s;
  user-select: none; will-change: transform;
}

.seg-btn:active { transform: scale(.88) !important }

.seg-btn.sel {
  background: rgba(20, 20, 22, .12);
  color: var(--tx);
}


/* ═══════════════════════════════════════════════════
   15. 信息面板 (Info Card)
   ═══════════════════════════════════════════════════ */

.info-card { cursor: pointer; user-select: none; transform: translateZ(0) }

.info-card-hd {
  display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 11px;
}

.info-card-hd-title {
  font-size: var(--fs-xs); font-weight: 700;
  letter-spacing: .6px; text-transform: uppercase;
  color: var(--ac);
  display: flex; align-items: center; gap: 4px;
}

.info-card-hd-title::before { content: '◈'; font-size: var(--fs-xs); opacity: .4 }

.info-switch-pill {
  display: inline-flex; align-items: center; gap: 4px;
  padding: 3px 9px 3px 7px; border-radius: 999px;
  background: rgba(255, 255, 255, .18); border: none;
  font-size: var(--fs-xs); font-weight: 700;
  color: var(--tx3); letter-spacing: .2px;
  pointer-events: none; transition: opacity .15s;
}

.info-switch-pill-icon {
  font-size: 10px; opacity: .65;
  transition: transform .32s cubic-bezier(.34, 1.56, .64,1);
}


/* ═══════════════════════════════════════════════════
   16. 标签指示点 (Tab Dot Notification)
   ═══════════════════════════════════════════════════ */

.tab-dot {
  position: absolute; top: 5px; right: calc(50% - 13px);
  width: 5px; height: 5px; border-radius: 50%;
  background: var(--ok);
  box-shadow: 0 0 6px color-mix(in srgb, var(--ok) 80%, transparent);
  display: none; z-index: 2;
}

.tab-dot.visible { display: block }


/* ═══════════════════════════════════════════════════
   17. 液态玻璃效果 (Liquid Glass Effect)
       需配合下方 SVG Filter 使用
   ═══════════════════════════════════════════════════ */

body.aat-liquid-glass {
  --lg-clear: rgba(255, 255, 255, .018);
  --lg-edge:  rgba(255, 255, 255, .30);
  --lg-hot:   rgba(255, 255, 255, .86);
  --lg-cyan:  rgba(125, 249, 255, .105);
  --lg-pink:  rgba(255, 124, 207, .080);
  --lg-shadow:rgba(0, 0, 0, .30);
}

/* 液态玻璃卡片 */
body.aat-liquid-glass .card,
body.aat-liquid-glass .btn,
body.aat-liquid-glass .tabbar {
  background:
    radial-gradient(circle at 18% 0%, rgba(255, 255, 255, .115), transparent 34%),
    radial-gradient(circle at 92% 104%, var(--lg-cyan), transparent 38%),
    linear-gradient(135deg, rgba(255, 255, 255, .040), rgba(255, 255, 255, .010)) !important;
  border: none !important;
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .46),
    inset 0 -1px 0 rgba(255, 255, 255, .10) !important;
  backdrop-filter: blur(var(--blur)) brightness(1.05) contrast(1.08) saturate(1.08) !important;
  -webkit-backdrop-filter: blur(var(--blur)) brightness(1.05) contrast(1.08) saturate(1.08) !important;
  transform: translateZ(0);
  backface-visibility: hidden;
}

/* 液态玻璃边缘高光 */
body.aat-liquid-glass .card::before {
  content: '' !important;
  position: absolute !important; inset: 0 !important;
  border-radius: inherit !important;
  pointer-events: none !important; z-index: 1 !important; opacity: .55 !important;
  background:
    linear-gradient(90deg, rgba(255, 255, 255, .28), transparent 16%, transparent 84%, rgba(255, 255, 255, .16)),
    linear-gradient(180deg, rgba(255, 255, 255, .22), transparent 18%, transparent 82%, rgba(255, 255, 255, .10)) !important;
  padding: 1px !important;
  -webkit-mask: linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0) !important;
  -webkit-mask-composite: xor !important;
  mask-composite: exclude !important;
}

/* 液态玻璃按钮（轻量版） */
body.aat-liquid-glass .btn {
  background:
    radial-gradient(circle at 22% 12%, rgba(255, 255, 255, .32), transparent 44%),
    radial-gradient(circle at 80% 88%, var(--lg-cyan), transparent 40%),
    linear-gradient(135deg, rgba(255, 255, 255, .14), rgba(255, 255, 255, .04)) !important;
  box-shadow:
    inset 0 1px 0 rgba(255, 255, 255, .52),
    inset 0 -1px 0 rgba(255, 255, 255, .08) !important;
}

/* 液态玻璃底栏 */
body.aat-liquid-glass .tabbar {
  background:
    radial-gradient(circle at 50% 0%, rgba(255, 255, 255, .16), transparent 40%),
    linear-gradient(135deg, rgba(255, 255, 255, .045), rgba(255, 255, 255, .012)) !important;
  border: 1px solid rgba(255, 255, 255, .30) !important;
  box-shadow: none !important;
}

/* 液态玻璃指示器 */
body.aat-liquid-glass .tab-indicator {
  background:
    radial-gradient(circle at 18% 0%, rgba(255, 255, 255, .115), transparent 34%),
    radial-gradient(circle at 92% 104%, var(--lg-cyan), transparent 38%),
    linear-gradient(135deg, rgba(255, 255, 255, .040), rgba(255, 255, 255, .010)) !important;
  backdrop-filter: blur(calc(var(--blur) + 4px)) brightness(1.06) contrast(1.14) saturate(1.18) !important;
  -webkit-backdrop-filter: blur(calc(var(--blur) + 4px)) brightness(1.06) contrast(1.14) saturate(1.18) !important;
  box-shadow: 0 2px 8px rgba(0, 0, 0, .10) !important;
  border: 1px solid rgba(255, 255, 255, .30) !important;
}

/* 液态玻璃激活态按钮 */
body.aat-liquid-glass .liquid-mode-btn.active {
  color: var(--ac);
  border-color: rgba(255, 255, 255, .50);
  background:
    radial-gradient(circle at 28% 18%, rgba(255, 255, 255, .42), transparent 34%),
    linear-gradient(135deg, rgba(255, 255, 255, .28), rgba(210, 245, 255, .10), rgba(255, 224, 246, .07)) !important;
  box-shadow:
    0 12px 24px rgba(0, 0, 0, .20),
    inset 0 1px 0 rgba(255, 255, 255, .60),
    inset 0 -1px 0 rgba(255, 255, 255, .12) !important;
}


/* ═══════════════════════════════════════════════════
   18. 液态玻璃 SVG Filter（需插入 HTML body）
   ═══════════════════════════════════════════════════
   将以下 SVG 插入到 <body> 开头：

   <svg id="aat-liquid-glass-svg" width="0" height="0"
        aria-hidden="true" style="position:fixed;left:-9999px;top:-9999px">
     <defs>
       <filter id="aatLiquidGlassFilter" x="-34%" y="-34%" width="168%" height="168%"
               color-interpolation-filters="sRGB">
         <!-- R通道 X 渐变 -->
         <feImage href="data:image/svg+xml;charset=utf-8,..." result="xRamp" .../>
         <feColorMatrix in="xRamp" type="matrix"
           values="1 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0" result="xR"/>
         <!-- G通道 Y 渐变 -->
         <feImage href="data:image/svg+xml;charset=utf-8,..." result="yRamp" .../>
         <feColorMatrix in="yRamp" type="matrix"
           values="0 0 0 0 0 0 1 0 0 0 0 0 0 0 0 0 0 0 1 0" result="yG"/>
         <!-- 合并 + 位移映射 -->
         <feComposite in="xR" in2="yG" operator="arithmetic"
           k1="0" k2="1" k3="1" k4="0" result="xyMap"/>
         <feDisplacementMap in="SourceGraphic" in2="xyMap"
           scale="28" xChannelSelector="R" yChannelSelector="G" result="warped"/>
         <feGaussianBlur in="warped" stdDeviation="0.038" result="softWarped"/>
         <feColorMatrix in="softWarped" type="saturate" values="1.06"/>
       </filter>
       <filter id="aatLiquidGlassFilterLite" ...>
         <!-- 轻量版，scale=11 -->
       </filter>
     </defs>
   </svg>

   在 backdrop-filter 中引用：
   backdrop-filter: url(#aatLiquidGlassFilter) blur(18px) brightness(1.05);
   ═══════════════════════════════════════════════════ */


/* ═══════════════════════════════════════════════════
   19. 渐变标签 (Gradient Tags)
   ═══════════════════════════════════════════════════ */

.tag {
  font-family: "SF Mono", Menlo, monospace;
  font-size: var(--fs-xs); font-weight: 700;
  padding: 3px 5px; border-radius: 6px;
  text-align: center; letter-spacing: .2px;
  background: rgba(20, 20, 22, .08);
  color: rgba(30, 30, 36, .70);
}


/* ═══════════════════════════════════════════════════
   20. 统计卡片 (Stats Card)
   ═══════════════════════════════════════════════════ */

.stats-mini {
  flex: 1; border-radius: var(--r-md);
  padding: clamp(10px, 2.56vw, 14px) clamp(11px, 2.82vw, 15px);
  display: flex; flex-direction: column;
  justify-content: space-between; min-height: 0;
  background: rgba(255, 255, 255, .08);
  backdrop-filter: blur(var(--blur)) saturate(180%) brightness(1.06);
  -webkit-backdrop-filter: blur(var(--blur)) saturate(180%) brightness(1.06);
}

.stats-mini-label {
  font-size: var(--fs-sm); color: var(--tx3); font-weight: 400;
  letter-spacing: .02px; margin-bottom: 4px;
}

.stats-mini-val {
  font-size: clamp(18px, 5.13vw, 26px); font-weight: 400;
  color: #141416; letter-spacing: -.2px;
  line-height: 1.15; word-break: break-all;
}


/* ═══════════════════════════════════════════════════
   21. 毛玻璃 Banner
   ═══════════════════════════════════════════════════ */

.glass-banner {
  display: flex; align-items: center; gap: 12px;
  background: rgba(20, 20, 22, .07);
  border: 1px solid rgba(20, 20, 22, .14);
  border-radius: var(--r-md);
  padding: 13px 15px; margin-bottom: 13px;
  box-shadow: inset 0 1.5px 0 rgba(255, 255, 255, .55),
              0 3px 14px rgba(0, 0, 0, .06);
  position: relative; overflow: hidden;
}

.glass-banner::before {
  content: ''; position: absolute; inset: 0;
  border-radius: inherit;
  background: linear-gradient(155deg, rgba(255, 255, 255, .30) 0%, transparent 55%);
  pointer-events: none;
}

.glass-banner-icon {
  width: 38px; height: 38px; border-radius: var(--r-sm); flex-shrink: 0;
  background: rgba(20, 20, 22, .18); color: var(--tx);
  display: flex; align-items: center; justify-content: center;
  box-shadow: inset 0 1px 0 rgba(255, 255, 255, .4);
  transition: background .25s, box-shadow .25s, opacity .25s;
}

.glass-banner-text { flex: 1; min-width: 0 }
.glass-banner-title { font-size: var(--fs-md); font-weight: 700; color: var(--tx) }
.glass-banner-sub { font-size: var(--fs-xs); color: var(--tx3); margin-top: 2px }


<!-- 粘性导航栏，支持毛玻璃效果 -->
<nav class="sticky top-0 z-50 backdrop-blur-xl">
    <!-- 导航内容 -->
</nav>

2. 莫奈极简风格
<!-- ═══ 统计卡片 ═══ -->
<div class="demo-section">
  <div class="demo-title">◈ 统计卡片</div>
  <div class="st-hero">
    <div class="card in" style="--del:400ms;border-radius:var(--r-lg);padding:18px;aspect-ratio:1/1;display:flex;flex-direction:column;justify-content:space-between">
      <div style="font-size:22px;font-weight:700;color:var(--tx)">运行中</div>
      <div style="font-size:12px;color:var(--tx3);font-family:'SF Mono',monospace">PID · 12345</div>
    </div>
    <div class="flex-col">
      <div class="card stats-mini in" style="--del:460ms">
        <div class="stats-mini-label">平台</div>
        <div class="stats-mini-val" style="font-size:18px">Snapdragon</div>
      </div>
      <div class="card stats-mini in" style="--del:520ms">
        <div class="stats-mini-label">设备</div>
        <div class="stats-mini-val" style="font-size:18px">Pixel 9</div>
      </div>
    </div>
  </div>
</div>



3. 用户自行提出风格

你需要先自行做出框架，再去逐步根据用户给的命令去优化
可以去参考[大厂UI](https://github.com/VoltAgent/awesome-design-md)的风格