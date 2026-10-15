"""Push names from symbols/functions.json into the running Binary Ninja via the MCP plugin's HTTP API.

  python tools/bn_apply_names.py            # apply all
  python tools/bn_apply_names.py --dry-run  # show what would change

Creates a function first if Binary Ninja has none starting at that address.
"""
import json
import os
import sys
import urllib.parse
import urllib.request

BASE = os.environ.get("BN_MCP", "http://127.0.0.1:9009")
HERE = os.path.dirname(os.path.abspath(__file__))
NAMES = os.path.join(HERE, "..", "symbols", "functions.json")


def get(path, **params):
    url = BASE + path + ("?" + urllib.parse.urlencode(params) if params else "")
    with urllib.request.urlopen(url, timeout=60) as r:
        return json.loads(r.read().decode())


def post(path, **params):
    """Mutating endpoints (renameFunction, comment, ...) are POST with form-encoded params."""
    data = urllib.parse.urlencode(params).encode()
    req = urllib.request.Request(BASE + path, data=data,
                                 headers={"Content-Type": "application/x-www-form-urlencoded"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.loads(r.read().decode())


def all_functions():
    out = {}
    off = 0
    while True:
        j = get("/methods", offset=off, limit=5000)["functions"]
        if not j:
            break
        for f in j:
            out[int(f["address"], 16)] = f["name"]
        off += len(j)
    return out


def main():
    dry = "--dry-run" in sys.argv
    names = {int(k, 16): v for k, v in json.load(open(NAMES, encoding="utf-8")).items() if not k.startswith("_")}
    have = all_functions()
    created = renamed = same = 0
    for addr in sorted(names):
        want = names[addr]
        cur = have.get(addr)
        if cur is None:
            print("create %08x %s" % (addr, want))
            if not dry:
                r = get("/makeFunctionAt", address="0x%08x" % addr)
                fl = get("/functionAt", address="0x%08x" % addr).get("functions") or []
                # functionAt returns the containing function too; only accept an exact start
                cur = None
                for nm in fl:
                    if nm.endswith("%x" % addr) or nm == want:
                        cur = nm
                if cur is None:
                    print("   could not create/locate function at %08x: %s %s" % (addr, r, fl))
                    continue
            created += 1
        if cur == want:
            same += 1
            continue
        print("rename %08x %-24s -> %s" % (addr, cur, want))
        if not dry and cur is not None:
            r = post("/renameFunction", oldName=cur, newName=want)
            if "error" in r:
                print("   ERROR", r)
        renamed += 1
    print("created %d, renamed %d, unchanged %d" % (created, renamed, same))


if __name__ == "__main__":
    main()
