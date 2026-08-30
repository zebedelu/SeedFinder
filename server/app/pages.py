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
<style>
  pre {
    background: #f5f5f5;
    border: 1px solid #ccc;
    padding: 10px;
    overflow-x: auto;
  }
  textarea {
    width: 100%;
    box-sizing: border-box;
    min-height: 130px;
  }
  button:disabled {
    color: #888;
    background: #ddd;
  }
</style>
</head>
<body>
<h1>Structures IDs</h1>
<table border="1" cellpadding="6" cellspacing="0">
  <tr><th>ID — Structure</th><th>ID — Structure</th><th>ID — Structure</th></tr>
  <tr><td>1 — Desert Pyramid</td><td>2 — Jungle Temple</td><td>3 — Swamp Hut</td></tr>
  <tr><td>4 — Igloo</td><td>5 — Village</td><td>6 — Ocean Ruin</td></tr>
  <tr><td>7 — Shipwreck</td><td>8 — Monument</td><td>9 — Mansion</td></tr>
  <tr><td>10 — Outpost</td><td>11 — Ruined Portal</td><td>12 — Ruined Portal (nether)</td></tr>
  <tr><td>13 — Ancient City</td><td>14 — Treasure</td><td>15 — Mineshaft</td></tr>
  <tr><td>23 — Trail Ruins</td><td>24 — Trial Chambers</td><td></td></tr>
</table>

<h1>SeedFinder</h1>

<p>
Minecraft Bedrock structure locator. Start the local API server with
<code>server\\start.bat</code> (Windows), <code>server/start.sh</code> (Linux),
or run the pre-built <code>SeedFinder.exe</code> — all endpoints listen on
<code>http://127.0.0.1:7890</code>.
</p>

<h2>Try it</h2>
<textarea id="scan-payload" spellcheck="false">{
  "seed": 8675309,
  "x": 0,
  "z": 0,
  "radius": 100,
  "max": 20,
  "types": [5, 1, 10]
}</textarea>
<p><button id="scan-go">Search</button> <button id="scan-clear">Clear</button></p>
<pre id="scan-out" hidden></pre>

<h2>cmd (curl)</h2>

curl "http://127.0.0.1:7890/scan?seed=8675309&x=0&z=0&radius=100&max=20&types=5,1,10"</code></pre>

<h2>Python (stdlib only)</h2>
<pre><code>import json, urllib.request

url = "http://127.0.0.1:7890/scan?seed=8675309&x=0&z=0&radius=100&max=20&types=5,1,10"
data = json.loads(urllib.request.urlopen(url).read())

for s in data["results"]:
    print(f"{s['name']:20s} x={s['x']:6d} z={s['z']:6d}  dist={s['distance']:.1f}")</code></pre>

<h1>SeedCracker</h1>

<p>
Reverse-seed tool. Given 4+ structure coordinates it recovers the most probable
Bedrock world seed(s). POST a JSON payload to <code>/seedcracker</code>.
</p>

<h2>Try it</h2>
<textarea id="crack-payload" spellcheck="false">[
  {"tolerance": 0, "max_seconds": 10, "end": 6000000},
  {"type": 5, "x": 648, "z": -280},
  {"type": 5, "x": 680, "z": 664},
  {"type": 5, "x": -936, "z": 200},
  {"type": 5, "x": 888, "z": -936},
  {"type": 5, "x": -1288, "z": 264}
]</textarea>
<p><button id="crack-go">Search</button> <button id="crack-clear">Clear</button></p>
<pre id="crack-out" hidden></pre>

<h2>cmd (curl)</h2>
<pre><code>curl -X POST "http://127.0.0.1:7890/seedcracker" ^
  -H "Content-Type: application/json" ^
  -d "[{\\"tolerance\\":6,\\"max_seconds\\":10,\\"end\\":6000000},{\\"type\\":5,\\"x\\":648,\\"z\\":-280},{\\"type\\":5,\\"x\\":680,\\"z\\":664},{\\"type\\":5,\\"x\\":-936,\\"z\\":200},{\\"type\\":5,\\"x\\":888,\\"z\\":-936},{\\"type\\":5,\\"x\\":-1288,\\"z\\":264}]"</code></pre>

<h2>Python (stdlib only)</h2>
<pre><code>import json, urllib.request

payload = [
    {"tolerance": 6, "max_seconds": 10, "end": 6000000},
    {"type": 5, "x": 648, "z": -280},
    {"type": 5, "x": 680, "z": 664},
    {"type": 5, "x": -936, "z": 200},
    {"type": 5, "x": 888, "z": -936},
    {"type": 5, "x": -1288, "z": 264},
]
req = urllib.request.Request(
    "http://127.0.0.1:7890/seedcracker",
    data=json.dumps(payload).encode(),
    headers={"Content-Type": "application/json"},
)
data = json.loads(urllib.request.urlopen(req).read())

for r in data[1:]:
    print(f"seed={r['seed']}  score={r['score']}")
    for m in r.get("matches", []):
        print(f"  type={m[0]} x={m[1]} z={m[2]}")</code></pre>

<p><a href="https://github.com/zebedelu/SeedFinder">github.com/zebedelu/SeedFinder</a></p>

<script>
function wire(btnId, outId, run) {
  const btn = document.getElementById(btnId);
  const out = document.getElementById(outId);
  btn.addEventListener("click", async () => {
    btn.disabled = true;
    btn.textContent = "Searching...";
    out.hidden = true;
    try {
      out.textContent = await run();
    } catch (e) {
      out.textContent = "Error: " + e.message;
    } finally {
      out.hidden = false;
      btn.disabled = false;
      btn.textContent = "Search";
    }
  });
}

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
  for (const k of ["seed", "x", "z", "radius", "max", "types"]) {
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
