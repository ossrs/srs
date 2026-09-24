#!/usr/bin/env python3
"""obsws.py - control OBS Studio through obs-websocket 5.x with no third-party packages.

The obs-websocket plugin ships inside OBS Studio 28+ (obs-websocket.plugin / .so / .dll),
so nothing needs installing; it only needs to be *enabled* in
<config>/plugin_config/obs-websocket/config.json, which OBS reads once at startup.

Subcommands (run with -h for options):
  probe             Find OBS, its config dir, websocket config, running state; test a connection.
  enable-websocket  Turn the websocket server on in config.json (takes effect at next OBS start).
  launch            Start OBS detached (optionally with --profile/--collection/--scene/--startstreaming)
                    and wait until the websocket accepts requests.
  quit              Stop outputs, ask OBS to quit gracefully, wait for the process to exit.
  call              Send one request:  obsws.py call GetVersion
                                        obsws.py call CreateScene '{"sceneName":"Test"}'
  batch             Send a RequestBatch from a JSON file (or '-' for stdin).
  events            Print events as JSON lines until --timeout.
  wait-stream       Poll GetStreamStatus until the stream is active/inactive.
  screenshot        Save a PNG of the program output (or a named source) to a file.
  instance          new | list | remove isolated instances (see below).

Connection options apply to every networked subcommand:
  --host (default 127.0.0.1)  --port  --password  --instance
Defaults come from the environment (OBS_WS_HOST, OBS_WS_PORT, OBS_WS_PASSWORD) and then from
the websocket config.json, so on the local machine no options are needed.

Isolated instances (parallel tests): `--instance ID` (or OBS_INSTANCE=ID) runs a separate OBS process
with its own config dir under $OBS_CONTROL_HOME (default ~/.cache/obs-control)/instances/ID, its own
websocket port and password, and its own pid file, so several tests can drive several OBS at once
without sharing profiles, scene collections, sources, sentinels or logs:
  ID=$(obsws.py instance new)                 (claims a unique id such as t3f9a2)
  obsws.py launch --instance $ID --wait 90 && obsws.py call GetVersion --instance $ID
  obsws.py instance list
  obsws.py instance remove --instance $ID     (quit it and delete its config dir; --all for every one)
macOS and Linux only (OBS reads its config dir from CFFIXED_USER_HOME / XDG_CONFIG_HOME).

Exit codes: 0 ok, 1 request refused by OBS, 2 cannot connect / protocol error,
3 authentication failed, 4 OBS not found / bad local state, 5 timeout.
"""

import argparse
import base64
import glob
import hashlib
import json
import os
import platform
import re
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import time
import uuid

WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
RPC_VERSION = 1

# EventSubscription bitmask (protocol.md "EventSubscription").
SUB_NONE = 0
SUB_ALL = 0x0FFF  # General|Config|Scenes|Inputs|Transitions|Filters|Outputs|SceneItems|MediaInputs|Vendors|Ui|Canvases
SUB_OUTPUTS = 1 << 6

CLOSE_CODES = {
    4002: "MessageDecodeError", 4003: "MissingDataField", 4004: "InvalidDataFieldType",
    4005: "InvalidDataFieldValue", 4006: "UnknownOpCode", 4007: "NotIdentified",
    4008: "AlreadyIdentified", 4009: "AuthenticationFailed", 4010: "UnsupportedRpcVersion",
    4011: "SessionInvalidated", 4012: "UnsupportedFeature",
}


# --------------------------------------------------------------------------- errors
class OBSError(Exception):
    exit_code = 2


class ConnectError(OBSError):
    exit_code = 2


class AuthError(OBSError):
    exit_code = 3


class RequestFailed(OBSError):
    exit_code = 1

    def __init__(self, request_type, status):
        self.request_type = request_type
        self.status = status
        super().__init__("%s failed: code %s %s" % (request_type, status.get("code"), status.get("comment", "")))


class LocalStateError(OBSError):
    exit_code = 4


class TimeoutError_(OBSError):
    exit_code = 5


class WSClosed(ConnectError):
    def __init__(self, code, reason):
        self.code = code
        self.reason = reason
        name = CLOSE_CODES.get(code, "")
        super().__init__("websocket closed by OBS: %s %s %s" % (code, name, reason))


