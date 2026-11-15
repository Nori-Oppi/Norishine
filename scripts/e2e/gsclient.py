"""Minimal GameStream client for end-to-end tests against a Norishine dev host.

Implements pairing (PIN approved through the web UI API with a pairing ID), and the
HTTPS calls needed to test permissions and virtual displays. Test use only.
"""
import datetime
import hashlib
import http.cookiejar
import json
import os
import secrets
import ssl
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding, rsa
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.x509.oid import NameOID


def _ecb(key, data, encrypt):
    c = Cipher(algorithms.AES(key), modes.ECB())
    op = c.encryptor() if encrypt else c.decryptor()
    return op.update(data) + op.finalize()


def _insecure_ctx():
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    return ctx


class WebUI:
    """Admin web UI API (cookie session)."""

    def __init__(self, host, web_port, user, password):
        self.base = 'https://%s:%d' % (host, web_port)
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(
            urllib.request.HTTPSHandler(context=_insecure_ctx()),
            urllib.request.HTTPCookieProcessor(self.jar))
        r = self.call('POST', '/api/login', {'username': user, 'password': password}, raw=True)
        if not any(c.name == 'auth' for c in self.jar):
            raise RuntimeError('web UI login failed: %r' % r)

    def call(self, method, path, body=None, raw=False):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request(self.base + path, data=data, method=method)
        if data is not None:
            req.add_header('Content-Type', 'application/json')
        try:
            with self.opener.open(req, timeout=15) as resp:
                text = resp.read().decode()
                status = resp.status
        except urllib.error.HTTPError as e:
            text = e.read().decode()
            status = e.code
        if raw:
            return status, text
        try:
            return status, json.loads(text)
        except ValueError:
            return status, text


class Client:
    def __init__(self, host, port, name, unique_id=None):
        self.host = host
        self.http_port = port
        self.https_port = port - 5
        self.name = name
        self.unique_id = unique_id or secrets.token_hex(8).upper()
        self.key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'NVIDIA GameStream Client')])
        now = datetime.datetime.now(datetime.timezone.utc)
        self.cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
                     .public_key(self.key.public_key()).serial_number(x509.random_serial_number())
                     .not_valid_before(now - datetime.timedelta(days=1))
                     .not_valid_after(now + datetime.timedelta(days=3650))
                     .sign(self.key, hashes.SHA256()))
        self.cert_pem = self.cert.public_bytes(serialization.Encoding.PEM)
        d = tempfile.mkdtemp(prefix='gsclient-')
        self.cert_file = os.path.join(d, 'cert.pem')
        self.key_file = os.path.join(d, 'key.pem')
        open(self.cert_file, 'wb').write(self.cert_pem)
        open(self.key_file, 'wb').write(self.key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))

    # --- transport
    def _get(self, https, path, params, timeout=30):
        q = {'uniqueid': self.unique_id, 'uuid': secrets.token_hex(16)}
        q.update(params)
        url = '%s://%s:%d/%s?%s' % ('https' if https else 'http', self.host,
                                    self.https_port if https else self.http_port, path, urllib.parse.urlencode(q))
        handlers = []
        if https:
            ctx = _insecure_ctx()
            ctx.load_cert_chain(self.cert_file, self.key_file)
            handlers.append(urllib.request.HTTPSHandler(context=ctx))
        opener = urllib.request.build_opener(*handlers)
        try:
            with opener.open(url, timeout=timeout) as resp:
                body, code = resp.read(), resp.status
        except urllib.error.HTTPError as e:
            body, code = e.read(), e.code
        try:
            return ET.fromstring(body)
        except ET.ParseError:
            el = ET.Element('root', status_code=str(code), http_only='1')
            el.text = body.decode(errors='replace')
            return el

    def http(self, path, **params):
        return self._get(False, path, params)

    def https(self, path, timeout=30, **params):
        return self._get(True, path, params, timeout)

    # --- pairing
    def start_pair(self, pin):
        """Send getservercert; it blocks until the PIN is entered. Returns a thread + result holder."""
        self.salt = secrets.token_bytes(16)
        self.aes = hashlib.sha256(self.salt + pin.encode()).digest()[:16]
        result = {}

        def run():
            try:
                result['root'] = self._get(False, 'pair', {
                    'devicename': self.name, 'updateState': 1, 'phrase': 'getservercert',
                    'salt': self.salt.hex(), 'clientcert': self.cert_pem.hex()}, timeout=120)
            except Exception as e:  # noqa: BLE001
                result['error'] = e

        t = threading.Thread(target=run, daemon=True)
        t.start()
        return t, result

    def finish_pair(self, root):
        if root.findtext('paired') != '1':
            return False, 'getservercert: %s' % ET.tostring(root)
        server_cert = x509.load_pem_x509_certificate(bytes.fromhex(root.findtext('plaincert')))

        client_challenge = secrets.token_bytes(16)
        r = self.http('pair', devicename=self.name, updateState=1,
                      clientchallenge=_ecb(self.aes, client_challenge, True).hex())
        if r.findtext('paired') != '1':
            return False, 'clientchallenge: %s' % ET.tostring(r)
        dec = _ecb(self.aes, bytes.fromhex(r.findtext('challengeresponse')), False)
        server_hash, server_challenge = dec[:32], dec[32:48]

        client_secret = secrets.token_bytes(16)
        h = hashlib.sha256(server_challenge + self.cert.signature + client_secret).digest()
        r = self.http('pair', devicename=self.name, updateState=1,
                      serverchallengeresp=_ecb(self.aes, h, True).hex())
        if r.findtext('paired') != '1':
            return False, 'serverchallengeresp: %s' % ET.tostring(r)
        ps = bytes.fromhex(r.findtext('pairingsecret'))
        server_secret, server_sig = ps[:16], ps[16:]
        server_cert.public_key().verify(server_sig, server_secret, padding.PKCS1v15(), hashes.SHA256())
        if hashlib.sha256(client_challenge + server_cert.signature + server_secret).digest() != server_hash:
            return False, 'server hash mismatch (wrong PIN?)'

        sig = self.key.sign(client_secret, padding.PKCS1v15(), hashes.SHA256())
        r = self.http('pair', devicename=self.name, updateState=1,
                      clientpairingsecret=(client_secret + sig).hex())
        if r.findtext('paired') != '1':
            return False, 'clientpairingsecret: %s' % ET.tostring(r)
        r = self.https('pair', devicename=self.name, updateState=1, phrase='pairchallenge')
        return r.findtext('paired') == '1', 'pairchallenge: %s' % ET.tostring(r)


def wait_for_pending(web, name, timeout=10):
    end = time.time() + timeout
    while time.time() < end:
        _, r = web.call('GET', '/api/pin')
        for p in (r.get('pairings') or []) if isinstance(r, dict) else []:
            if p['name'] == name:
                return p
        time.sleep(0.2)
    return None


def pair_via_webui(client, web, pin='4821', approve_name=''):
    t, res = client.start_pair(pin)
    pending = wait_for_pending(web, client.name)
    if not pending:
        return False, 'request never listed as pending'
    status, r = web.call('POST', '/api/pin', {'pairing_id': pending['id'], 'pin': pin, 'name': approve_name})
    t.join(30)
    if 'root' not in res:
        return False, 'getservercert failed: %r' % res.get('error')
    return client.finish_pair(res['root'])
