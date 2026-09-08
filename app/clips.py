"""Search the clip / replay log by tag.
  python clips.py                 -> list everything, newest first
  python clips.py headshot        -> only entries tagged headshot
  python clips.py tank car        -> tagged tank OR car
  python clips.py --tags          -> tag counts"""
import json
import os
import sys
from collections import Counter


def load():
    out = []
    for fn, kind in (("clips.jsonl", "twitch"), ("replays.jsonl", "replay")):
        if os.path.exists(fn):
            for line in open(fn, encoding="utf-8"):
                if line.strip():
                    e = json.loads(line); e["_kind"] = kind; out.append(e)
    return sorted(out, key=lambda e: e.get("created", ""), reverse=True)


entries = load()
if "--tags" in sys.argv:
    for tag, n in Counter(t for e in entries for t in e.get("tags", [])).most_common():
        print(f"{n:4}  {tag}")
    sys.exit()
want = {a.lower() for a in sys.argv[1:] if not a.startswith("--")}
for e in entries:
    tags = [t.lower() for t in e.get("tags", [])]
    if want and not want & set(tags):
        continue
    where = e.get("url") or e.get("file", "")
    print(f"{e.get('created','')}  {e['_kind']:6}  {e.get('title',''):36}  [{' '.join(tags)}]  {where}")