# --------------------------------------------------------------------------- RFC 6455 client
def _mask(data, key):
    if not data:
        return data
    n = len(data)
    rep = (key * (n // 4 + 1))[:n]
    return (int.from_bytes(data, "big") ^ int.from_bytes(rep, "big")).to_bytes(n, "big")


class WebSocket:
    """Minimal text-frame websocket client (client frames masked, ping answered, close honoured)."""

    def __init__(self, host, port, timeout=10.0, subprotocol="obswebsocket.json"):
        try:
            self.sock = socket.create_connection((host, port), timeout=timeout)
        except OSError as e:
            raise ConnectError("cannot connect to ws://%s:%s (%s). Is OBS running with the websocket server enabled?" % (host, port, e))
        self.sock.settimeout(timeout)
        self.buf = b""
        key = base64.b64encode(os.urandom(16)).decode()
        req = ("GET / HTTP/1.1\r\nHost: %s:%s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
               "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: %s\r\n\r\n"
               % (host, port, key, subprotocol))
        self.sock.sendall(req.encode())
        head = self._read_until(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in lines[0]:
            raise ConnectError("websocket upgrade refused: %s" % lines[0])
        headers = {}
        for line in lines[1:]:
            if ":" in line:
                k, v = line.split(":", 1)
                headers[k.strip().lower()] = v.strip()
        accept = base64.b64encode(hashlib.sha1((key + WS_GUID).encode()).digest()).decode()
        if headers.get("sec-websocket-accept") != accept:
            raise ConnectError("bad Sec-WebSocket-Accept from server")

    def _read_until(self, marker):
        while marker not in self.buf:
            chunk = self._recv()
            self.buf += chunk
        head, self.buf = self.buf.split(marker, 1)
        return head

    def _recv(self):
        try:
            chunk = self.sock.recv(65536)
        except socket.timeout:
            raise TimeoutError_("timed out waiting for data from OBS")
        if not chunk:
            raise ConnectError("connection closed by OBS")
        return chunk

    def _recv_exact(self, n):
        while len(self.buf) < n:
            self.buf += self._recv()
        data, self.buf = self.buf[:n], self.buf[n:]
        return data

    def _read_frame(self):
        b1, b2 = self._recv_exact(2)
        fin = bool(b1 & 0x80)
        opcode = b1 & 0x0F
        masked = bool(b2 & 0x80)
        length = b2 & 0x7F
        if length == 126:
            length = struct.unpack("!H", self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv_exact(8))[0]
        key = self._recv_exact(4) if masked else None
        payload = self._recv_exact(length)
        if key:
            payload = _mask(payload, key)
        return fin, opcode, payload

    def send_frame(self, opcode, payload=b""):
        n = len(payload)
        header = bytes([0x80 | opcode])
        if n < 126:
            header += bytes([0x80 | n])
        elif n < 65536:
            header += bytes([0x80 | 126]) + struct.pack("!H", n)
        else:
            header += bytes([0x80 | 127]) + struct.pack("!Q", n)
        key = os.urandom(4)
        self.sock.sendall(header + key + _mask(payload, key))

    def send_text(self, text):
        self.send_frame(0x1, text.encode("utf-8"))

    def recv_text(self):
        message = b""
        while True:
            fin, opcode, payload = self._read_frame()
            if opcode == 0x9:  # ping
                self.send_frame(0xA, payload)
                continue
            if opcode == 0xA:  # pong
                continue
            if opcode == 0x8:  # close
                code = struct.unpack("!H", payload[:2])[0] if len(payload) >= 2 else 1005
                reason = payload[2:].decode("utf-8", "replace")
                try:
                    self.send_frame(0x8, payload[:2])
                except OSError:
                    pass
                raise WSClosed(code, reason)
            if opcode in (0x1, 0x2):
                message = payload
            elif opcode == 0x0:
                message += payload
            if fin:
                return message.decode("utf-8")

    def close(self):
        """Send a normal-closure frame and wait briefly for the peer's close frame, so the server
        logs code 1000 instead of an abnormal 1006 'End of File'."""
        try:
            self.send_frame(0x8, struct.pack("!H", 1000))
            self.sock.settimeout(1.0)
            for _ in range(20):
                _, opcode, _ = self._read_frame()
                if opcode == 0x8:
                    break
        except (OSError, OBSError):
            pass
        try:
            self.sock.close()
        except OSError:
            pass


# --------------------------------------------------------------------------- obs-websocket session
class OBSClient:
    """Identified obs-websocket session. Use as a context manager or call close()."""

    def __init__(self, host="127.0.0.1", port=4455, password=None, timeout=10.0, subscriptions=SUB_NONE):
        self.ws = WebSocket(host, port, timeout)
        self.pending_events = []
        hello = json.loads(self.ws.recv_text())
        if hello.get("op") != 0:
            raise ConnectError("expected Hello (op 0), got %r" % hello)
        self.hello = hello["d"]
        identify = {"rpcVersion": RPC_VERSION, "eventSubscriptions": subscriptions}
        auth = self.hello.get("authentication")
        if auth:
            if not password:
                raise AuthError("OBS requires a websocket password; pass --password or set OBS_WS_PASSWORD "
                                "(the value lives in plugin_config/obs-websocket/config.json as server_password)")
            secret = base64.b64encode(hashlib.sha256((password + auth["salt"]).encode()).digest()).decode()
            identify["authentication"] = base64.b64encode(
                hashlib.sha256((secret + auth["challenge"]).encode()).digest()).decode()
        self.ws.send_text(json.dumps({"op": 1, "d": identify}))
        try:
            msg = json.loads(self.ws.recv_text())
        except WSClosed as e:
            if e.code == 4009:
                raise AuthError("authentication failed: wrong websocket password")
            raise
        if msg.get("op") != 2:
            raise ConnectError("expected Identified (op 2), got %r" % msg)
        self.negotiated_rpc = msg["d"]["negotiatedRpcVersion"]

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        self.ws.close()

    def _wait_for(self, op, request_id):
        while True:
            msg = json.loads(self.ws.recv_text())
            if msg.get("op") == op and msg["d"].get("requestId") == request_id:
                return msg["d"]
            if msg.get("op") == 5:
                self.pending_events.append(msg["d"])

    def request(self, request_type, data=None):
        """Send one request and return the raw RequestResponse data (status + responseData)."""
        rid = str(uuid.uuid4())
        d = {"requestType": request_type, "requestId": rid}
        if data is not None:
            d["requestData"] = data
        self.ws.send_text(json.dumps({"op": 6, "d": d}))
        return self._wait_for(7, rid)

    def call(self, request_type, data=None):
        """Send one request; return responseData ({} if none) or raise RequestFailed."""
        resp = self.request(request_type, data)
        status = resp.get("requestStatus", {})
        if not status.get("result"):
            raise RequestFailed(request_type, status)
        return resp.get("responseData") or {}

    def try_call(self, request_type, data=None):
        """Like call() but returns None instead of raising when OBS refuses the request."""
        try:
            return self.call(request_type, data)
        except RequestFailed:
            return None

    def batch(self, requests, halt_on_failure=False, execution_type=0):
        rid = str(uuid.uuid4())
        d = {"requestId": rid, "haltOnFailure": halt_on_failure, "executionType": execution_type,
             "requests": requests}
        self.ws.send_text(json.dumps({"op": 8, "d": d}))
        return self._wait_for(9, rid)

    def next_event(self):
        """Return the next event data dict; raises TimeoutError_ when the socket timeout elapses."""
        if self.pending_events:
            return self.pending_events.pop(0)
        while True:
            msg = json.loads(self.ws.recv_text())
            if msg.get("op") == 5:
                return msg["d"]


# --------------------------------------------------------------------------- local OBS discovery
def system():
    return platform.system()


def config_dir():
    """The config dir of the OBS this process controls: the current instance's, else the main one."""
    if INSTANCE:
        return instance_config_dir(INSTANCE)
    return main_config_dir()


def main_config_dir():
    """The obs-studio config dir: ~/Library/Application Support/obs-studio, %APPDATA%\\obs-studio,
    ~/.config/obs-studio (or the flatpak location). Override with OBS_CONFIG_DIR."""
    env = os.environ.get("OBS_CONFIG_DIR")
    if env:
        return env
    s = system()
    if s == "Darwin":
        return os.path.expanduser("~/Library/Application Support/obs-studio")
    if s == "Windows":
        return os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "obs-studio")
    xdg = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    path = os.path.join(xdg, "obs-studio")
    flatpak = os.path.expanduser("~/.var/app/com.obsproject.Studio/config/obs-studio")
    if not os.path.isdir(path) and os.path.isdir(flatpak):
        return flatpak
    return path


def read_ini(path):
    """Tiny INI reader (OBS writes plain key=value under [Section], no escaping)."""
    result = {}
    if not os.path.isfile(path):
        return result
    section = None
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(("#", ";")):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
                result.setdefault(section, {})
            elif "=" in line and section is not None:
                k, v = line.split("=", 1)
                result[section][k.strip()] = v.strip()
    return result


def basic_dirs():
    """(profiles_dir, scenes_dir) honouring global.ini [Locations] overrides."""
    cfg = config_dir()
    locs = read_ini(os.path.join(cfg, "global.ini")).get("Locations", {})
    prof_base = locs.get("Profiles")
    scene_base = locs.get("SceneCollections")
    profiles = os.path.join(prof_base, "obs-studio", "basic", "profiles") if prof_base else os.path.join(cfg, "basic", "profiles")
    scenes = os.path.join(scene_base, "obs-studio", "basic", "scenes") if scene_base else os.path.join(cfg, "basic", "scenes")
    return profiles, scenes


def websocket_config_path(cfg_dir=None):
    return os.path.join(cfg_dir or config_dir(), "plugin_config", "obs-websocket", "config.json")


def read_websocket_config(cfg_dir=None):
    path = websocket_config_path(cfg_dir)
    defaults = {"first_load": True, "server_enabled": False, "server_port": 4455,
                "alerts_enabled": False, "auth_required": True, "server_password": ""}
    if os.path.isfile(path):
        try:
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            if isinstance(data, dict):
                defaults.update(data)
        except (OSError, ValueError) as e:
            print("warning: cannot parse %s: %s" % (path, e), file=sys.stderr)
    return defaults


def write_websocket_config(cfg, cfg_dir=None):
    path = websocket_config_path(cfg_dir)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=4)
        f.write("\n")
    return path


def find_obs_binary():
    """Return (path, cwd) for launching OBS, or (None, None)."""
    env = os.environ.get("OBS_BINARY")
    if env and os.path.exists(env):
        return env, None
    s = system()
    if s == "Darwin":
        for app in ("/Applications/OBS.app", os.path.expanduser("~/Applications/OBS.app")):
            exe = os.path.join(app, "Contents", "MacOS", "OBS")
            if os.path.exists(exe):
                return exe, None
        return None, None
    if s == "Windows":
        for base in (os.environ.get("ProgramFiles", r"C:\Program Files"), r"C:\Program Files"):
            exe = os.path.join(base, "obs-studio", "bin", "64bit", "obs64.exe")
            if os.path.exists(exe):
                return exe, os.path.dirname(exe)  # OBS on Windows must run with cwd = its bin dir
        return None, None
    exe = shutil.which("obs")
    if exe:
        return exe, None
    if shutil.which("flatpak"):
        try:
            out = subprocess.run(["flatpak", "list", "--app", "--columns=application"], capture_output=True, text=True, timeout=10).stdout
            if "com.obsproject.Studio" in out:
                return "flatpak run com.obsproject.Studio", None
        except (OSError, subprocess.SubprocessError):
            pass
    return None, None


def obs_version(binary):
    s = system()
    try:
        if s == "Darwin" and binary:
            import plistlib
            plist = os.path.join(os.path.dirname(os.path.dirname(binary)), "Info.plist")
            with open(plist, "rb") as f:
                return plistlib.load(f).get("CFBundleShortVersionString")
        if s == "Linux" and binary and not binary.startswith("flatpak"):
            out = subprocess.run([binary, "--version"], capture_output=True, text=True, timeout=15).stdout
            return out.strip().replace("OBS Studio - ", "") or None
    except (OSError, subprocess.SubprocessError, ValueError):
        return None
    return None


def websocket_plugin_present(binary):
    s = system()
    candidates = []
    if s == "Darwin" and binary:
        candidates.append(os.path.join(os.path.dirname(os.path.dirname(binary)), "PlugIns", "obs-websocket.plugin"))
    elif s == "Windows" and binary:
        candidates.append(os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(binary))), "obs-plugins", "64bit", "obs-websocket.dll"))
    else:
        candidates += glob.glob("/usr/lib*/obs-plugins/obs-websocket.so")
        candidates += glob.glob("/usr/lib/*/obs-plugins/obs-websocket.so")
        candidates += glob.glob("/usr/local/lib*/obs-plugins/obs-websocket.so")
        candidates += glob.glob("/var/lib/flatpak/app/com.obsproject.Studio/current/active/files/lib/obs-plugins/obs-websocket.so")
        candidates += glob.glob(os.path.expanduser("~/.local/share/flatpak/app/com.obsproject.Studio/current/active/files/lib/obs-plugins/obs-websocket.so"))
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def obs_pids():
    """PIDs of the OBS this process controls: the current instance's process, else every OBS process
    that is not an isolated instance (the user's own OBS)."""
    if INSTANCE:
        pid = instance_pid(INSTANCE)
        return [pid] if pid else []
    owned = set(instance_pids().values())
    return [p for p in all_obs_pids() if p not in owned]


