import sys

from novabridge import NovaBridge

if len(sys.argv) < 2:
    raise SystemExit("usage: python runtime_pair_and_health.py <pairing_code>")

code = sys.argv[1]
client = NovaBridge(host="127.0.0.1", port=30020)
print(client.runtime_pair(code, role="automation"))
print(client.health())
