from novabridge import NovaBridge

client = NovaBridge(host="127.0.0.1", port=30010)
print(client.spawn("PointLight", label="DemoLight", x=0, y=0, z=250))