def all_obs_pids():
    s = system()
    try:
        if s == "Windows":
            out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq obs64.exe", "/FO", "CSV", "/NH"], capture_output=True, text=True, timeout=10).stdout
            return [int(line.split(",")[1].strip('"')) for line in out.splitlines() if line.startswith('"obs64.exe"')]
        name = "OBS" if s == "Darwin" else "obs"
        out = subprocess.run(["pgrep", "-x", name], capture_output=True, text=True, timeout=10).stdout
        return [int(p) for p in out.split()]
    except (OSError, subprocess.SubprocessError, ValueError):
        return []


def stale_sentinels():
    """Crash sentinels left by an OBS that did not exit cleanly. If any exist when OBS is not running,
    the next launch shows a blocking 'unclean shutdown' dialog (frontend/utility/CrashHandler.cpp)."""
    return glob.glob(os.path.join(config_dir(), ".sentinel", "run_*"))


def latest_log():
    logs = glob.glob(os.path.join(config_dir(), "logs", "*.txt"))
    return max(logs, key=os.path.getmtime) if logs else None


def current_profile_and_collection():
    basic = read_ini(os.path.join(config_dir(), "user.ini")).get("Basic", {})
    if not basic:  # OBS < 31 kept these in global.ini
        basic = read_ini(os.path.join(config_dir(), "global.ini")).get("Basic", {})
    return basic.get("Profile"), basic.get("SceneCollection")


