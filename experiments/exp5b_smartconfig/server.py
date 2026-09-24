#!/usr/bin/env python3
import socket

HOST = '0.0.0.0'   # 监听所有网卡
PORT = 8080

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind((HOST, PORT))
s.listen(5)

print(f"TCP 服务器启动，监听 {HOST}:{PORT} ...")

while True:
    conn, addr = s.accept()
    print(f"客户端连接: {addr}")
    while True:
        data = conn.recv(1024)
        if not data:
            break
        print(f"收到: {data.decode()}")
        conn.sendall(data)   # 回显
    print("客户端断开")
    conn.close()