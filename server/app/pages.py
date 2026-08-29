"""Page blueprint — a single landing route.

/  plain HTML landing page with copy-paste API examples.
"""

from flask import Blueprint, Response

pages_bp = Blueprint("pages", __name__)

_INDEX_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SeedFinder</title>
</head>
<body>
<h1>SeedFinder</h1>

<p>
Minecraft Bedrock structure locator. Start the local API server with
<code>server\\start.bat</code> (Windows), <code>server/start.sh</code> (Linux),
or run the pre-built <code>SeedFinder.exe</code> — all endpoints listen on
<code>http://127.0.0.1:7890</code>.
</p>

<h2>cmd (curl)</h2>
<pre><code>curl "http://127.0.0.1:7890/status"

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

<h2>cmd (curl)</h2>
<pre><code>curl -X POST "http://127.0.0.1:7890/seedcracker" ^
  -H "Content-Type: application/json" ^
  -d "[{\"tolerance\":0},{\"type\":5,\"x\":-280,\"z\":152},{\"type\":5,\"x\":-280,\"z\":-360},{\"type\":8,\"x\":696,\"z\":360},{\"type\":8,\"x\":712,\"z\":760}]"</code></pre>

<h2>Python (stdlib only)</h2>
<pre><code>import json, urllib.request

payload = [
    {"tolerance": 0},
    {"type": 5, "x": -280, "z": 152},
    {"type": 5, "x": -280, "z": -360},
    {"type": 8, "x": 696, "z": 360},
    {"type": 8, "x": 712, "z": 760},
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
</body>
</html>
"""


@pages_bp.route("/")
def index():
    return Response(_INDEX_HTML, mimetype="text/html")