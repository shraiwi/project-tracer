# A dead-simple keyserver implementation in python.

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

def get_epoch() -> int:
    """gets the unix epoch time"""
    return int(time.time())

def derive_enin(epoch : int):
    """gets an enin given an epoch"""
    return epoch // (60 * 10)

def unpack_tek(tek : bytes) -> Tuple[int, str]:
    """splits a datapair into an epoch and tek string. can be used for storage."""
    return derive_enin(int.from_bytes(tek[:4], "little")), base64.b64encode(tek[4:settings.packed_tek_len]).decode("utf-8")

def pack_tek(epoch : int, tek : str) -> bytes:
    """turns a tek tuple into its binary representation"""
    return epoch.to_bytes(4, "little") + base64.b64decode(tek)

def commit_teks(teks : Iterable[Tuple[int, str]]):
    """appends an iterable of teks to the tekfile."""
    global tek_file_path
    with open(tek_file_path, "a") as tek_file:
        tek_file.writelines(map("%d,%s\n".__mod__, teks))

def random_bytes(num : int) -> bytes:
    """generates random bytes of length num"""
    return bytes(random.getrandbits(8) for _ in range(num))

class CaseIDType(Enum):
    NOT_FOUND = 0
    VALID = 1
    TOO_OLD = 2

def get_caseids() -> Set[Tuple[int, str]]:
    """returns a set of all caseids"""
    return set(iter_caseids())

def commit_caseids(caseids : Set[Tuple[int, str]]):
    """updates caseids"""
    global caseid_file_path
    with open(caseid_file_path, "w") as caseid_file:
        caseid_file.writelines(map("%d,%s\n".__mod__, caseids))

def iter_caseids() -> Iterable[Tuple[int, str]]:
    """iterates over the caseid file"""
    global caseid_file_path
    caseid_file = open(caseid_file_path, "r")
    for line in caseid_file:
        row = line.split(",")
        yield (int(row[0]), row[1].rstrip())

def burn_caseid(test_caseid: str, caseid_array: Set[Tuple[int, str]]) -> Tuple[CaseIDType, Tuple[int, str]]:
    """validates a caseid against a caseid array. if the caseid is valid, it will return a tuple containing the matching caseid."""
    min_age = get_epoch() - settings.caseid_purge_age * 24 * 60 * 60
    for epoch, caseid in caseid_array:
        if caseid.casefold() == test_caseid.casefold():
            if epoch > min_age: 
                return CaseIDType.VALID, (epoch, caseid)
            else: 
                return CaseIDType.TOO_OLD, (epoch, caseid)
    return CaseIDType.NOT_FOUND, None

def gen_caseid(epoch: int, caseid_array: Set[Tuple[int, str]]) -> str:
    """randomly generates a 7-character case id and adds it to the caseid array"""
    data = base64.b32encode(secrets.token_bytes(4)).decode("utf-8")[:7]
    caseid_array.add((epoch, data))
    return data

class TracerServerHandler(BaseHTTPRequestHandler):
    def send_headers(self, code : int = 200, headers : dict = { "Content-Type" : "text/html" }) -> None:
        """sends a response code and a dictionary of headers"""
        self.send_response(code)
        for key, value in headers.items():
            self.send_header(key, value)
        self.end_headers()

    def get_query(self, default : dict = {}) -> dict:
        """gets the query string as a dictionary"""
        return dict([*default.items()] + [*urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query, False).items()])

    def do_GET(self):
        """returns all of the valid binary TEKs"""
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
        """accepts a body consisting of a CaseID and 14 binary TEKs and saves them to a pending TEK array if the CaseID is valid."""
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

def reload_changes():
    global pending_teks, active_caseid_array, sync_thread
    commit_teks(pending_teks)
    pending_teks.clear()
    commit_caseids(active_caseid_array)
    sync_thread = threading.Timer(sync_thread.interval, sync_thread.function)
    sync_thread.setDaemon(True)
    sync_thread.start()

sync_thread = threading.Timer(5.0, reload_changes)
sync_thread.setDaemon(True)
sync_thread.start()

def shutdown(cmd):
    global server_thread, sync_thread
    server_thread._stop()
    sync_thread.cancel()
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
