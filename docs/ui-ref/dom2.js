((rootSel, maxd) => {
  const out = [];
  const walk = (el, d) => {
    if (d > maxd) return;
    const r = el.getBoundingClientRect();
    if (r.width === 0 || r.height === 0) return;
    const cs = getComputedStyle(el);
    const cls = (el.className && el.className.baseVal === undefined ? String(el.className) : '').trim().split(/\s+/).join('.');
    const txt = el.children.length === 0 ? (el.innerText || '').trim().slice(0, 20) : '';
    const bits = [];
    if (cs.backgroundColor !== 'rgba(0, 0, 0, 0)') bits.push('bg=' + cs.backgroundColor);
    if (txt || el.tagName === 'I') bits.push(`fs=${cs.fontSize} fw=${cs.fontWeight} c=${cs.color}`);
    if (cs.borderRadius !== '0px') bits.push('r=' + cs.borderRadius);
    if (!/^0px none/.test(cs.border) && cs.border) bits.push('b=' + cs.border.slice(0, 40));
    if (cs.textAlign !== 'start' && txt) bits.push('ta=' + cs.textAlign);
    out.push(`${' '.repeat(d)}${el.tagName.toLowerCase()}${cls ? '.' + cls : ''} [${Math.round(r.x)},${Math.round(r.y)} ${Math.round(r.width)}x${Math.round(r.height)}] ${bits.join(' ')}${txt ? ' "' + txt + '"' : ''}`);
    for (const c of el.children) walk(c, d + 1);
  };
  const el = document.querySelector(rootSel);
  if (el) walk(el, 0);
  return out.join('\n');
})(ROOT, DEPTH)