# --------------------------------------------------------------------------- isolated instances
# Several OBS processes on one machine would otherwise share one config dir: the same user.ini (current
# profile/collection), the same websocket port, the same profiles, scene collections, sentinels and
# logs. An instance gives one OBS a private home so OBS derives a private config dir from it
# (macOS: CFFIXED_USER_HOME feeds NSSearchPathForDirectoriesInDomains; Linux: XDG_CONFIG_HOME).
INSTANCE = None
INSTANCE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_-]{0,31}$")
FIRST_INSTANCE_PORT = 4460
LAST_INSTANCE_PORT = 5459


def set_instance(name):
    global INSTANCE
    if name:
        if not INSTANCE_RE.match(name):
            raise LocalStateError("instance id %r: use 1-32 letters, digits, '-' or '_', starting with a letter or digit" % name)
        if system() == "Windows":
            raise LocalStateError("isolated instances need OBS to take its config dir from the environment, which "
                                  "OBS on Windows does not; use one portable OBS copy per test (OBS_BINARY + --portable)")
    INSTANCE = name or None


def apply_instance_args(args):
    set_instance(getattr(args, "instance", None) or os.environ.get("OBS_INSTANCE"))


def instance_flag():
    """The CLI option to repeat in hints so a copied command talks to the same OBS."""
    return " --instance %s" % INSTANCE if INSTANCE else ""


def instances_root():
    return os.path.join(os.environ.get("OBS_CONTROL_HOME") or os.path.expanduser("~/.cache/obs-control"), "instances")


def instance_dir(name):
    return os.path.join(instances_root(), name)


def instance_home(name):
    return os.path.join(instance_dir(name), "home")


def instance_config_dir(name):
    home = instance_home(name)
    if system() == "Darwin":
        return os.path.join(home, "Library", "Application Support", "obs-studio")
    return os.path.join(home, ".config", "obs-studio")


def instance_env(name):
    env = dict(os.environ)
    if system() == "Darwin":
        env["CFFIXED_USER_HOME"] = instance_home(name)
    else:
        env["XDG_CONFIG_HOME"] = os.path.join(instance_home(name), ".config")
    return env


def new_instance():
    """Claim a fresh instance id: "t" plus 5 random hex digits. The claim is the atomic mkdir of its
    directory, so an id already taken on this machine is skipped and tests started at the same moment
    (or by different agents) never get the same id."""
    os.makedirs(instances_root(), exist_ok=True)
    while True:
        name = "t" + secrets.token_hex(3)[:5]
        try:
            os.mkdir(instance_dir(name))
            return name
        except FileExistsError:
            continue


def instance_names():
    root = instances_root()
    if not os.path.isdir(root):
        return []
    return sorted(n for n in os.listdir(root) if INSTANCE_RE.match(n) and os.path.isdir(os.path.join(root, n)))


