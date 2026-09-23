#!/usr/bin/env python3
"""Проверява, че отговорите на главния модул са валиден JSON с очакваните полета."""
import json
import sys
from pathlib import Path

out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
fails = 0


def check(cond, msg):
    global fails
    if not cond:
        fails += 1
        print("FAIL:", msg)


docs = {}
for f in sorted(out.glob("*.json")):
    raw = f.read_bytes()
    try:
        docs[f.name] = json.loads(raw.decode("utf-8"))   # строг UTF-8 и строг JSON
    except Exception as e:                               # noqa: BLE001
        check(False, f"{f.name}: {e}")

d = docs["data_full.json"]
for key in ["online", "timeSince", "linkAlarm", "id", "temperature", "humidity", "hr202",
            "locked", "relays", "vcc", "a0", "freeHeap", "uptime", "resetReason", "dhtErrors",
            "success", "failed", "queue", "error", "protoWarn", "proto", "fwMaster", "fwSlave",
            "s", "m", "link", "ping"]:
    check(key in d, f"data_full: липсва {key}")
check(d["temperature"] == 22.9 and d["humidity"] == 34.0, "температура/влажност")
check(isinstance(d["relays"], list) and len(d["relays"]) == 4, "relays")
check(d["s"]["loopMax"] == 25593 and d["s"]["b1m"] == 5, "s.* полета")
check(d["ping"]["ok"] == 2 and d["ping"]["lost"] == 1 and d["ping"]["min"] is not None, "ping")
check(d["link"]["ackMin"] is not None, "ackMin")

e = docs["data_empty.json"]
check(e["online"] is False and e["temperature"] is None and e["link"]["ackAvg"] is None, "data_empty")
check(docs["data_nan.json"]["temperature"] is None and docs["data_nan.json"]["humidity"] is None, "NaN -> null")
check(docs["data_protowarn.json"]["protoWarn"] == 2, "protoWarn")
check(docs["data_locked.json"]["locked"] is True, "locked")
check(docs["config_escaped.json"]["ssid"] == 'My "Wi\\Fi"\x01\t', "екраниране на SSID")
check(docs["config.json"]["ip"] == "192.168.1.13" and docs["config.json"]["mac"] == "BC:FF:4D:1D:A5:A6", "config")
check("error" in docs["overflow.json"], "overflow")

print(f"JSON: {len(docs)} файла, {fails} грешки")
sys.exit(1 if fails else 0)
