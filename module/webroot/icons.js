(function () {
  const icons = {
    activity: '<polyline points="22 12 18 12 15 21 9 3 6 12 2 12"/>',
    'arrow-down': '<path d="M12 5v14"/><path d="m19 12-7 7-7-7"/>',
    'arrow-up': '<path d="m5 12 7-7 7 7"/><path d="M12 19V5"/>',
    'battery-charging': '<path d="M15 7h1a2 2 0 0 1 2 2v1"/><path d="M18 14v1a2 2 0 0 1-2 2h-2"/><path d="M22 11v2"/><path d="M8 7H4a2 2 0 0 0-2 2v6a2 2 0 0 0 2 2h5"/><path d="m11 7-3 5h4l-3 5"/>',
    eraser: '<path d="m7 21-4.3-4.3a2.4 2.4 0 0 1 0-3.4L13.3 2.7a2.4 2.4 0 0 1 3.4 0l4.6 4.6a2.4 2.4 0 0 1 0 3.4L11 21"/><path d="M22 21H7"/><path d="m5 11 9 9"/>',
    'file-text': '<path d="M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z"/><path d="M14 2v6h6"/><path d="M10 9H8"/><path d="M16 13H8"/><path d="M16 17H8"/>',
    gauge: '<path d="m12 14 4-4"/><path d="M3.34 19a10 10 0 1 1 17.32 0"/>',
    moon: '<path d="M12 3a6 6 0 0 0 9 7 9 9 0 1 1-9-7Z"/>',
    palette: '<circle cx="13.5" cy="6.5" r=".5"/><circle cx="17.5" cy="10.5" r=".5"/><circle cx="8.5" cy="7.5" r=".5"/><circle cx="6.5" cy="12.5" r=".5"/><path d="M12 22a10 10 0 1 1 10-10 3.5 3.5 0 0 1-3.5 3.5H17a2 2 0 0 0-1.4 3.4l.2.2A2 2 0 0 1 14.4 22Z"/>',
    'rotate-ccw': '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"/><path d="M3 3v5h5"/>',
    'rotate-cw': '<path d="M21 12a9 9 0 1 1-3-6.7L21 8"/><path d="M21 3v5h-5"/>',
    save: '<path d="M15.2 3a2 2 0 0 1 1.4.6l.8.8a2 2 0 0 1 .6 1.4V21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2Z"/><path d="M7 3v5h9"/><path d="M7 21v-7h10v7"/>',
    settings: '<path d="M12.2 2h-.4a2 2 0 0 0-2 1.7l-.1.8a2 2 0 0 1-1.1 1.5l-.8.4a2 2 0 0 0-.8 2.7l.4.7a2 2 0 0 1 0 1.8l-.4.7a2 2 0 0 0 .8 2.7l.8.4a2 2 0 0 1 1.1 1.5l.1.8a2 2 0 0 0 2 1.7h.4a2 2 0 0 0 2-1.7l.1-.8a2 2 0 0 1 1.1-1.5l.8-.4a2 2 0 0 0 .8-2.7l-.4-.7a2 2 0 0 1 0-1.8l.4-.7a2 2 0 0 0-.8-2.7l-.8-.4a2 2 0 0 1-1.1-1.5l-.1-.8a2 2 0 0 0-2-1.7Z"/><circle cx="12" cy="12" r="3"/>',
    'sliders-horizontal': '<path d="M21 4h-7"/><path d="M10 4H3"/><path d="M21 12h-9"/><path d="M8 12H3"/><path d="M21 20h-5"/><path d="M12 20H3"/><circle cx="12" cy="4" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="14" cy="20" r="2"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.9 4.9 1.4 1.4"/><path d="m17.7 17.7 1.4 1.4"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.3 17.7-1.4 1.4"/><path d="m19.1 4.9-1.4 1.4"/>',
    thermometer: '<path d="M14 14.8V5a4 4 0 0 0-8 0v9.8a6 6 0 1 0 8 0Z"/><path d="M10 8v8"/>',
    zap: '<path d="M4 14a1 1 0 0 1-.8-1.6l9-11A1 1 0 0 1 14 2v7h5a1 1 0 0 1 .8 1.6l-9 11A1 1 0 0 1 10 21v-7Z"/>'
  };

  function createIcons() {
    document.querySelectorAll('i[data-lucide]').forEach((node) => {
      const name = node.getAttribute('data-lucide');
      const body = icons[name];
      if (!body) return;
      node.innerHTML = '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + body + '</svg>';
    });
  }

  window.lucide = { createIcons };
})();
