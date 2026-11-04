// Shared helpers for Gigma pages (copy/share with fallbacks, toast).
window.Gigma = (() => {
  let toastEl, toastTimer;
  function toast(msg) {
    if (!toastEl) {
      toastEl = document.createElement('div');
      toastEl.className = 'toast';
      toastEl.setAttribute('role', 'status');
      toastEl.setAttribute('aria-live', 'polite');
      document.body.appendChild(toastEl);
    }
    toastEl.textContent = msg;
    toastEl.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove('show'), 1800);
  }
  async function copy(text) {
    try {
      await navigator.clipboard.writeText(text);
      return true;
    } catch (_) {
      // Fallback for in-app browsers / older WebViews without async clipboard.
      const ta = document.createElement('textarea');
      ta.value = text; ta.setAttribute('readonly', '');
      ta.style.position = 'fixed'; ta.style.opacity = '0'; ta.style.fontSize = '16px';
      document.body.appendChild(ta); ta.select(); ta.setSelectionRange(0, text.length);
      let ok = false;
      try { ok = document.execCommand('copy'); } catch (_) {}
      ta.remove();
      return ok;
    }
  }
  // Native share sheet on phones (WhatsApp, Messages…); copy elsewhere.
  async function share(url, title) {
    if (navigator.share && matchMedia('(pointer: coarse)').matches) {
      try { await navigator.share({ title: title || 'Gigma session', text: 'Join my Gigma session', url }); return; }
      catch (e) { if (e && e.name === 'AbortError') return; }
    }
    toast((await copy(url)) ? 'Link copied' : 'Copy failed — long-press the link');
  }
  function initial(s) { return (String(s || '?').trim()[0] || '?').toUpperCase(); }
  return { toast, copy, share, initial };
})();
