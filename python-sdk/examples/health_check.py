from novabridge import NovaBridge

client = NovaBridge(host="127.0.0.1", port=30010)
print(client.health())
