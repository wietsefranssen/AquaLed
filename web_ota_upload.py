Import("env")
import sys
import os
import urllib.request


def upload_via_web(source, target, env):
    firmware_path = str(source[0])
    if not os.path.isfile(firmware_path):
        print(f"[web-ota] Firmware niet gevonden: {firmware_path}")
        sys.exit(1)

    ip = env.GetProjectOption("upload_port")
    url = f"http://{ip}/api/ota/upload"
    print(f"[web-ota] Uploaden naar {url} ...")

    boundary = "----PlatformIOWebOTA"
    with open(firmware_path, "rb") as f:
        firmware_data = f.read()

    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="firmware"; filename="firmware.bin"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode() + firmware_data + f"\r\n--{boundary}--\r\n".encode()

    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    req.add_header("Content-Length", str(len(body)))

    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            result = resp.read().decode()
            print(f"[web-ota] Succes: {result}")
    except Exception as e:
        print(f"[web-ota] Upload mislukt: {e}")
        sys.exit(1)


env.Replace(UPLOADCMD=upload_via_web)
