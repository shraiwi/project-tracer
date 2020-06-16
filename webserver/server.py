# a dead-simple server implementation
# this should NOT be used for production

from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import *
from Crypto.Cipher import AES
from Crypto.Util import Counter
from enum import Enum
import secrets
import operator
import urllib.parse
import threading
import random
import shutil
import base64
import time
import csv

class settings():
    caseid_len = 7          # how many characters long a tek is
    caseid_purge_age = 14   # how long caseids last
    tek_life = 14           # how long a tek lasts (how many the server should expect)
    packed_tek_len = 20     # how long a packed tek is in bytes (4 bytes epoch, 16 bytes tek)

# gets the unix epoch time
def get_epoch() -> int:
    return int(time.time())

# gets an enin given an epoch
def derive_enin(epoch : int):
    return epoch // (60 * 10)

# splits a datapair into an epoch and tek string. can be used for storage.
def unpack_tek(tek : bytes) -> Tuple[int, str]:
    return derive_enin(int.from_bytes(tek[:4], "little")), base64.b64encode(tek[4:settings.packed_tek_len]).decode("utf-8")

# turns a tek into its binary representation
def pack_tek(epoch : int, tek : str) -> bytes:
    return epoch.to_bytes(4, "little") + base64.b64decode(tek)

# appends an iterable of teks to the tekfile.
def commit_teks(teks : Iterable[Tuple[int, str]]):
    global tek_file_path
    with open(tek_file_path, "a") as tek_file:
        tek_file.writelines(map("%d,%s\n".__mod__, teks))

# generates random bytes given a random number
def random_bytes(num : int) -> bytes:
    return bytes(random.getrandbits(8) for _ in range(num))

class CaseIDType(Enum):
    NOT_FOUND = 0
    VALID = 1
    TOO_OLD = 2

# returns a set of all caseids
def get_caseids() -> Set[Tuple[int, str]]:
    return set(iter_caseids())

# removes caseids that aren't in the active caseid array
def commit_caseids(caseids : Set[Tuple[int, str]]):
    global caseid_file_path
    with open(caseid_file_path, "w") as caseid_file:
        caseid_file.writelines(map("%d,%s\n".__mod__, caseids))

# iterates over the caseid file
def iter_caseids() -> Iterable[Tuple[int, str]]:
    global caseid_file_path
    caseid_file = open(caseid_file_path, "r")
    for line in caseid_file:
        row = line.split(",")
        yield (int(row[0]), row[1].rstrip())

# validates a caseid against a caseid array. if the caseid is valid, it will return a tuple containing the matching caseid.
def burn_caseid(test_caseid: str, caseid_array: Set[Tuple[int, str]]) -> Tuple[CaseIDType, Tuple[int, str]]:
    min_age = get_epoch() - settings.caseid_purge_age * 24 * 60 * 60
    for epoch, caseid in caseid_array:
        if caseid.casefold() == test_caseid.casefold():
            if epoch > min_age: 
                return CaseIDType.VALID, (epoch, caseid)
            else: 
                return CaseIDType.TOO_OLD, (epoch, caseid)
    return CaseIDType.NOT_FOUND, None

# randomly generates a 7-character case id and adds it to the caseid array
def gen_caseid(epoch: int, caseid_array: Set[Tuple[int, str]]) -> str:
    data = base64.b32encode(secrets.token_bytes(4)).decode("utf-8")[:7]
    caseid_array.add((epoch, data))
    return data

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
        global tek_file_path
        self.send_headers()
        query = self.get_query({ 
            "oldest" : [0] 
        })

        oldest_age = int(query["oldest"][0])

        with open(tek_file_path, "r") as tek_file:
            tek_reader = csv.reader(tek_file)
            for row in tek_reader:
                epoch = int(row[0])
                if epoch >= oldest_age:
                    self.wfile.write(pack_tek(epoch, row[1]))

    def do_POST(self):
        global active_caseid_array, pending_teks

        self.send_headers()
        content_len = int(self.headers["Content-Length"])
        content_len -= settings.caseid_len
        if content_len // settings.packed_tek_len == settings.tek_life:
            caseid = self.rfile.read(settings.caseid_len).decode("utf-8")
            ret, match_caseid = burn_caseid(caseid, active_caseid_array)
            if ret == CaseIDType.VALID:
                active_caseid_array.remove(match_caseid)
                for i in range(settings.tek_life):
                    chunk = self.rfile.read(settings.packed_tek_len)
                    if not chunk: break
                    tek = unpack_tek(chunk)
                    if tek[0]: pending_teks.append(tek)
                self.wfile.write(b"ok")
                return
            elif burn_caseid == CaseIDType.TOO_OLD:
                self.wfile.write(b"expired")
                return
        
        self.wfile.write(b"invalid")

    def log_message(self, format, *args):
        return

tek_file_path = "tekfile.csv"
caseid_file_path = "caseid.csv"

active_caseid_array = get_caseids()
pending_teks = []

http_server = HTTPServer(("", 80), TracerServerHandler)

server_thread = threading.Thread(target=http_server.serve_forever, name="tracer webserver")
server_thread.setDaemon(True)
server_thread.start()

def shutdown(cmd):
    global server_thread
    server_thread._stop()
    raise SystemExit

def reload_caseid_array() -> int:
    global active_caseid_array
    active_caseid_array = get_caseids()
    return len(active_caseid_array)

def commit_pending_teks(cmd):
    global pending_teks
    commit_teks(pending_teks)
    out = "commited %d teks" % len(pending_teks)
    pending_teks.clear()
    return out

command_list = {
    "gen_caseid":           lambda cmd: "\n".join("%d: generated key %s" % (i, gen_caseid(get_epoch(), active_caseid_array)) for i in range(int(cmd[1]) if cmd[1:] else 1)),
    "list_caseid":          lambda cmd: "\n".join(map("epoch: %d\tcaseid: %s".__mod__, active_caseid_array)),
    "get_caseid":           lambda cmd: reload_caseid_array(),
    "commit_caseid":        lambda cmd: commit_caseids(active_caseid_array),
    "commit_teks":          commit_pending_teks,
    "list_teks":            lambda cmd: "\n".join(map("epoch: %d\ttek: %s".__mod__, pending_teks)),
    "help":                 lambda cmd: "available commands:\n\t"+"\n\t".join(command_list.keys()),
    "exit":                 shutdown
}

while True:
    userin = input("> ")
    parsedcmd = userin.split()
    command = parsedcmd[0] if parsedcmd else "help"
    if command in command_list:
        print(command_list[command](parsedcmd))
    else: print("unknown command!")
