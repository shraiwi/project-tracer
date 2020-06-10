# a dead-simple server implementation
# this should NOT be used for production

from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import *
import urllib.parse
import random
import shutil
import base64
import csv

tek_file_path = "tekfile.csv"

# gets an enin given an epoch
def derive_enin(epoch : int):
    return epoch // (60 * 10)

# splits a datapair into an epoch and tek string. can be used for storage.
def unpack_tek(tek : bytes) -> Tuple[int, str]:
    return derive_enin(int.from_bytes(tek[:4], "little")), base64.b64encode(tek[4:20]).decode("utf-8")

# turns a tek into its binary representation
def pack_tek(epoch : int, tek : str) -> bytes:
    return epoch.to_bytes(4, "little") + base64.b64decode(tek)

# generates random bytes along with a random epoch
def random_bytes(num : int) -> bytes:
    return bytes(random.getrandbits(8) for _ in range(num))

with open("randomtek.bin", "wb+") as randtek:
    randtek.write(random_bytes(20*14))


class TracerServerHandler(BaseHTTPRequestHandler):

    # sends a response code and a dictionary of headers
    def send_headers(self, code : int = 200, headers : dict = { "Content-Type" : "text/html" }) -> None:
        self.send_response(code)
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()

    # gets the query string as a dictionary
    def get_query(self, default : dict = {}) -> dict:
        return dict([*default.items()] + [*urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query, False).items()])
        

    def do_GET(self):
        self.send_headers()
        query = self.get_query({ 
            "oldest" : [0] 
        })

        oldest_age = int(query["oldest"][0])

        with open(tek_file_path, "r") as tek_file:
            tek_reader = csv.reader(tek_file)
            for row in tek_reader:
                epoch = int(row[0])
                #print(row, query)
                if epoch > oldest_age:
                    self.wfile.write(pack_tek(epoch, row[1]))
            
            #shutil.copyfileobj(tek_file, self.wfile)

    def do_POST(self):
        self.send_headers()
        content_len = int(self.headers["Content-Length"])
        if content_len % 20 == 0:
            with open(tek_file_path, "w") as tek_file:
                tek_writer = csv.writer(tek_file)
                for i in range(content_len // 20):
                    chunk = self.rfile.read(20)
                    if not chunk: break
                    tek_pair = unpack_tek(chunk)
                    tek_writer.writerow(tek_pair)
            self.wfile.write(b"ok")
        else:
            self.wfile.write(b"fail")
        

            
            
            #try:
            #    decoded_tek = decode_tek()
            #tek_writer.writerows()


http_server = HTTPServer(("", 80), TracerServerHandler)
http_server.serve_forever()
