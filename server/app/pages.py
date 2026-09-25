"""Page blueprint — a single landing route.

/  plain HTML landing page with SeedFinder/SeedCracker forms and copy-paste API examples.
"""

from flask import Blueprint, Response

pages_bp = Blueprint("pages", __name__)

_INDEX_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SeedFinder</title>
<link rel="icon" href="data:image/png;base64,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">
<style>
  :root {
    --bg: #0d0f14;
    --surface: #151821;
    --surface-alt: #1a1d27;
    --border: #272b37;
    --border-light: #323646;
    --text: #d1d5db;
    --text-muted: #6b7280;
    --text-dim: #4b5563;
    --accent: #d4a34a;
    --accent-blue: #4a9ece;
    --code-bg: #080a0e;
    --danger: #e05252;
    --font: 'SF Pro Text', 'Helvetica Neue', 'Segoe UI', system-ui, sans-serif;
    --font-mono: 'SF Mono', 'Cascadia Code', 'Consolas', 'Courier New', monospace;
  }
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    background: var(--bg);
    color: var(--text);
    font-family: var(--font);
    font-size: 18px;
    line-height: 1.6;
    -webkit-font-smoothing: antialiased;
    padding: 40px 24px 60px;
    min-height: 100vh;
    background-image:
      radial-gradient(circle at 1px 1px, rgba(255,255,255,0.03) 1px, transparent 0);
    background-size: 32px 32px;
  }
  .container {
    max-width: 880px;
    margin: 0 auto;
  }

  /* Header */
  .header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 8px;
  }
  .header .brand {
    display: flex;
    align-items: center;
    gap: 14px;
  }
  .header .logo {
    width: 44px;
    height: 44px;
    border-radius: 8px;
    display: block;
  }
  .header h1 {
    font-size: 32px;
    font-weight: 600;
    letter-spacing: -0.3px;
    color: #fff;
  }
  .header h1 span { color: var(--accent); }
  .header .subtitle {
    font-size: 18px;
    color: var(--text-muted);
  }
  .header .gh-link {
    color: var(--text-muted);
    text-decoration: none;
    font-size: 15px;
    transition: color 0.15s;
  }
  .header .gh-link:hover { color: var(--accent); }
  .divider {
    border: none;
    border-top: 1px solid var(--border);
    margin: 20px 0 28px;
  }

  /* Section cards */
  .section {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 24px 28px;
    margin-bottom: 24px;
  }
  .section-title {
    font-size: 24px;
    font-weight: 600;
    color: #fff;
    margin-bottom: 4px;
    display: flex;
    align-items: center;
    gap: 10px;
  }
  .section-title .marker {
    display: inline-block;
    width: 3px;
    height: 20px;
    border-radius: 2px;
    flex-shrink: 0;
  }
  .section-title .marker.gold { background: var(--accent); }
  .section-title .marker.blue { background: var(--accent-blue); }
  .section-desc {
    font-size: 17px;
    color: var(--text-muted);
    margin-bottom: 18px;
    margin-left: 13px;
  }

  /* Structure IDs table */
  .id-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
  }
  .id-table th {
    text-align: left;
    font-weight: 500;
    color: var(--text-muted);
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    padding: 0 0 8px 0;
    border-bottom: 1px solid var(--border);
  }
  .id-table td {
    padding: 5px 0;
    border-bottom: 1px solid var(--border-light);
    color: var(--text);
    font-family: var(--font-mono);
    font-size: 14px;
  }
  .id-table tr:last-child td { border-bottom: none; }
  .id-table td:empty { border-bottom: none; }
  .id-table .id-num { color: var(--accent); }

  /* Interactive area */
  .playground { margin-top: 4px; }
  .playground textarea {
    width: 100%;
    overflow: hidden;
    min-height: 40px;
    background: var(--code-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: #c9d1d9;
    font-family: var(--font-mono);
    font-size: 13px;
    line-height: 1.5;
    padding: 12px 14px;
    resize: vertical;
    outline: none;
    transition: border-color 0.15s;
  }
  .playground textarea:focus { border-color: var(--accent); }
  .playground .actions {
    display: flex;
    gap: 8px;
    margin: 10px 0 12px;
  }
  .playground .actions button {
    padding: 7px 18px;
    font-size: 13px;
    font-family: var(--font);
    border: 1px solid var(--border);
    border-radius: 5px;
    cursor: pointer;
    transition: all 0.15s;
    background: var(--surface-alt);
    color: var(--text);
  }
  .playground .actions button:hover:not(:disabled) {
    border-color: var(--text-muted);
    background: #222635;
  }
  .playground .actions button.primary {
    background: var(--accent);
    color: #0d0f14;
    border-color: var(--accent);
    font-weight: 500;
  }
  .playground .actions button.primary:hover:not(:disabled) {
    background: #deaf52;
    border-color: #deaf52;
  }
  .playground .actions button.primary:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }
  .playground .actions button.primary.blue {
    background: var(--accent-blue);
    border-color: var(--accent-blue);
  }
  .playground .actions button.primary.blue:hover:not(:disabled) {
    background: #5bb0dc;
    border-color: #5bb0dc;
  }
  .playground pre {
    background: var(--code-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 14px 16px;
    font-family: var(--font-mono);
    font-size: 13px;
    line-height: 1.5;
    overflow-x: auto;
    max-width: 100%;
    color: #b0b8c4;
    white-space: pre-wrap;
    word-break: break-all;
    margin: 0;
  }
  .playground pre.error {
    border-color: var(--danger);
    color: #f48787;
  }

  /* Code examples */
  .code-examples {
    display: grid;
    grid-template-columns: 1fr;
    gap: 14px;
    margin-top: 20px;
  }
  .code-block {
    min-width: 0;
    background: var(--code-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
  }
  .code-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 8px 14px 0;
  }
  .code-head .label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.8px;
    color: var(--text-dim);
    font-weight: 500;
  }
  .copy-wrap { position: relative; }
  .copy-msg {
    position: absolute;
    bottom: calc(100% + 6px);
    right: 0;
    background: #13210f;
    color: #8bc34a;
    border: 1px solid #3a5a2a;
    border-radius: 4px;
    font-size: 11px;
    padding: 2px 8px;
    white-space: nowrap;
    z-index: 10;
  }
  .copy-btn {
    background: none;
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text-muted);
    font-size: 11px;
    font-family: var(--font);
    padding: 2px 10px;
    cursor: pointer;
    transition: all 0.15s;
  }
  .copy-btn:hover { color: var(--accent); border-color: var(--text-muted); }
  .code-block pre {
    padding: 10px 14px 14px;
    font-family: var(--font-mono);
    font-size: 13px;
    line-height: 1.5;
    overflow-x: auto;
    color: #9ca3af;
    white-space: pre;
    margin: 0;
    max-width: 100%;
  }
  .code-block .kw { color: #c9a04a; }
  .code-block .str { color: #9fbb73; }
  .code-block .fn { color: #6bb0d6; }

  /* Footer */
  .footer {
    text-align: center;
    margin-top: 32px;
    font-size: 13px;
    color: var(--text-dim);
  }
  .footer a {
    color: var(--text-muted);
    text-decoration: none;
    transition: color 0.15s;
  }
  .footer a:hover { color: var(--accent); }
</style>
</head>
<body>
<div class="container">

<div class="header">
  <div class="brand">
    <img class="logo" src="data:image/png;base64,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" alt="SeedFinder logo">
    <div>
      <h1>SeedFinder <span>API</span></h1>
      <div class="subtitle">Minecraft Bedrock &amp; Java structure finder, local API server</div>
    </div>
  </div>
  <a class="gh-link" href="https://github.com/zebedelu/SeedFinder" target="_blank" rel="noopener">github &#8599;</a>
</div>
<hr class="divider">

<!-- Structure IDs -->
<div class="section">
  <div class="section-title">
    <span class="marker gold"></span>
    Structure IDs
  </div>
  <div class="section-desc">Reference of structure type IDs used by the <code>/scan</code> endpoint.</div>
  <table class="id-table">
    <tr><th>ID</th><th>Structure</th><th>ID</th><th>Structure</th><th>ID</th><th>Structure</th></tr>
    <tr><td><span class="id-num">1</span></td><td>Desert Pyramid</td><td><span class="id-num">7</span></td><td>Shipwreck</td><td><span class="id-num">13</span></td><td>Ancient City</td></tr>
    <tr><td><span class="id-num">2</span></td><td>Jungle Temple</td><td><span class="id-num">8</span></td><td>Monument</td><td><span class="id-num">14</span></td><td>Buried Treasure</td></tr>
    <tr><td><span class="id-num">3</span></td><td>Swamp Hut</td><td><span class="id-num">9</span></td><td>Mansion</td><td><span class="id-num">15</span></td><td>Mineshaft</td></tr>
    <tr><td><span class="id-num">4</span></td><td>Igloo</td><td><span class="id-num">10</span></td><td>Outpost</td><td><span class="id-num">23</span></td><td>Trail Ruins</td></tr>
    <tr><td><span class="id-num">5</span></td><td>Village</td><td><span class="id-num">11</span></td><td>Ruined Portal</td><td><span class="id-num">24</span></td><td>Trial Chambers</td></tr>
    <tr><td><span class="id-num">6</span></td><td>Ocean Ruin</td><td><span class="id-num">12</span></td><td>Ruined Portal (nether)</td><td></td><td></td></tr>
  </table>
</div>

<!-- SeedFinder Scan -->
<div class="section">
  <div class="section-title">
    <span class="marker gold"></span>
    SeedFinder Scan
  </div>
  <div class="section-desc">
    Find structures around a position, on Bedrock or Java. This sends a <code>GET /scan</code> request with the parameters below.
    Add <code>"mc": "java"</code> to ask for <b>Java</b> structures; without it you get <code>bedrock</code>. Only the first letter counts (<code>j…</code>/<code>b…</code>), so <code>jova</code> works too.
    The same parameters also go as a JSON body on <code>POST /scan</code>. <code>/scan/java</code> and <code>/scan/bedrock</code> fix the edition and ignore <code>mc</code>. <code>mc</code> means edition here, not game version; <code>version</code> is reserved for that. Java always targets the latest release.
  </div>

  <div class="playground">
    <textarea id="scan-payload" spellcheck="false">{
  "seed": 8675309,
  "x": 0,
  "z": 0,
  "radius": 100,
  "max": 20,
  "types": "5,1,10",
  "mc": "bedrock"
}</textarea>
    <div class="actions">
      <button class="primary" id="scan-go">Search</button>
      <button id="scan-clear">Clear</button>
    </div>
    <pre id="scan-out" hidden></pre>
  </div>

  <div class="code-examples">
    <div class="code-block">
      <div class="code-head">
        <span class="label">curl - both (mc parameter)</span>
        <span class="copy-wrap">
          <span class="copy-msg" hidden>Copied!</span>
          <button type="button" class="copy-btn" data-copy-target="curl-scan">Copy</button>
        </span>
      </div>
      <pre id="curl-scan">curl "http://127.0.0.1:7890/scan?seed=8675309&amp;x=0&amp;z=0&amp;radius=100&amp;max=20&amp;types=5,1,10&amp;mc=bedrock"</pre>
    </div>
    <div class="code-block">
      <div class="code-head">
        <span class="label">curl - java</span>
        <span class="copy-wrap">
          <span class="copy-msg" hidden>Copied!</span>
          <button type="button" class="copy-btn" data-copy-target="curl-scan-java">Copy</button>
        </span>
      </div>
      <pre id="curl-scan-java">curl "http://127.0.0.1:7890/scan/java?seed=8675309&amp;x=0&amp;z=0&amp;radius=100&amp;max=20&amp;types=5,1,10"</pre>
    </div>
    <div class="code-block">
      <div class="code-head">
        <span class="label">curl - bedrock</span>
        <span class="copy-wrap">
          <span class="copy-msg" hidden>Copied!</span>
          <button type="button" class="copy-btn" data-copy-target="curl-scan-bedrock">Copy</button>
        </span>
      </div>
      <pre id="curl-scan-bedrock">curl "http://127.0.0.1:7890/scan/bedrock?seed=8675309&amp;x=0&amp;z=0&amp;radius=100&amp;max=20&amp;types=5,1,10"</pre>
    </div>
    <div class="code-block">
      <div class="code-head">
        <span class="label">curl - post + mc</span>
        <span class="copy-wrap">
          <span class="copy-msg" hidden>Copied!</span>
          <button type="button" class="copy-btn" data-copy-target="curl-scan-post">Copy</button>
        </span>
      </div>
      <pre id="curl-scan-post">curl -X POST "http://127.0.0.1:7890/scan" \\
  -H "Content-Type: application/json" \\
  -d '{"seed": 8675309, "x": 0, "z": 0, "radius": 100, "max": 20, "types": "5,1,10", "mc": "java"}'</pre>
    </div>
    <div class="code-block">
      <div class="code-head">
        <span class="label">Python (requests)</span>
        <span class="copy-wrap">
          <span class="copy-msg" hidden>Copied!</span>
          <button type="button" class="copy-btn" data-copy-target="py-scan">Copy</button>
        </span>
      </div>
      <pre id="py-scan"><span class="kw">import</span> requests

url = <span class="str">"http://127.0.0.1:7890/scan"</span>
params = {<span class="str">"seed"</span>: 8675309, <span class="str">"x"</span>: 0, <span class="str">"z"</span>: 0, <span class="str">"radius"</span>: 100, <span class="str">"max"</span>: 20, <span class="str">"types"</span>: <span class="str">"5,1,10"</span>}
data = requests.get(url, params=params).json()

<span class="kw">for</span> s <span class="kw">in</span> data[<span class="str">"results"</span>]:
    print(<span class="fn">f</span><span class="str">"{s['name']:20s} x={s['x']:6d} z={s['z']:6d}  dist={s['distance']:.1f}"</span>)</pre>
    </div>
  </div>
</div>

<!-- SeedCracker -->
<div class="section">
  <div class="section-title">
    <span class="marker blue"></span>
    SeedCracker
  </div>
  <div class="section-desc">
    Give it the coordinates of 4 or more structures and it searches for a world where those structures land exactly where you placed them. For now it works on Bedrock only; Java is a possibility down the road, and the Bedrock search itself still has room to improve. Sends a <code>POST /seedcracker</code> request.
  </div>

  <div class="playground">
    <textarea id="crack-payload" spellcheck="false">[
  {"tolerance": 0, "max_seconds": 10, "end": 6000000},
  {"type": 5, "x": 648, "z": -280},
  {"type": 5, "x": 680, "z": 664},
  {"type": 5, "x": -936, "z": 200},
  {"type": 5, "x": 888, "z": -936},
  {"type": 5, "x": -1288, "z": 264}
]</textarea>
    <div class="actions">
      <button class="primary blue" id="crack-go">Search</button>
      <button id="crack-clear">Clear</button>
    </div>
    <pre id="crack-out" hidden></pre>
  </div>

  <div class="code-examples">
    <div class="code-block">
      <div class="code-head">
        <span class="label">Python (requests)</span>
        <span class="copy-wrap">
          <span class="copy-msg" hidden>Copied!</span>
          <button type="button" class="copy-btn" data-copy-target="py-crack">Copy</button>
        </span>
      </div>
      <pre id="py-crack"><span class="kw">import</span> requests

payload = [
  {<span class="str">"tolerance"</span>: 6, <span class="str">"max_seconds"</span>: 10, <span class="str">"end"</span>: 6000000},
  {<span class="str">"type"</span>: 5, <span class="str">"x"</span>: 648, <span class="str">"z"</span>: -280},
  {<span class="str">"type"</span>: 5, <span class="str">"x"</span>: 680, <span class="str">"z"</span>: 664},
  {<span class="str">"type"</span>: 5, <span class="str">"x"</span>: -936, <span class="str">"z"</span>: 200},
  {<span class="str">"type"</span>: 5, <span class="str">"x"</span>: 888, <span class="str">"z"</span>: -936},
  {<span class="str">"type"</span>: 5, <span class="str">"x"</span>: -1288, <span class="str">"z"</span>: 264},
]
data = requests.post(<span class="str">"http://127.0.0.1:7890/seedcracker"</span>, json=payload).json()
print(data)</pre>
    </div>
  </div>
</div>

<div class="footer">
  <a href="https://github.com/zebedelu/SeedFinder" target="_blank" rel="noopener">github.com/zebedelu/SeedFinder</a>
</div>

</div>

<script>
function resize(ta) {
  ta.style.height = "0";
  ta.style.height = ta.scrollHeight + "px";
}
document.querySelectorAll("#scan-payload, #crack-payload").forEach(ta => {
  ta.addEventListener("input", () => resize(ta));
  resize(ta);
});

function wire(btnId, outId, run) {
  const btn = document.getElementById(btnId);
  const out = document.getElementById(outId);
  btn.addEventListener("click", async () => {
    btn.disabled = true;
    btn.textContent = "Searching...";
    out.hidden = true;
    out.classList.remove("error");
    try {
      out.textContent = await run();
    } catch (e) {
      out.textContent = "Error: " + e.message;
      out.classList.add("error");
    } finally {
      out.hidden = false;
      btn.disabled = false;
      btn.textContent = "Search";
    }
  });
}

/* Copy buttons: show a "Copied!" tooltip above the clicked button. */
document.querySelectorAll(".copy-btn").forEach((btn) => {
  btn.addEventListener("click", async () => {
    const pre = document.getElementById(btn.dataset.copyTarget);
    const text = pre.textContent;
    try {
      await navigator.clipboard.writeText(text);
    } catch (e) {
      const ta = document.createElement("textarea");
      ta.value = text;
      document.body.appendChild(ta);
      ta.select();
      document.execCommand("copy");
      ta.remove();
    }
    const msg = btn.previousElementSibling;
    msg.hidden = false;
    setTimeout(() => { msg.hidden = true; }, 1500);
  });
});

wire("crack-go", "crack-out", async () => {
  const res = await fetch("/seedcracker", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: document.getElementById("crack-payload").value,
  });
  return JSON.stringify(await res.json(), null, 2);
});

document.getElementById("scan-clear").addEventListener("click", () => {
  const out = document.getElementById("scan-out");
  out.textContent = "";
  out.hidden = true;
});
document.getElementById("crack-clear").addEventListener("click", () => {
  const out = document.getElementById("crack-out");
  out.textContent = "";
  out.hidden = true;
});

wire("scan-go", "scan-out", async () => {
  const p = JSON.parse(document.getElementById("scan-payload").value);
  const q = new URLSearchParams();
  for (const k of ["seed", "x", "z", "radius", "max", "types", "mc"]) {
    if (p[k] !== undefined) q.set(k, p[k]);
  }
  const res = await fetch("/scan?" + q);
  return JSON.stringify(await res.json(), null, 2);
});
</script>
</body>
</html>
"""


@pages_bp.route("/")
def index():
    return Response(_INDEX_HTML, mimetype="text/html")