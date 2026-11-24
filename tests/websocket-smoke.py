"""Run against an isolated OBS profile configured with a fresh test password."""
import base64
import hashlib
import json
import sys
import time
import uuid
from pathlib import Path

import websocket

config = json.loads(Path(sys.argv[1]).read_text())
port = config["server_port"]
password = config["server_password"]
for attempt in range(30):
    try:
        ws = websocket.create_connection(f"ws://127.0.0.1:{port}", timeout=3)
        break
    except OSError:
        time.sleep(1)
else:
    raise RuntimeError("isolated server unavailable")

def b64hash(s):
    return base64.b64encode(hashlib.sha256(s.encode()).digest()).decode()

hello = json.loads(ws.recv())
auth = hello["d"]["authentication"]
ws.send(json.dumps({"op": 1, "d": {"rpcVersion": 1, "authentication": b64hash(b64hash(password+auth["salt"])+auth["challenge"]), "eventSubscriptions": 0}}))
assert json.loads(ws.recv())["op"] == 2

def call(kind, data):
    rid = str(uuid.uuid4())
    ws.send(json.dumps({"op": 6, "d": {"requestType": kind, "requestId": rid, "requestData": data}}))
    while True:
        msg = json.loads(ws.recv())
        if msg["op"] == 7 and msg["d"]["requestId"] == rid:
            reply = msg["d"]
            assert reply["requestStatus"]["result"], reply["requestStatus"]
            return reply.get("responseData", {})

def vendor(kind, **data):
    return call("CallVendorRequest", {"vendorName": "streaming-media", "requestType": kind, "requestData": data})["responseData"]

def result(receipt):
    for _ in range(30):
        status = vendor("GetRequestStatus", sourceUuid=source, requestId=receipt)["status"]
        if status != "pending":
            return status
        time.sleep(0.1)
    raise RuntimeError("request did not complete")

sources = vendor("ListSources")["sources"]
assert len(sources) == 1, sources
source = sources[0]["sourceUuid"]
name = sources[0]["sourceName"]
assert not sources[0]["acceptRequests"]
url = "https://youtu.be/jNQXAC9IVRw"
assert vendor("SubmitVideo", sourceUuid=source, url=url)["status"] == "disabled"
assert vendor("SubmitVideo", sourceUuid="nonexistent", url=url)["status"] == "source_not_found"
call("SetInputSettings", {"inputName": name, "inputSettings": {"remote_requests": True}, "overlay": True})
assert vendor("SubmitVideo", sourceUuid=source, url="https://example.org/video")["status"] == "invalid_url"
receipt = vendor("SubmitVideo", sourceUuid=source, url=url)
assert receipt["status"] == "pending", receipt
assert result(receipt["requestId"]) == "queued"
dup = vendor("SubmitVideo", sourceUuid=source, url="https://www.youtube.com/watch?v=jNQXAC9IVRw&t=2")
assert result(dup["requestId"]) == "duplicate"
snapshot = vendor("GetQueue", sourceUuid=source)
assert snapshot["items"] == [{"url":"https://www.youtube.com/watch?v=jNQXAC9IVRw"}], snapshot
assert snapshot["currentUrl"] == "" and not snapshot["autoAdvance"]
call("SetInputSettings", {"inputName": name, "inputSettings": {"remote_requests": False}, "overlay": True})
assert vendor("SubmitVideo", sourceUuid=source, url="https://youtu.be/aqz-KE-bpKQ")["status"] == "disabled"
print("WEBSOCKET PASS: auth, discovery, opt-in, invalid URL, target validation, receipt, queue, duplicate, no autoplay, disable")
ws.close()
