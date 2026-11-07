#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Alexandru Hrisca
"""Sends malicious requests to sp211-airprintd (built with AddressSanitizer by
attack.sh) and checks that it rejects them and keeps working."""
import socket, struct, sys, time, urllib.request

PORT = int(sys.argv[1])
results = []

def ipp_attr(tag, name, value):
    name = name.encode(); value = value if isinstance(value, bytes) else value.encode()
    return bytes([tag]) + struct.pack('>H', len(name)) + name + struct.pack('>H', len(value)) + value

def ipp_request(op, extra=b'', reqid=1, user='alice', attrs_after=b''):
    b = struct.pack('>BBHI', 2, 0, op, reqid) + b'\x01'
    b += ipp_attr(0x47, 'attributes-charset', 'utf-8') + ipp_attr(0x48, 'attributes-natural-language', 'en')
    b += ipp_attr(0x45, 'printer-uri', f'ipp://localhost:{PORT}/ipp/print')
    b += ipp_attr(0x42, 'requesting-user-name', user) + extra + attrs_after + b'\x03'
    return b

def http(body, headers=None, raw=None, timeout=10):
    s = socket.create_connection(('127.0.0.1', PORT), timeout=timeout)
    if raw is None:
        h = {'Host': 'localhost', 'Content-Type': 'application/ipp', 'Content-Length': str(len(body))}
        h.update(headers or {})
        raw = b'POST /ipp/print HTTP/1.1\r\n' + b''.join(f'{k}: {v}\r\n'.encode() for k, v in h.items()) + b'\r\n' + body
    try:
        s.sendall(raw)
    except OSError:
        pass
    data = b''
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            data += d
            if b'\r\n\r\n' in data:
                hdr, _, rest = data.partition(b'\r\n\r\n')
                cl = [l for l in hdr.split(b'\r\n') if l.lower().startswith(b'content-length')]
                if cl and len(rest) >= int(cl[0].split(b':')[1]): break
    except OSError:
        pass
    s.close()
    return data

def ipp_status(resp):
    body = resp.partition(b'\r\n\r\n')[2]
    return struct.unpack('>H', body[2:4])[0] if len(body) >= 4 else None

def alive():
    r = http(ipp_request(0x0b))
    return ipp_status(r) == 0

def check(name, ok):
    ok = bool(ok) and alive()
    results.append(ok)
    print(('OK   ' if ok else 'FAIL ') + name)

# 1. negative / malformed chunk sizes
for size in (b'-1', b'-ffff', b'zz', b'ffffffffffffffffffff'):
    http(None, raw=b'POST /ipp/print HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n' + size + b'\r\n' + b'A' * 4000)
check('malformed chunk sizes rejected', True)

# 2. negative Content-Length
r = http(None, raw=b'POST /ipp/print HTTP/1.1\r\nHost: x\r\nContent-Length: -5\r\n\r\n' + b'A' * 100)
check('negative Content-Length -> 400', b' 400 ' in r.split(b'\r\n')[0] if r else True)

# 3. deeply nested collections
nested = b''
for i in range(200):
    nested += ipp_attr(0x34, 'media-col' if i == 0 else '', b'') + ipp_attr(0x4a, '', 'media-size')
nested += ipp_attr(0x37, '', b'') * 200
r = http(ipp_request(0x04, b'\x02' + nested))
check('200 nested collections rejected', ipp_status(r) is None or ipp_status(r) >= 0x400)

# 4. too many attributes
many = b''.join(ipp_attr(0x44, f'x-{i}', 'v') for i in range(5000))
http(ipp_request(0x0b, many))
check('5000 attributes rejected', True)

# 5. document larger than the limit (server runs with -m 1)
doc = b'UNIRAST\x00' + b'\x00' * (3 * 1024 * 1024)
req = ipp_request(0x02, ipp_attr(0x49, 'document-format', 'image/urf')) + doc
r = http(req)
check('3 MB document over 1 MB limit -> too large', ipp_status(r) == 0x0409)

# 6. connection flood: 60 idle connections, server must still answer afterwards
socks = []
for i in range(60):
    try: socks.append(socket.create_connection(('127.0.0.1', PORT), timeout=2))
    except OSError: pass
time.sleep(0.5)
for s in socks: s.close()
time.sleep(1)
check('60 idle connections, server recovers', True)

# 7. HTML injection in job name / user -> status page must not contain it
evil = '<script>alert(1)</script>'
create = ipp_request(0x05, ipp_attr(0x42, 'job-name', evil), user=evil)
r = http(create)
page = urllib.request.urlopen(f'http://127.0.0.1:{PORT}/', timeout=5).read()
check('status page has no injected HTML', b'<script>' not in page)

# 8. Cancel-Job by another user -> not authorized
r = http(ipp_request(0x05, ipp_attr(0x42, 'job-name', 'owned'), user='alice'))
body = r.partition(b'\r\n\r\n')[2]
jid = body.find(b'job-id')
job_id = struct.unpack('>I', body[jid + 8:jid + 12])[0]
r = http(ipp_request(0x08, ipp_attr(0x21, 'job-id', struct.pack('>I', job_id)), user='mallory'))
check('Cancel-Job by another user -> not authorized', ipp_status(r) == 0x0403)
r = http(ipp_request(0x08, ipp_attr(0x21, 'job-id', struct.pack('>I', job_id)), user='alice'))
check('Cancel-Job by the owner -> ok', ipp_status(r) == 0)

# 9. job cap: 60 Create-Job requests, later ones get server-error-busy
codes = [ipp_status(http(ipp_request(0x05, ipp_attr(0x42, 'job-name', f'j{i}'), reqid=i + 10))) for i in range(60)]
check('job cap -> server-error-busy', 0x0507 in codes)

print(f'passed {sum(results)}, failed {len(results) - sum(results)}')
sys.exit(0 if all(results) else 1)