def pid_is_obs(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        pass
    try:
        out = subprocess.run(["ps", "-p", str(pid), "-o", "comm="], capture_output=True, text=True, timeout=10).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return True
    return os.path.basename(out) in ("OBS", "obs")


def instance_pid(name):
    """PID of the instance's OBS if it is still running (a reused PID of another program does not count)."""
    try:
        with open(os.path.join(instance_dir(name), "obs.pid"), encoding="utf-8") as f:
            pid = int(f.read().strip())
    except (OSError, ValueError):
        return None
    return pid if pid_is_obs(pid) else None


def instance_pids():
    pids = {}
    for name in instance_names():
        pid = instance_pid(name)
        if pid:
            pids[name] = pid
    return pids


class instances_lock:
    """Serialises port allocation so instances launched at the same moment never pick the same port."""

    def __enter__(self):
        import fcntl
        os.makedirs(instances_root(), exist_ok=True)
        self.f = open(os.path.join(instances_root(), ".lock"), "a+")
        fcntl.flock(self.f, fcntl.LOCK_EX)
        return self

    def __exit__(self, *exc):
        import fcntl
        fcntl.flock(self.f, fcntl.LOCK_UN)
        self.f.close()


def port_free(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        s.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def write_ini(path, sections):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        for section, values in sections.items():
            f.write("[%s]\n" % section)
            for k, v in values.items():
                f.write("%s=%s\n" % (k, v))
            f.write("\n")


def seed_instance(name):
    """Create or refresh the instance's config so OBS starts without dialogs and with its own websocket
    server. Existing files are kept, so profiles and scene collections survive relaunches."""
    with instances_lock():
        cfg_dir = instance_config_dir(name)
        os.makedirs(cfg_dir, exist_ok=True)

        ws = read_websocket_config(cfg_dir)
        port = ws.get("server_port") if os.path.isfile(websocket_config_path(cfg_dir)) else None
        if port and not instance_pid(name) and not port_free(port):
            port = None  # something else took it since the last run
        if not port:
            reserved = {read_websocket_config(main_config_dir()).get("server_port") or 4455}
            for other in instance_names():
                if other != name and os.path.isfile(websocket_config_path(instance_config_dir(other))):
                    reserved.add(read_websocket_config(instance_config_dir(other)).get("server_port"))
            for candidate in range(FIRST_INSTANCE_PORT, LAST_INSTANCE_PORT + 1):
                if candidate not in reserved and port_free(candidate):
                    port = candidate
                    break
            if not port:
                raise LocalStateError("no free websocket port in %d-%d" % (FIRST_INSTANCE_PORT, LAST_INSTANCE_PORT))
        ws.update({"first_load": False, "server_enabled": True, "server_port": port, "alerts_enabled": False,
                   "auth_required": True})
        if not ws.get("server_password"):
            ws["server_password"] = base64.urlsafe_b64encode(os.urandom(12)).decode().rstrip("=")
        write_websocket_config(ws, cfg_dir)

        # global.ini: LastVersion (copied from the main OBS) suppresses the what's-new check and
        # MacOSPermissionsDialogLastShown the modal macOS permissions dialog.
        global_ini = os.path.join(cfg_dir, "global.ini")
        if not os.path.isfile(global_ini):
            main_general = read_ini(os.path.join(main_config_dir(), "global.ini")).get("General", {})
            general = {"EnableAutoUpdates": "false", "MacOSPermissionsDialogLastShown": main_general.get("MacOSPermissionsDialogLastShown", "1")}
            if main_general.get("LastVersion"):
                general["LastVersion"] = main_general["LastVersion"]
            write_ini(global_ini, {"General": general})
        # user.ini: FirstRun=true skips the auto-configuration wizard and the first-run desktop/mic
        # capture sources; ConfirmOnExit=false keeps quit from opening a dialog.
        user_ini = os.path.join(cfg_dir, "user.ini")
        if not os.path.isfile(user_ini):
            write_ini(user_ini, {"General": {"FirstRun": "true", "ConfirmOnExit": "false"}})
    return port


def resolve_connection(args):
    cfg = read_websocket_config()
    # An instance's port and password live in its own config.json; the OBS_WS_* variables describe the
    # main OBS and would point every instance at the same server.
    env = {} if INSTANCE else os.environ
    host = getattr(args, "host", None) or env.get("OBS_WS_HOST") or "127.0.0.1"
    port = getattr(args, "port", None) or env.get("OBS_WS_PORT") or cfg.get("server_port") or 4455
    password = getattr(args, "password", None) or env.get("OBS_WS_PASSWORD")
    if password is None and cfg.get("auth_required", True):
        password = cfg.get("server_password") or None
    return host, int(port), password


def connect(args, subscriptions=SUB_NONE, timeout=None):
    host, port, password = resolve_connection(args)
    return OBSClient(host, port, password, timeout=timeout or getattr(args, "timeout", 10.0), subscriptions=subscriptions)


def wait_for_websocket(args, deadline_s):
    """Retry the handshake until OBS answers requests (the server accepts TCP before OBS finished loading)."""
    end = time.time() + deadline_s
    last = None
    while time.time() < end:
        try:
            c = connect(args, timeout=5.0)
            try:
                ver = c.call("GetVersion")
                return ver
            finally:
                c.close()
        except AuthError:
            raise
        except OBSError as e:
            last = e
            time.sleep(1.0)
    raise TimeoutError_("OBS websocket did not become ready within %ss (%s)" % (deadline_s, last))


def dump(obj):
    print(json.dumps(obj, indent=2, ensure_ascii=False))


# --------------------------------------------------------------------------- subcommands
def cmd_probe(args):
    binary, _ = find_obs_binary()
    cfg = read_websocket_config()
    pids = obs_pids()
    profile, collection = current_profile_and_collection()
    profiles_dir, scenes_dir = basic_dirs()
    info = {
        "platform": system(),
        "instance": INSTANCE,
        "obs_binary": binary,
        "obs_version": obs_version(binary),
        "config_dir": config_dir(),
        "config_dir_exists": os.path.isdir(config_dir()),
        "profiles_dir": profiles_dir,
        "scenes_dir": scenes_dir,
        # on-disk view; while OBS runs, GetProfileList/GetSceneCollectionList are authoritative
        "profile_dirs_on_disk": sorted(os.listdir(profiles_dir)) if os.path.isdir(profiles_dir) else [],
        "scene_collection_files_on_disk": sorted(os.path.splitext(f)[0] for f in os.listdir(scenes_dir) if f.endswith(".json")) if os.path.isdir(scenes_dir) else [],
        "current_profile_on_disk": profile,            # user.ini view; the live handshake below is authoritative
        "current_scene_collection_on_disk": collection,
        "websocket_plugin": websocket_plugin_present(binary),
        "websocket_config": websocket_config_path(),
        "websocket_enabled": bool(cfg.get("server_enabled")),
        "websocket_port": cfg.get("server_port"),
        "websocket_auth_required": bool(cfg.get("auth_required", True)),
        "websocket_password": cfg.get("server_password") if args.show_password else ("<set>" if cfg.get("server_password") else ""),
        "obs_running": bool(pids),
        "obs_pids": pids,
        "stale_sentinels": stale_sentinels() if not pids else [],
        "latest_log": latest_log(),
    }
    if not INSTANCE:
        info["isolated_instances_running"] = instance_pids()
    live = {"connected": False}
    try:
        c = connect(args, timeout=3.0)
        try:
            v = c.call("GetVersion")
            live = {"connected": True, "obsVersion": v.get("obsVersion"), "obsWebSocketVersion": v.get("obsWebSocketVersion"),
                    "platform": v.get("platform"), "requestCount": len(v.get("availableRequests", []))}
            live["profiles"] = c.call("GetProfileList")
            live["sceneCollections"] = c.call("GetSceneCollectionList")
        finally:
            c.close()
    except OBSError as e:
        live = {"connected": False, "error": str(e), "error_kind": type(e).__name__}
    info["live"] = live
    if args.json:
        dump(info)
    else:
        for k, v in info.items():
            if k != "live":
                print("%-26s %s" % (k + ":", v))
        print("live websocket:            %s" % json.dumps(live))
        print()
        f = instance_flag()
        if not binary:
            print("NEXT: OBS Studio is not installed (or set OBS_BINARY). Install OBS 28+ - obs-websocket is bundled with it.")
        elif not info["websocket_plugin"] and not live["connected"]:
            print("NEXT: obs-websocket plugin file not found next to OBS; OBS < 28 needs the separate plugin release.")
        elif INSTANCE and not pids:
            print("NEXT: instance %s is not running; `obsws.py launch%s --wait 90` creates its config and starts it." % (INSTANCE, f))
        elif not info["websocket_enabled"] and not live["connected"]:
            print("NEXT: run `obsws.py enable-websocket%s` then restart OBS (`obsws.py quit%s` / `obsws.py launch%s`)." % (f, f, f))
        elif not pids:
            print("NEXT: OBS is not running; `obsws.py launch%s --wait 90`." % f)
        elif not live["connected"]:
            print("NEXT: OBS runs but the websocket did not answer: %s" % live.get("error"))
        else:
            print("READY: `obsws.py call GetVersion%s` works." % f)
    return 0


def cmd_enable_websocket(args):
    if INSTANCE:
        port = seed_instance(INSTANCE)
        print("instance %s: websocket enabled on port %s in %s (seeded again by every `launch`)" % (INSTANCE, port, websocket_config_path()))
        return 0
    cfg = read_websocket_config()
    cfg["first_load"] = False
    cfg["server_enabled"] = True
    if args.port:
        cfg["server_port"] = args.port
    if args.no_auth:
        cfg["auth_required"] = False
    else:
        cfg["auth_required"] = True
        if args.password:
            cfg["server_password"] = args.password
        elif not cfg.get("server_password"):
            cfg["server_password"] = base64.urlsafe_b64encode(os.urandom(12)).decode().rstrip("=")
    path = write_websocket_config(cfg)
    print("wrote %s" % path)
    print("server_enabled=true port=%s auth_required=%s" % (cfg["server_port"], cfg["auth_required"]))
    if args.show_password and cfg.get("auth_required"):
        print("password=%s" % cfg["server_password"])
    pids = obs_pids()
    if pids:
        print("OBS is running (pid %s): the file is read only at startup, so restart OBS for this to take effect "
              "(obsws.py quit && obsws.py launch). Saving the Tools > WebSocket Server Settings dialog would overwrite it."
              % pids)
        if args.restart:
            rc = cmd_quit(args)
            if rc:
                return rc
            return cmd_launch(args)
    return 0


def build_launch_args(args):
    extra = []
    if getattr(args, "profile", None):
        extra += ["--profile", args.profile]
    if getattr(args, "collection", None):
        extra += ["--collection", args.collection]
    if getattr(args, "scene", None):
        extra += ["--scene", args.scene]
    if getattr(args, "startstreaming", False):
        extra.append("--startstreaming")
    if getattr(args, "startrecording", False):
        extra.append("--startrecording")
    if getattr(args, "minimize_to_tray", False):
        extra.append("--minimize-to-tray")
    if getattr(args, "studio_mode", False):
        extra.append("--studio-mode")
    if getattr(args, "portable", False):
        extra.append("--portable")
    if getattr(args, "verbose", False):
        extra.append("--verbose")
    if not getattr(args, "allow_updater", False):
        extra.append("--disable-updater")
    if not getattr(args, "allow_missing_files_check", False):
        extra.append("--disable-missing-files-check")
    if getattr(args, "ws_port", None):
        extra += ["--websocket_port", str(args.ws_port)]
    if getattr(args, "ws_password", None):
        extra += ["--websocket_password", args.ws_password]
    if getattr(args, "ws_debug", False):
        extra.append("--websocket_debug")
    extra += getattr(args, "extra", []) or []
    return extra


def cmd_launch(args):
    pids = obs_pids()
    if pids and INSTANCE and not getattr(args, "reuse", False):
        # Another test may own this instance; attaching to it would make two tests drive one OBS.
        raise LocalStateError("instance %s is already running (pid %s). Claim your own id with `obsws.py instance new`, "
                              "or pass --reuse if this test launched it." % (INSTANCE, pids))
    if pids:
        print("OBS%s already running (pid %s); not launching another. Use `quit%s` first."
              % (" instance " + INSTANCE if INSTANCE else "", pids, instance_flag()))
        if getattr(args, "wait", 0):
            ver = wait_for_websocket(args, args.wait)
            print("websocket ready: OBS %s / obs-websocket %s" % (ver.get("obsVersion"), ver.get("obsWebSocketVersion")))
        return 0
    binary, cwd = find_obs_binary()
    if not binary:
        raise LocalStateError("OBS binary not found; install OBS Studio or set OBS_BINARY")
    if INSTANCE and binary.startswith("flatpak "):
        raise LocalStateError("isolated instances need a native OBS; flatpak sets its own XDG_CONFIG_HOME")
    stale = stale_sentinels()
    if stale and not getattr(args, "keep_sentinel", False):
        for s in stale:
            try:
                os.remove(s)
            except OSError:
                pass
        print("removed %d stale crash sentinel(s) so OBS will not block on the 'unclean shutdown' dialog "
              "(check %s for the crash)" % (len(stale), os.path.join(config_dir(), "logs")))
    env = None
    if INSTANCE:
        port = seed_instance(INSTANCE)
        env = instance_env(INSTANCE)
        print("instance %s: config %s, websocket port %s" % (INSTANCE, config_dir(), port))
    extra = build_launch_args(args)
    # Any other OBS process (the user's or another instance) makes OBS show a blocking "already running"
    # dialog; --multi skips it. Each instance has its own config dir, so they do not share state.
    if (getattr(args, "multi", False) or INSTANCE or all_obs_pids()) and "--multi" not in extra:
        extra.append("--multi")
    cfg = read_websocket_config()
    if not cfg.get("server_enabled") and not getattr(args, "ws_port", None):
        print("warning: websocket server is disabled in %s; requests will not work after launch "
              "(run `obsws.py enable-websocket` first)" % websocket_config_path())
    argv = (binary.split(" ") if binary.startswith("flatpak ") else [binary]) + extra
    kwargs = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL, "cwd": cwd, "env": env}
    if system() == "Windows":
        kwargs["creationflags"] = getattr(subprocess, "DETACHED_PROCESS", 0) | getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0)
    else:
        kwargs["start_new_session"] = True
    proc = subprocess.Popen(argv, **kwargs)
    if INSTANCE:
        with open(os.path.join(instance_dir(INSTANCE), "obs.pid"), "w", encoding="utf-8") as f:
            f.write("%d\n" % proc.pid)
    print("launched: %s (pid %s)" % (" ".join(argv), proc.pid))
    if getattr(args, "wait", 0):
        time.sleep(1.0)
        if proc.poll() is not None:
            raise LocalStateError("OBS exited immediately with code %s; see %s" % (proc.returncode, latest_log()))
        ver = wait_for_websocket(args, args.wait)
        print("websocket ready: OBS %s / obs-websocket %s on port %s" % (ver.get("obsVersion"), ver.get("obsWebSocketVersion"), resolve_connection(args)[1]))
    return 0


