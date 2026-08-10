# WebUI Dashboard Design

## Goal

Optimize `charge-boost/webroot` into a polished liquid-glass charging dashboard while preserving the existing KernelSU configuration and monitoring behavior.

## Scope

- Keep the current four-page structure: status, chart, config, log.
- Replace emoji-style icons with Lucide icon markup loaded from the web when available.
- Replace default system-first typography with Google Fonts first, while keeping local fallbacks so the WebView remains usable offline.
- Tighten the visual hierarchy on the status page so battery percentage, charging state, temperature, power, and current are easier to scan.
- Normalize repeated inline styles into CSS classes.
- Preserve existing config file paths, shell commands, refresh intervals, and option names.

## Visual Direction

The approved direction is a hybrid of iOS liquid glass and an energy dashboard. Panels remain translucent, but spacing, typography, icons, and state colors become more disciplined. The UI should feel like a control surface for charging rather than a decorative landing page.

## Architecture

`index.html` owns page structure and static labels. `style.css` owns theme tokens, layout, icon sizing, panels, forms, chart containers, and responsive behavior. `app.js` continues to own KernelSU execution, config parsing, status refresh, chart drawing, and UI updates.

## Data Flow

The WebUI keeps reading battery nodes from `/sys/class/power_supply/battery/*`, reads and writes `/data/adb/modules/turbo-charge/option.txt`, reads logs from `/data/adb/modules/turbo-charge/log.txt`, and stores only UI preferences in `localStorage`.

## Error Handling

Existing fallback behavior remains: if KernelSU is unavailable, commands return empty data and the UI stays usable. External font and icon resources are enhancements only; the page must still render readable text and controls if they do not load.

## Testing

Testing is static and browser-oriented because this is a module WebUI, not a packaged frontend project. Verify that the edited files parse, no emoji icon text remains in controls/headings, no existing config key names changed, and the HTML can be opened locally without syntax errors.

## Non-Goals

- Do not rewrite the charging logic.
- Do not change module paths or option names.
- Do not introduce a build step.
- Do not add framework dependencies.
