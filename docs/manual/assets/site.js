// PHI manual: menu toggle on narrow screens, and a search over every heading.
(function () {
  const toggle = document.querySelector(".menu-toggle");
  if (toggle)
    toggle.addEventListener("click", () => document.body.classList.toggle("side-open"));

  const here = document.querySelector(".side li.here");
  if (here) here.scrollIntoView({ block: "center" });

  const input = document.querySelector(".side .search");
  const results = document.querySelector(".side .search-results");
  const toc = document.querySelector(".side .side-toc");
  if (!input) return;

  let data = null;
  function load(then) {
    if (data) return then();
    const s = document.createElement("script");
    s.src = "assets/search.js";
    s.onload = () => { data = window.PHI_SEARCH || []; then(); };
    document.head.appendChild(s);
  }

  function run() {
    const q = input.value.trim().toLowerCase();
    if (!q) { results.hidden = true; toc.hidden = false; return; }
    const words = q.split(/\s+/);
    const hits = data.filter(d => words.every(w => (d.t + " " + d.c).toLowerCase().includes(w))).slice(0, 40);
    results.innerHTML = "";
    for (const h of hits) {
      const li = document.createElement("li");
      const a = document.createElement("a");
      a.href = h.h;
      a.textContent = h.t;
      const small = document.createElement("small");
      small.textContent = h.c;
      a.appendChild(small);
      li.appendChild(a);
      results.appendChild(li);
    }
    if (!hits.length) results.innerHTML = '<li style="padding:6px;color:#8a7f63">Nothing found</li>';
    results.hidden = false;
    toc.hidden = true;
  }
  input.addEventListener("input", () => load(run));
  input.addEventListener("keydown", e => {
    if (e.key === "Enter") { const first = results.querySelector("a"); if (first) location.href = first.href; }
    if (e.key === "Escape") { input.value = ""; run(); }
  });
  document.addEventListener("keydown", e => {
    if (e.key === "/" && document.activeElement !== input) { e.preventDefault(); input.focus(); }
  });
})();