def cmd_quit(args):
    pids = obs_pids()
    if not pids:
        print("OBS%s is not running" % (" instance " + INSTANCE if INSTANCE else ""))
        return 0
    # Stop outputs first: with ConfirmOnExit=true OBS shows a blocking dialog if an output is active.
    try:
        c = connect(args, timeout=5.0)
        try:
            if c.call("GetStreamStatus").get("outputActive"):
                c.try_call("StopStream")
            if c.call("GetRecordStatus").get("outputActive"):
                c.try_call("StopRecord")
            end = time.time() + 15
            while time.time() < end:
                s = c.call("GetStreamStatus")
                r = c.call("GetRecordStatus")
                if not s.get("outputActive") and not r.get("outputActive"):
                    break
                time.sleep(0.5)
        finally:
            c.close()
    except OBSError as e:
        print("note: could not stop outputs over websocket (%s); quitting anyway" % e)
    s = system()
    try:
        if s == "Windows":
            subprocess.run(["taskkill", "/IM", "obs64.exe"], capture_output=True, timeout=15)
        elif s == "Darwin" and not INSTANCE and not instance_pids():
            subprocess.run(["osascript", "-e", 'tell application id "com.obsproject.obs-studio" to quit'], capture_output=True, timeout=15)
        else:
            # The AppleScript quit addresses the bundle id, which every instance shares, so signal the
            # exact process instead; OBS handles SIGTERM with a clean shutdown.
            for pid in pids:
                os.kill(pid, 15)
    except (OSError, subprocess.SubprocessError) as e:
        print("graceful quit request failed: %s" % e)
    end = time.time() + args.timeout
    while time.time() < end:
        if not [p for p in pids if pid_is_obs(p)]:
            print("OBS exited")
            return 0
        time.sleep(0.5)
    if getattr(args, "force", False):
        for pid in pids:
            try:
                os.kill(pid, 9)
            except OSError:
                pass
        time.sleep(1.0)
        print("OBS killed (unclean; the next `launch` clears the crash sentinel)")
        return 0
    raise TimeoutError_("OBS still running after %ss (a dialog may be open); rerun with --force" % args.timeout)


