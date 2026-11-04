const title = document.getElementById('title');
const create = document.getElementById('create');
const err = document.getElementById('err');
const share = document.getElementById('share');
const link = document.getElementById('link');
const open = document.getElementById('open');
const copy = document.getElementById('copy');

create.addEventListener('click', async () => {
  err.hidden = true;
  create.disabled = true;
  try {
    const res = await fetch('/api/sessions', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title: title.value.trim() }),
    });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || 'Could not create session');
    link.value = data.url;
    open.href = data.path;
    share.hidden = false;
  } catch (e) {
    err.textContent = e.message || String(e);
    err.hidden = false;
  } finally {
    create.disabled = false;
  }
});

copy.addEventListener('click', async () => {
  await navigator.clipboard.writeText(link.value);
  copy.textContent = 'Copied';
  setTimeout(() => { copy.textContent = 'Copy'; }, 1200);
});
