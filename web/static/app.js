Gigma.hydrate();

const form = document.getElementById('start-form');
const title = document.getElementById('title');
const create = document.getElementById('create');
const err = document.getElementById('err');
const share = document.getElementById('share');
const link = document.getElementById('link');
const open = document.getElementById('open');
let current = null;

form.addEventListener('submit', async (e) => {
  e.preventDefault();
  err.hidden = true;
  create.disabled = true;
  create.innerHTML = '<span class="spinner" aria-hidden="true"></span> Creating…';
  try {
    const res = await fetch('/api/sessions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title: title.value.trim() }),
    });
    const data = await res.json().catch(() => ({}));
    if (!res.ok) throw new Error(data.error || 'Could not create session');
    current = data;
    link.value = data.url;
    open.href = data.path;
    share.hidden = false;
    title.blur();
    share.scrollIntoView({ behavior: 'smooth', block: 'start' });
  } catch (e) {
    err.textContent = (e && e.message) || 'Network error — try again.';
    err.hidden = false;
  } finally {
    create.disabled = false;
    Gigma.setBtn(create, 'plus', 'New meeting');
  }
});

document.getElementById('copy').addEventListener('click', async () => {
  Gigma.toast((await Gigma.copy(link.value)) ? 'Link copied' : 'Copy failed — long-press the link');
});
document.getElementById('sharebtn').addEventListener('click', () => {
  Gigma.share(link.value, current && current.title);
});
link.addEventListener('focus', () => link.select());

// ---- Landing page extras ----
// Install paths: tap to copy.
document.querySelectorAll('[data-copy]').forEach((b) => {
  b.addEventListener('click', async () => {
    Gigma.toast((await Gigma.copy(b.dataset.copy)) ? 'Path copied' : 'Copy failed — select the path instead');
  });
});

// Highlight the download that matches this computer; phones get a hint instead.
(() => {
  const ua = navigator.userAgent || '';
  const plat = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
  const phone = /iPhone|iPad|iPod|Android/i.test(ua) || (/Mac/.test(plat) && navigator.maxTouchPoints > 1);
  if (phone) { document.getElementById('mobile-note').hidden = false; return; }
  const card = /Win/i.test(plat + ua) ? 'dl-win' : /Mac/i.test(plat + ua) ? 'dl-mac' : null;
  if (!card) return;
  const el = document.getElementById(card);
  el.classList.add('is-rec');
  el.querySelector('.rec').hidden = false;
})();

// Decorative level meter in the hero illustration.
(() => {
  const m = document.getElementById('mock-meter');
  if (!m) return;
  for (let i = 0; i < 16; i++) {
    const b = document.createElement('i');
    b.style.setProperty('--d', (i * 0.11).toFixed(2) + 's');
    b.style.setProperty('--h', (35 + ((i * 37) % 60)) + '%');
    m.appendChild(b);
  }
})();
