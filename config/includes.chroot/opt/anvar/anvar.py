import os
import json
import base64
import time
import socket
import subprocess
from cryptography.fernet import Fernet
import requests

LICENSE_FILE = "/etc/anvar/license.dat"
SOCKET_PATH = "/var/run/anvar.sock"
BASE_URL = "https://lisence.freecp.biz.id"
KEY_PATH = "/etc/anvar/.key"

def generate_key():
    key = Fernet.generate_key()
    with open(KEY_PATH, "wb") as f:
        f.write(key)
    return key

def get_key():
    if not os.path.exists(KEY_PATH):
        return generate_key()
    with open(KEY_PATH, "rb") as f:
        return f.read()

def encrypt_data(data, key):
    f = Fernet(key)
    return f.encrypt(data.encode())

def decrypt_data(data, key):
    f = Fernet(key)
    return f.decrypt(data).decode()

def save_license(license_data):
    key = get_key()
    encrypted = encrypt_data(json.dumps(license_data), key)
    with open(LICENSE_FILE, "wb") as f:
        f.write(encrypted)

def load_license():
    key = get_key()
    if not os.path.exists(LICENSE_FILE):
        return None
    with open(LICENSE_FILE, "rb") as f:
        encrypted = f.read()
    return json.loads(decrypt_data(encrypted, key))

def is_expired(license_obj):
    return time.time() > license_obj["until"]

def activate_license(license_key):
    url = f"{BASE_URL}/{license_key}/activate"
    response = requests.get(url)
    if response.status_code == 200:
        data = response.json()
        save_license(data)
        return data
    else:
        return {"status": "failed", "message": "Activation failed."}

def check_license(license_key):
    local_license = load_license()
    if local_license and not is_expired(local_license):
        return local_license
    else:
        url = f"{BASE_URL}/{license_key}"
        response = requests.get(url)
        if response.status_code == 200:
            data = response.json()
            save_license(data)
            return data
        else:
            return None

def handle_socket_connection(sock):
    while True:
        data = sock.recv(1024).decode()
        if data.startswith("check"):
            license_key = data.split()[1]
            result = check_license(license_key)
            if result:
                sock.sendall(json.dumps(result).encode())
            else:
                sock.sendall(json.dumps({"status": "failed", "message": "License invalid or expired."}).encode())
        elif data.startswith("activate"):
            license_key = data.split()[1]
            result = activate_license(license_key)
            sock.sendall(json.dumps(result).encode())

def run_service():
    # Create the Unix socket
    if os.path.exists(SOCKET_PATH):
        os.remove(SOCKET_PATH)

    server_sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    server_sock.bind(SOCKET_PATH)
    server_sock.listen(5)

    # Change the socket file permissions to allow all users to write
    subprocess.run(['chmod', '777', SOCKET_PATH])

    while True:
        client_sock, _ = server_sock.accept()
        handle_socket_connection(client_sock)
        client_sock.close()

if __name__ == "__main__":
    run_service()

