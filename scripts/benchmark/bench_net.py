"""A relay between client and host that can drop video packets, for compare.py.

The host listens on all addresses at its usual ports. The client is given 127.0.0.2, where
the relay listens on every port; it forwards HTTP and HTTPS to the host through 127.0.0.3
and RTSP and the UDP streams to 127.0.0.1. The client takes the RTSP and stream ports from
the host but keeps using the address it launched on; in case it switches to the address the
host reports (127.0.0.3, where the host was reached), the relay listens for RTSP and the
streams there too. An address of the relay's own wins over the host's "any address", so
every packet of every stream goes through the relay, for every host in a comparison.
The host binds its UDP ports when a stream starts, and Windows may refuse that while another
socket holds an address on the same port. So the relay opens its UDP sockets only once the
host has bound the port, and closes them when the host lets it go (it watches the system's
UDP table every millisecond). The client resends its first packets until it gets an answer.
Only the host-to-client video packets can be dropped: `loss` drops each one with that
probability, `blackout(seconds)` drops all of them for a while.

Ports relative to the host's base port (47989 by default), the same in Sunshine, Apollo,
Vibepollo and Norishine: TCP -5 (HTTPS), 0 (HTTP), +21 (RTSP); UDP +9 (video), +10 (control),
+11 (audio).
"""
import ctypes
import random
import socket
import struct
import threading
import time

RELAY_ADDR = '127.0.0.2'    # what the client is given
STREAM_ADDR = '127.0.0.3'   # what the host tells the client to use for RTSP and the streams
HOST_ADDR = '127.0.0.1'
# (port offset, address the relay listens on, address it forwards to)
TCP_ROUTES = ((-5, RELAY_ADDR, STREAM_ADDR), (0, RELAY_ADDR, STREAM_ADDR),
              (21, RELAY_ADDR, HOST_ADDR), (21, STREAM_ADDR, HOST_ADDR))
UDP_OFFSETS = (9, 10, 11)
VIDEO_OFFSET = 9
OWN_ADDRS = (RELAY_ADDR, STREAM_ADDR)


def host_udp_ports():
    """UDP ports bound by any socket not on one of the relay's own addresses."""
    iphlpapi = ctypes.WinDLL('iphlpapi')
    ports = set()
    for family, row in ((socket.AF_INET, 8), (socket.AF_INET6, 24)):
        size = ctypes.c_ulong(0)
        iphlpapi.GetExtendedUdpTable(None, ctypes.byref(size), False, family, 0, 0)
        buf = ctypes.create_string_buffer(size.value + 4096)
        size = ctypes.c_ulong(len(buf))
        if iphlpapi.GetExtendedUdpTable(buf, ctypes.byref(size), False, family, 0, 0) != 0:
            continue
        raw = buf.raw
        count = struct.unpack_from('<I', raw, 0)[0]
        for i in range(count):
            off = 4 + i * row
            if family == socket.AF_INET:
                addr = socket.inet_ntoa(raw[off:off + 4])
                port = struct.unpack_from('>H', raw, off + 4)[0]
            else:
                addr = None
                port = struct.unpack_from('>H', raw, off + 20)[0]
            if addr not in OWN_ADDRS:
                ports.add(port)
    return ports


class Relay:
    def __init__(self, base_port):
        self.base = base_port
        self.loss = 0.0
        self.blackout_until = 0.0
        self.video_packets = 0
        self.video_dropped = 0
        self._rng = random.Random(4321)
        self._socks = []
        self.counts = {}
        self._stop = threading.Event()

    def blackout(self, seconds):
        self.blackout_until = time.perf_counter() + seconds

    def start(self):
        for off, listen, target in TCP_ROUTES:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.bind((listen, self.base + off))
            s.listen(16)
            self._socks.append(s)
            threading.Thread(target=self._accept, args=(s, target, self.base + off), daemon=True).start()
        self._udp = {}  # port offset -> the relay's sockets for it
        threading.Thread(target=self._follow_host, daemon=True).start()
        return self

    def stop(self):
        self._stop.set()
        for s in self._socks:
            try:
                s.close()
            except OSError:
                pass

    # UDP sockets exist only while the host has the port bound
    def _follow_host(self):
        while not self._stop.is_set():
            try:
                bound = host_udp_ports()
            except OSError:
                bound = set()
            for off in UDP_OFFSETS:
                port = self.base + off
                if port in bound and off not in self._udp:
                    socks = []
                    for listen in OWN_ADDRS:
                        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
                        s.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 << 20)
                        try:
                            s.bind((listen, port))
                        except OSError:
                            s.close()
                            continue
                        socks.append(s)
                        threading.Thread(target=self._udp_in, args=(s, port, off == VIDEO_OFFSET), daemon=True).start()
                    self._udp[off] = socks
                elif port not in bound and off in self._udp:
                    for s in self._udp.pop(off):
                        s.close()
            time.sleep(0.001)
        for socks in self._udp.values():
            for s in socks:
                s.close()

    # TCP: one pair of pumps per connection
    def _accept(self, listener, target, port):
        while not self._stop.is_set():
            try:
                c, _ = listener.accept()
            except OSError:
                return
            self.counts['tcp%d' % port] = self.counts.get('tcp%d' % port, 0) + 1
            try:
                u = socket.create_connection((target, port), timeout=10)
                u.settimeout(None)
            except OSError:
                c.close()
                continue
            for a, b in ((c, u), (u, c)):
                threading.Thread(target=self._pump, args=(a, b), daemon=True).start()

    @staticmethod
    def _pump(src, dst):
        try:
            while True:
                data = src.recv(65536)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        for s in (src, dst):
            try:
                s.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass

    # UDP: one upstream socket per client address
    def _udp_in(self, sock, port, video):
        upstream = {}
        while not self._stop.is_set():
            try:
                data, addr = sock.recvfrom(65536)
            except OSError:
                if sock.fileno() == -1:  # closed: the host let the port go
                    for u in upstream.values():
                        u.close()
                    return
                continue  # Windows reports ICMP port unreachable as a recv error
            self.counts[port] = self.counts.get(port, 0) + 1
            u = upstream.get(addr)
            if u is None:
                u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                u.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 8 << 20)
                u.bind((HOST_ADDR, 0))
                u.connect((HOST_ADDR, port))
                upstream[addr] = u
                threading.Thread(target=self._udp_back, args=(u, sock, addr, video), daemon=True).start()
            try:
                u.send(data)
            except OSError:
                pass

    def _udp_back(self, u, sock, addr, video):
        while not self._stop.is_set():
            try:
                data = u.recv(65536)
            except OSError:
                if u.fileno() == -1 or sock.fileno() == -1:
                    return
                continue
            if video:
                self.video_packets += 1
                if time.perf_counter() < self.blackout_until or (self.loss and self._rng.random() < self.loss):
                    self.video_dropped += 1
                    continue
            try:
                sock.sendto(data, addr)
            except OSError:
                pass
