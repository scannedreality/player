#!/usr/bin/env python3
from http.server import HTTPServer, SimpleHTTPRequestHandler, test
import os.path
import socket
import ssl
import sys

class CORSRequestHandler (SimpleHTTPRequestHandler):
    def end_headers (self):
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        SimpleHTTPRequestHandler.end_headers(self)

def _get_best_family(*address):
    infos = socket.getaddrinfo(
        *address,
        type=socket.SOCK_STREAM,
        flags=socket.AI_PASSIVE,
    )
    family, type, proto, canonname, sockaddr = next(iter(infos))
    return family, sockaddr

if __name__ == '__main__':
    # This is adapted from the function test() at:
    # https://github.com/python/cpython/blob/3.10/Lib/http/server.py
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000

    HTTPServer.address_family, addr = _get_best_family(None, port)
    CORSRequestHandler.protocol_version = "HTTP/1.0"

    with HTTPServer(addr, CORSRequestHandler) as httpd:
        # If the files key.pem and cert.pem exist, use HTTPS
        if os.path.isfile('key.pem') and os.path.isfile('cert.pem'):
            print("Using HTTPS (using cert.pem and key.pem)")
            context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH)
            context.load_cert_chain(certfile="cert.pem", keyfile="key.pem")
            httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
        else:
            print("Using HTTP")

        host, port = httpd.socket.getsockname()[:2]
        url_host = f'[{host}]' if ':' in host else host
        print(
            f"Serving on {host} port {port} "
            f"(http://{url_host}:{port}/) ..."
        )
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nKeyboard interrupt received, exiting.")
            sys.exit(0)