def cmd_instance(args):
    if args.action == "new":
        print(new_instance())
        return 0
    if args.action == "list":
        rows = []
        for name in instance_names():
            cfg_dir = instance_config_dir(name)
            ws = read_websocket_config(cfg_dir) if os.path.isfile(websocket_config_path(cfg_dir)) else {}
            rows.append({"instance": name, "pid": instance_pid(name), "websocket_port": ws.get("server_port"),
                         "config_dir": cfg_dir})
        if args.json:
            dump(rows)
        elif not rows:
            print("no instances under %s" % instances_root())
        else:
            for r in rows:
                print("%-20s pid=%-8s port=%-6s %s" % (r["instance"], r["pid"] or "-", r["websocket_port"], r["config_dir"]))
        return 0
    names = instance_names() if args.all else ([INSTANCE] if INSTANCE else [])
    if not names:
        raise LocalStateError("give --instance ID or --all")
    rc = 0
    for name in names:
        set_instance(name)
        try:
            cmd_quit(args)
        except OBSError as e:
            print("instance %s: %s" % (name, e))
            rc = e.exit_code
            continue
        shutil.rmtree(instance_dir(name), ignore_errors=True)
        print("instance %s removed" % name)
    return rc


def parse_json_arg(text, what="request data"):
    if text is None:
        return None
    if text == "-":
        text = sys.stdin.read()
    elif text.startswith("@"):
        with open(text[1:], encoding="utf-8") as f:
            text = f.read()
    try:
        return json.loads(text)
    except ValueError as e:
        raise LocalStateError("%s is not valid JSON: %s" % (what, e))


def cmd_call(args):
    data = parse_json_arg(args.data)
    c = connect(args)
    try:
        resp = c.request(args.request_type, data)
    finally:
        c.close()
    status = resp.get("requestStatus", {})
    if status.get("result"):
        dump(resp.get("responseData") or {})
        return 0
    dump({"requestType": args.request_type, "requestStatus": status})
    return 1


def cmd_batch(args):
    requests = parse_json_arg(args.file, "batch file")
    if isinstance(requests, dict) and "requests" in requests:
        requests = requests["requests"]
    exec_type = {"none": -1, "serial-realtime": 0, "serial-frame": 1, "parallel": 2}[args.execution_type]
    c = connect(args)
    try:
        resp = c.batch(requests, args.halt_on_failure, exec_type)
    finally:
        c.close()
    results = resp.get("results", [])
    dump(results)
    return 0 if all(r.get("requestStatus", {}).get("result") for r in results) else 1


def cmd_events(args):
    subs = SUB_ALL
    if args.subscriptions is not None:
        subs = args.subscriptions
    c = connect(args, subscriptions=subs, timeout=1.0)
    end = time.time() + args.timeout
    wanted = set(args.types.split(",")) if args.types else None
    count = 0
    try:
        while time.time() < end:
            try:
                ev = c.next_event()
            except TimeoutError_:
                continue
            if wanted and ev.get("eventType") not in wanted:
                continue
            print(json.dumps(ev, ensure_ascii=False), flush=True)
            count += 1
            if args.count and count >= args.count:
                break
    finally:
        c.close()
    return 0


def cmd_wait_stream(args):
    want = args.state == "active"
    c = connect(args)
    end = time.time() + args.timeout
    try:
        while True:
            s = c.call("GetStreamStatus")
            if bool(s.get("outputActive")) == want and not (want and s.get("outputReconnecting")):
                dump(s)
                return 0
            if time.time() >= end:
                dump(s)
                raise TimeoutError_("stream did not become %s within %ss" % (args.state, args.timeout))
            time.sleep(1.0)
    finally:
        c.close()


