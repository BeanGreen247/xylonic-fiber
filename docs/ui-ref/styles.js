((sels) => {
  const props = ['color','backgroundColor','fontFamily','fontSize','fontWeight','lineHeight','letterSpacing','border','borderRadius','padding','margin','boxShadow','gap','opacity'];
  return sels.map(sel => {
    const el = document.querySelector(sel); if (!el) return sel + ' : (none)';
    const r = el.getBoundingClientRect(), cs = getComputedStyle(el);
    const o = props.map(p => cs[p]).filter((v,i)=> v && v!=='none' && v!=='normal' && v!=='0px' && v!=='rgba(0, 0, 0, 0)' && v!=='0px none rgb(255, 255, 255)').map((v,i)=>v);
    const d = {}; props.forEach(p => { const v = cs[p]; if (v && v!=='none' && v!=='normal' && v!=='0px' && v!=='rgba(0, 0, 0, 0)' && !/^0px none/.test(v)) d[p]=v; });
    return `${sel} [${Math.round(r.x)},${Math.round(r.y)} ${Math.round(r.width)}x${Math.round(r.height)}] ` + JSON.stringify(d);
  }).join('\n');
})(SELS)
