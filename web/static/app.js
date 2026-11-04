const form = document.getElementById('start');
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
    create.textContent = 'New meeting';
  }
});

document.getElementById('copy').addEventListener('click', async () => {
  Gigma.toast((await Gigma.copy(link.value)) ? 'Link copied' : 'Copy failed — long-press the link');
});
document.getElementById('sharebtn').addEventListener('click', () => {
  Gigma.share(link.value, current && current.title);
});
link.addEventListener('focus', () => link.select());
