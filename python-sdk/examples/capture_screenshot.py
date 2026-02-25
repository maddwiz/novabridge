from novabridge import NovaBridge

client = NovaBridge(host="127.0.0.1", port=30010)
print(client.viewport_screenshot(width=1280, height=720, save_path="viewport.png"))