def cmd_screenshot(args):
    c = connect(args)
    try:
        source = args.source
        if not source:
            source = c.call("GetCurrentProgramScene").get("sceneName")
        data = {"sourceName": source, "imageFormat": args.format, "imageFilePath": os.path.abspath(args.out)}
        if args.width:
            data["imageWidth"] = args.width
        if args.height:
            data["imageHeight"] = args.height
        c.call("SaveSourceScreenshot", data)
        print("saved %s (source %r)" % (os.path.abspath(args.out), source))
        return 0
    finally:
        c.close()


# --------------------------------------------------------------------------- CLI
def add_conn_args(p):
    p.add_argument("--host")
    p.add_argument("--port", type=int)
    p.add_argument("--password")
    p.add_argument("--timeout", type=float, default=10.0, help="socket timeout in seconds")
    p.add_argument("--instance", help="isolated OBS instance id (default $OBS_INSTANCE; none = the main OBS)")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("probe", help="report OBS install, config, websocket state; test a connection")
    add_conn_args(p)
    p.add_argument("--json", action="store_true")
    p.add_argument("--show-password", action="store_true")
    p.set_defaults(func=cmd_probe)

    p = sub.add_parser("enable-websocket", help="enable the websocket server in config.json")
    add_conn_args(p)
    p.add_argument("--ws-port", dest="port", type=int, help="server port (default keep/4455)")
    p.add_argument("--set-password", dest="password", help="set this password (default keep or generate)")
    p.add_argument("--no-auth", action="store_true", help="disable authentication (local testing only)")
    p.add_argument("--show-password", action="store_true")
    p.add_argument("--restart", action="store_true", help="quit and relaunch OBS if it is running")
    p.add_argument("--wait", type=int, default=90)
    p.set_defaults(func=cmd_enable_websocket, profile=None, collection=None, scene=None, startstreaming=False, force=False)

    p = sub.add_parser("launch", help="start OBS detached and wait for the websocket")
    add_conn_args(p)
    p.add_argument("--profile")
    p.add_argument("--collection")
    p.add_argument("--scene")
    p.add_argument("--startstreaming", action="store_true")
    p.add_argument("--startrecording", action="store_true")
    p.add_argument("--minimize-to-tray", action="store_true")
    p.add_argument("--studio-mode", action="store_true")
    p.add_argument("--portable", action="store_true")
    p.add_argument("--verbose", action="store_true")
    p.add_argument("--multi", action="store_true", help="pass --multi (automatic when another OBS runs or with --instance)")
    p.add_argument("--allow-updater", action="store_true", help="do not pass --disable-updater")
    p.add_argument("--allow-missing-files-check", action="store_true")
    p.add_argument("--keep-sentinel", action="store_true", help="do not clear stale crash sentinels")
    p.add_argument("--reuse", action="store_true", help="with --instance: attach to the instance if it already runs (only one you launched)")
    p.add_argument("--ws-port", type=int, help="override websocket port for this run (--websocket_port)")
    p.add_argument("--ws-password", help="override websocket password for this run (--websocket_password)")
    p.add_argument("--ws-debug", action="store_true")
    p.add_argument("--wait", type=int, default=90, help="seconds to wait for the websocket (0 = do not wait)")
    p.add_argument("extra", nargs="*", help="extra OBS arguments after --")
    p.set_defaults(func=cmd_launch)

    p = sub.add_parser("quit", help="stop outputs, quit OBS gracefully, wait for exit")
    add_conn_args(p)
    p.add_argument("--force", action="store_true", help="kill if it does not exit in time")
    p.set_defaults(func=cmd_quit)
    p.set_defaults(timeout=30.0)

    p = sub.add_parser("call", help="send one request")
    add_conn_args(p)
    p.add_argument("request_type")
    p.add_argument("data", nargs="?", help="JSON object, '-' for stdin, or @file.json")
    p.set_defaults(func=cmd_call)

    p = sub.add_parser("batch", help="send a RequestBatch")
    add_conn_args(p)
    p.add_argument("file", help="JSON array of {requestType, requestData}, '-' for stdin, or @file")
    p.add_argument("--halt-on-failure", action="store_true")
    p.add_argument("--execution-type", choices=["none", "serial-realtime", "serial-frame", "parallel"], default="serial-realtime")
    p.set_defaults(func=cmd_batch)

    p = sub.add_parser("events", help="print events as JSON lines (--timeout = seconds to listen)")
    add_conn_args(p)
    p.set_defaults(timeout=30.0)
    p.add_argument("--types", help="comma-separated eventType filter")
    p.add_argument("--count", type=int, default=0, help="stop after N events")
    p.add_argument("--subscriptions", type=int, help="EventSubscription bitmask (default all normal events)")
    p.set_defaults(func=cmd_events)

    p = sub.add_parser("wait-stream", help="wait until the stream output is active/inactive")
    add_conn_args(p)
    p.add_argument("--state", choices=["active", "inactive"], default="active")
    p.set_defaults(func=cmd_wait_stream, timeout=30.0)

    p = sub.add_parser("screenshot", help="save a screenshot of the program scene or a source")
    add_conn_args(p)
    p.add_argument("--out", required=True)
    p.add_argument("--source", help="source/scene name (default: current program scene)")
    p.add_argument("--format", default="png")
    p.add_argument("--width", type=int)
    p.add_argument("--height", type=int)
    p.set_defaults(func=cmd_screenshot)

    p = sub.add_parser("instance", help="new | list | remove isolated instances")
    add_conn_args(p)
    p.add_argument("action", choices=["new", "list", "remove"])
    p.add_argument("--all", action="store_true", help="remove: every instance")
    p.add_argument("--force", action="store_true", help="remove: kill an instance that does not quit in time")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_instance, timeout=30.0)

    args = ap.parse_args(argv)
    try:
        apply_instance_args(args)
        return args.func(args) or 0
    except OBSError as e:
        print("error: %s" % e, file=sys.stderr)
        return e.exit_code
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
