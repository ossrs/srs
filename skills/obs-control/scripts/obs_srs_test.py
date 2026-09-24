#!/usr/bin/env python3
"""obs_srs_test.py - drive OBS Studio as a test publisher for an SRS media server.

Builds (idempotently) an OBS profile + scene collection + scene + test sources, points the
stream output at SRS over RTMP, SRT or WHIP, starts the stream and verifies on the SRS HTTP API
that the stream is being published. Everything goes through obs-websocket (see obsws.py in this
directory); OBS must already be running with the websocket server enabled
(`obsws.py probe` tells you; `obsws.py enable-websocket` + `obsws.py launch` fix it).

Examples
  # RTMP to a local SRS, start streaming and check the SRS API
  obs_srs_test.py setup --rtmp rtmp://localhost/live/livestream --start --verify

  # WHIP (WebRTC) publish, 720p30 at 1500 kbps
  obs_srs_test.py setup --whip 'http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream' \
      --video-bitrate 1500 --start --verify

  # SRT publish (OBS chooses the MPEG-TS output from the srt:// prefix)
  obs_srs_test.py setup --srt 'srt://localhost:10080?streamid=#!::r=live/livestream,m=publish' --start

  obs_srs_test.py stream status           # OBS side: active? bytes, frames, congestion
  obs_srs_test.py verify --srs-api http://localhost:1985 --app live --stream livestream
  obs_srs_test.py stream stop
  obs_srs_test.py teardown                # remove the test profile and scene collection

Parallel tests: give every test its own isolated OBS (ID=$(obsws.py instance new); obsws.py launch
--instance $ID) and pass the same --instance here. Profile, collection, scene and source names then carry
the id ("SRS Test t3f9a2"), and "{instance}" in a URL or name expands to it, so each OBS
publishes its own stream:
  obs_srs_test.py setup --instance $ID --rtmp 'rtmp://localhost/live/{instance}' --start --verify
Before starting, setup refuses a stream that SRS already shows as published by someone else.

Exit codes follow obsws.py (0 ok, 1 OBS refused a request, 2 connect, 3 auth, 4 local state, 5 timeout).
"""

import argparse
import html
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import obsws  # noqa: E402

DEFAULT_PROFILE = "SRS Test"
DEFAULT_COLLECTION = "SRS Test"
DEFAULT_SCENE = "SRS Scene"
SRC_BACKGROUND = "SRS Background"
SRC_LABEL = "SRS Label"
SRC_MEDIA = "SRS Media"
SRC_BROWSER = "SRS Browser"
SRC_IMAGE = "SRS Image"
PLACEHOLDER = "{instance}"

ALIGN_TOP_LEFT = 5  # OBS_ALIGN_TOP | OBS_ALIGN_LEFT

# SimpleOutput/StreamEncoder names (frontend/utility/SimpleOutput.cpp) -> platform hint
SIMPLE_ENCODERS = ["x264", "x264_lowcpu", "apple_h264", "apple_hevc", "nvenc", "nvenc_hevc", "nvenc_av1",
                   "qsv", "qsv_av1", "amd", "amd_hevc", "amd_av1"]


def log(msg):
    print(msg, flush=True)


def named(base):
    """Default OBS object name, carrying the instance id so logs, screenshots and SRS clients of parallel
    tests can be told apart and nothing is shared by accident."""
    return "%s %s" % (base, obsws.INSTANCE) if obsws.INSTANCE else base


def expand(value):
    if not value or PLACEHOLDER not in value:
        return value
    if not obsws.INSTANCE:
        raise obsws.LocalStateError("%r uses %s but no --instance (or OBS_INSTANCE) is set" % (value, PLACEHOLDER))
    return value.replace(PLACEHOLDER, obsws.INSTANCE)


def resolve_args(args):
    for field in ("rtmp", "rtmp_server", "rtmp_key", "srt", "whip", "app", "stream", "text", "profile", "collection", "scene"):
        if hasattr(args, field):
            setattr(args, field, expand(getattr(args, field)))
    if hasattr(args, "profile"):
        args.profile = args.profile or named(DEFAULT_PROFILE)
        args.collection = args.collection or named(DEFAULT_COLLECTION)
        args.scene = args.scene or named(DEFAULT_SCENE)


def write_atomic(path, data):
    """Write via a private temp file and rename, so a parallel reader never sees a partial file."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".tmp-")
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write(data)
    os.replace(tmp, path)


def rgb_to_obs_color(hex_color):
    """'#RRGGBB' -> OBS packed int (0xAABBGGRR, alpha opaque)."""
    h = hex_color.lstrip("#")
    if len(h) != 6:
        raise obsws.LocalStateError("color must be #RRGGBB, got %r" % hex_color)
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return 0xFF000000 | (b << 16) | (g << 8) | r


# --------------------------------------------------------------------------- destination parsing
class Target:
    def __init__(self, kind, service_type, settings, app, stream, host):
        self.kind = kind                  # rtmp | srt | whip
        self.service_type = service_type  # rtmp_custom | whip_custom
        self.settings = settings
        self.app = app
        self.stream = stream
        self.host = host

    def describe(self):
        if self.kind == "rtmp":
            return "%s/%s" % (self.settings["server"], self.settings["key"])
        return self.settings["server"]


def parse_target(args):
    if args.whip:
        url = args.whip
        u = urllib.parse.urlparse(url)
        q = urllib.parse.parse_qs(u.query)
        app = args.app or (q.get("app") or ["live"])[0]
        stream = args.stream or (q.get("stream") or ["livestream"])[0]
        # SRS answers WHIP with the ICE candidate from its `candidate` config. Inside Docker that is the
        # container IP, which OBS cannot reach, so DTLS never completes ("DTLS: Hang" in the SRS log).
        # SRS lets the client override it per request with ?eip=<reachable ip>.
        eip = getattr(args, "eip", None)
        if eip is None and not getattr(args, "no_eip", False) and u.hostname in ("localhost", "127.0.0.1", "::1"):
            eip = "127.0.0.1"
        if eip and "eip" not in q:
            url += ("&" if u.query else "?") + "eip=" + eip
            log("WHIP: added eip=%s so SRS advertises a reachable ICE candidate (disable with --no-eip)" % eip)
        return Target("whip", "whip_custom", {"server": url, "bearer_token": args.bearer_token or ""},
                      app, stream, u.hostname)
    if args.srt:
        u = urllib.parse.urlparse(args.srt)
        app, stream = args.app, args.stream
        # SRS streamid: #!::r=app/stream,m=publish
        m = None
        for part in urllib.parse.unquote(u.query).replace("streamid=#!::", "").split(","):
            if part.startswith("r="):
                m = part[2:]
        if m and "/" in m:
            app = app or m.split("/")[0]
            stream = stream or m.split("/", 1)[1]
        return Target("srt", "rtmp_custom", {"server": args.srt, "key": "", "use_auth": False},
                      app or "live", stream or "livestream", u.hostname)
    server, key = args.rtmp_server, args.rtmp_key
    if args.rtmp:
        u = urllib.parse.urlparse(args.rtmp)
        parts = [p for p in u.path.split("/") if p]
        if len(parts) >= 2 and not key:
            key = parts[-1]
            server = "%s://%s/%s" % (u.scheme, u.netloc, "/".join(parts[:-1]))
        else:
            server = args.rtmp
    if not server:
        raise obsws.LocalStateError("give a destination: --rtmp rtmp://host/app/stream, --srt 'srt://...' or --whip URL")
    u = urllib.parse.urlparse(server)
    parts = [p for p in u.path.split("/") if p]
    app = args.app or (parts[0] if parts else "live")
    stream = args.stream or (key or "livestream")
    return Target("rtmp", "rtmp_custom", {"server": server, "key": key or "", "use_auth": False}, app, stream, u.hostname)


# --------------------------------------------------------------------------- OBS building blocks
def ensure_no_active_stream(c, stop_first):
    status = c.call("GetStreamStatus")
    if not status.get("outputActive"):
        return
    if not stop_first:
        raise obsws.LocalStateError("OBS is streaming; run `stream stop%s` first or pass --stop-first" % obsws.instance_flag())
    log("stopping the active stream first")
    c.try_call("StopStream")
    wait_stream(c, active=False, timeout=20)


def ensure_profile(c, name):
    plist = c.call("GetProfileList")
    if name not in plist["profiles"]:
        c.call("CreateProfile", {"profileName": name})  # creates AND switches
        # obs_frontend_create_profile is queued onto the UI thread, so poll until it is really current
        # before writing profile parameters (they would otherwise land in the previous profile).
        end = time.time() + 15
        while c.call("GetProfileList")["currentProfileName"] != name:
            if time.time() > end:
                raise obsws.TimeoutError_("profile %r was created but did not become current" % name)
            time.sleep(0.2)
        log("profile created: %s" % name)
    elif plist["currentProfileName"] != name:
        c.call("SetCurrentProfile", {"profileName": name})
        log("profile switched: %s" % name)
    else:
        log("profile already current: %s" % name)
    return plist["profiles"]


def set_profile_params(c, params):
    """Write basic.ini values. Returns the list of keys whose value actually changed."""
    changed = []
    for (category, key), value in params.items():
        cur = c.call("GetProfileParameter", {"parameterCategory": category, "parameterName": key})
        if cur.get("parameterValue") != value:
            c.call("SetProfileParameter", {"parameterCategory": category, "parameterName": key, "parameterValue": value})
            changed.append("%s/%s=%s" % (category, key, value))
    return changed


def reload_profile(c, name, all_profiles):
    """OBS builds its output handler (mode, encoder ids) when a profile is activated; parameters written
    afterwards only apply on the next activation. Switching away and back forces that without a restart."""
    others = [p for p in all_profiles if p != name]
    if not others:
        log("note: only one profile exists; encoder/output-mode changes apply after `obsws.py quit && obsws.py launch`")
        return False
    c.call("SetCurrentProfile", {"profileName": others[0]})
    c.call("SetCurrentProfile", {"profileName": name})
    log("profile reloaded (via %s) so encoder/output settings take effect" % others[0])
    return True


def ensure_collection(c, name):
    clist = c.call("GetSceneCollectionList")
    if name not in clist["sceneCollections"]:
        c.call("CreateSceneCollection", {"sceneCollectionName": name})  # blocks until loaded
        log("scene collection created: %s" % name)
    elif clist["currentSceneCollectionName"] != name:
        c.call("SetCurrentSceneCollection", {"sceneCollectionName": name})
        log("scene collection switched: %s" % name)
    else:
        log("scene collection already current: %s" % name)


def ensure_scene(c, name):
    scenes = [s["sceneName"] for s in c.call("GetSceneList")["scenes"]]
    if name not in scenes:
        c.call("CreateScene", {"sceneName": name})
        log("scene created: %s" % name)
    c.call("SetCurrentProgramScene", {"sceneName": name})


def ensure_input(c, scene, name, kind, settings, transform=None):
    inputs = {i["inputName"]: i for i in c.call("GetInputList")["inputs"]}
    if name in inputs:
        if inputs[name]["inputKind"] != kind:
            log("warning: input %r exists with kind %s (wanted %s); reusing it" % (name, inputs[name]["inputKind"], kind))
        c.call("SetInputSettings", {"inputName": name, "inputSettings": settings, "overlay": True})
        found = c.try_call("GetSceneItemId", {"sceneName": scene, "sourceName": name})
        if found is None:
            found = c.call("CreateSceneItem", {"sceneName": scene, "sourceName": name})
        item_id = found["sceneItemId"]
        log("input updated: %s (%s)" % (name, kind))
    else:
        created = c.call("CreateInput", {"sceneName": scene, "inputName": name, "inputKind": kind, "inputSettings": settings})
        item_id = created["sceneItemId"]
        log("input created: %s (%s)" % (name, kind))
    if transform:
        c.call("SetSceneItemTransform", {"sceneName": scene, "sceneItemId": item_id, "sceneItemTransform": transform})
    return item_id


def test_clip_path(width, height, fps):
    return os.path.join(tempfile.gettempdir(), "obs-control", "testsrc-%dx%d-%d.mp4" % (width, height, fps))


def ensure_test_clip(width, height, fps, seconds=20):
    """Generate a moving test pattern with a 440 Hz tone using ffmpeg (needed so the encoder has
    real motion to encode; a static colour source produces an almost empty bitstream)."""
    path = test_clip_path(width, height, fps)
    if os.path.isfile(path) and os.path.getsize(path) > 0:
        return path
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        return None
    os.makedirs(os.path.dirname(path), exist_ok=True)
    # Parallel setups share the clip; each renders to its own temp file and renames it into place.
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix=".tmp-", suffix=".mp4")
    os.close(fd)
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-y",
           "-f", "lavfi", "-i", "testsrc2=size=%dx%d:rate=%d" % (width, height, fps),
           "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000",
           "-t", str(seconds), "-c:v", "libx264", "-preset", "veryfast", "-pix_fmt", "yuv420p",
           "-c:a", "aac", "-shortest", tmp]
    try:
        subprocess.run(cmd, check=True, timeout=120)
        os.replace(tmp, path)
    except (OSError, subprocess.SubprocessError) as e:
        log("warning: ffmpeg could not generate the test clip: %s" % e)
        return None
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    return path


def clock_page_path():
    path = os.path.join(tempfile.gettempdir(), "obs-control", "clock.html")
    if not os.path.isfile(path):
        write_atomic(path, """<!doctype html><html><head><meta charset="utf-8"><style>
body{margin:0;background:transparent;font-family:Menlo,monospace;color:#fff}
#t{position:absolute;right:32px;bottom:32px;font-size:64px;text-shadow:0 0 12px #000}
#bar{position:absolute;left:0;top:0;height:16px;background:#3fb950}
</style></head><body><div id="bar"></div><div id="t"></div><script>
function tick(){var d=new Date();document.getElementById('t').textContent=d.toISOString().substr(11,12);
document.getElementById('bar').style.width=((d.getTime()%10000)/100)+'%';requestAnimationFrame(tick)}tick();
</script></body></html>""")
    return path


def build_sources(c, args, scene, width, height, fps, target):
    kinds = set(c.call("GetInputKindList")["inputKinds"])
    wanted = [s.strip() for s in args.sources.split(",") if s.strip()]
    for spec in wanted:
        if spec == "color":
            kind = "color_source_v3" if "color_source_v3" in kinds else "color_source"
            ensure_input(c, scene, named(SRC_BACKGROUND), kind,
                         {"color": rgb_to_obs_color(args.color), "width": width, "height": height})
        elif spec == "text":
            kind = "text_ft2_source_v2" if "text_ft2_source_v2" in kinds else ("text_gdiplus_v3" if "text_gdiplus_v3" in kinds else None)
            if not kind:
                log("skip text: no text source kind available")
                continue
            text = args.text or "SRS test: %s -> %s" % (args.profile, target.describe())
            settings = {"text": text}
            if kind.startswith("text_ft2"):
                settings["font"] = {"face": "Helvetica" if sys.platform == "darwin" else "Sans", "size": max(24, height // 14), "flags": 0, "style": ""}
                settings["color1"] = 0xFFFFFFFF
                settings["color2"] = 0xFFFFFFFF
                settings["outline"] = True
            else:
                settings["font"] = {"face": "Arial", "size": max(24, height // 14), "flags": 0, "style": ""}
            ensure_input(c, scene, named(SRC_LABEL), kind, settings,
                         {"positionX": 32, "positionY": 32, "alignment": ALIGN_TOP_LEFT})
        elif spec == "media" or spec.startswith("media:"):
            if "ffmpeg_source" not in kinds:
                log("skip media: ffmpeg_source not available")
                continue
            path = spec[6:] if spec.startswith("media:") and spec[6:] not in ("", "auto") else (args.media_file or ensure_test_clip(width, height, fps))
            if not path:
                log("skip media: ffmpeg not found to generate a test clip; pass --media-file")
                continue
            path = os.path.abspath(path)
            if not os.path.isfile(path):
                raise obsws.LocalStateError("media file not found: %s" % path)
            ensure_input(c, scene, named(SRC_MEDIA), "ffmpeg_source",
                         {"is_local_file": True, "local_file": path, "looping": True, "clear_on_media_end": False,
                          "restart_on_activate": False, "hw_decode": False},
                         {"positionX": 0, "positionY": 0, "alignment": ALIGN_TOP_LEFT, "boundsType": "OBS_BOUNDS_SCALE_INNER",
                          "boundsAlignment": 0, "boundsWidth": width, "boundsHeight": height})
        elif spec in ("clock", "browser") or spec.startswith("browser:"):
            if "browser_source" not in kinds:
                log("skip %s: browser_source not available in this OBS build" % spec)
                continue
            if spec.startswith("browser:") and spec[8:]:
                settings = {"is_local_file": False, "url": spec[8:], "width": width, "height": height, "fps_custom": True, "fps": fps}
            elif spec == "browser" and args.browser_url:
                settings = {"is_local_file": False, "url": args.browser_url, "width": width, "height": height, "fps_custom": True, "fps": fps}
            else:
                settings = {"is_local_file": True, "local_file": clock_page_path(), "width": width, "height": height, "fps_custom": True, "fps": fps}
            ensure_input(c, scene, named(SRC_BROWSER), "browser_source", settings, {"positionX": 0, "positionY": 0, "alignment": ALIGN_TOP_LEFT})
        elif spec.startswith("image:"):
            path = os.path.abspath(spec[6:])
            if not os.path.isfile(path):
                raise obsws.LocalStateError("image not found: %s" % path)
            ensure_input(c, scene, named(SRC_IMAGE), "image_source", {"file": path},
                         {"positionX": 0, "positionY": 0, "alignment": ALIGN_TOP_LEFT, "boundsType": "OBS_BOUNDS_SCALE_INNER",
                          "boundsAlignment": 0, "boundsWidth": width, "boundsHeight": height})
        else:
            raise obsws.LocalStateError("unknown source spec %r (use color,text,media[:path],clock,browser[:url],image:path)" % spec)


def wait_stream(c, active, timeout):
    end = time.time() + timeout
    while True:
        s = c.call("GetStreamStatus")
        if bool(s.get("outputActive")) == active and not (active and s.get("outputReconnecting")):
            return s
        if time.time() >= end:
            raise obsws.TimeoutError_("stream did not become %s within %ss: %s" % ("active" if active else "inactive", timeout, json.dumps(s)))
        time.sleep(1.0)


# --------------------------------------------------------------------------- SRS verification
def http_json(url, timeout=5):
    with urllib.request.urlopen(url, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))


def srs_find_stream(api, app, stream):
    data = http_json(api.rstrip("/") + "/api/v1/streams/")
    for s in data.get("streams", []):
        if s.get("app") == app and s.get("name") == stream:
            return s
    return None


def verify_srs(api, app, stream, timeout, expect_active=True, wait_bitrate=False):
    """Poll the SRS API until the stream is (in)active. SRS's kbps.recv_30s is a trailing 30 s window, so it
    reads 0 right after publish starts; wait_bitrate keeps polling (up to ~40 s more) until it is non-zero."""
    end = time.time() + timeout
    last_err = None
    last = None
    while True:
        try:
            last = srs_find_stream(api, app, stream)
            last_err = None
        except (urllib.error.URLError, OSError, ValueError) as e:
            last_err = e
        active = bool(last and last.get("publish", {}).get("active"))
        if active == expect_active and (not expect_active or last.get("video") or time.time() > end - 1):
            break
        if time.time() >= end:
            break
        time.sleep(1.0)
    if expect_active and wait_bitrate and last and last.get("publish", {}).get("active"):
        end2 = time.time() + 40
        while (last.get("kbps") or {}).get("recv_30s", 0) == 0 and time.time() < end2:
            time.sleep(2.0)
            try:
                last = srs_find_stream(api, app, stream) or last
            except (urllib.error.URLError, OSError, ValueError):
                pass
    if last_err:
        raise obsws.ConnectError("SRS API unreachable at %s: %s" % (api, last_err))
    if expect_active and not (last and last.get("publish", {}).get("active")):
        if last:
            log("SRS: %s/%s is listed but publish.active=false (a previous publisher's entry lingering, or the new one never completed)" % (app, stream))
        else:
            others = [("%s/%s" % (s.get("app"), s.get("name"))) for s in http_json(api.rstrip("/") + "/api/v1/streams/").get("streams", [])]
            log("SRS: %s/%s is NOT being published (streams present: %s)" % (app, stream, others or "none"))
        log("hints: OBS side -> `obs_srs_test.py stream status%s` and the newest OBS log (obsws.py probe shows its path); " % obsws.instance_flag() +
            "SRS side -> `docker logs srs --tail 50` or objs/srs.log. WHIP stuck at 'PeerConnection state: Connecting' "
            "then 'Failed' (OBS does not retry WHIP) plus 'DTLS: Hang' in SRS means the ICE candidate SRS advertised "
            "('RTC: Use candidates <ip>' in its log, the container's 172.17.x.x when CANDIDATE is unset) is unreachable: "
            "pass --eip <ip OBS can reach> or set CANDIDATE for SRS. RTMP 'Connection to ... failed' means wrong host/port or SRS down.")
        return False
    if not expect_active:
        ok = not (last and last.get("publish", {}).get("active"))
        log("SRS: %s/%s publish inactive: %s" % (app, stream, ok))
        return ok
    v = last.get("video") or {}
    a = last.get("audio") or {}
    kbps = last.get("kbps") or {}
    recv = kbps.get("recv_30s", 0)
    age = ""
    if last.get("live_ms"):
        age = "  live for %ds" % max(0, int(time.time() - last["live_ms"] / 1000.0))
    recv_txt = ("%s kbps" % recv) if recv else "n/a yet (SRS averages over 30 s; re-run verify or use --bitrate)"
    log("SRS: %s/%s is live  video=%s %s %sx%s  audio=%s %sHz ch%s  recv=%s  clients=%s%s"
        % (app, stream, v.get("codec"), v.get("profile"), v.get("width"), v.get("height"),
           a.get("codec"), a.get("sample_rate"), a.get("channel"), recv_txt, last.get("clients"), age))
    if not a:
        log("note: SRS reports no audio track yet (or the publisher sends a codec SRS does not accept over this protocol)")
    u = urllib.parse.urlparse(api)
    host = u.hostname or "localhost"
    log("play:  rtmp://%s/%s/%s  |  http://%s:8080/%s/%s.flv  |  http://%s:8080/%s/%s.m3u8  |  WHEP http://%s:%s/rtc/v1/whep/?app=%s&stream=%s"
        % (host, app, stream, host, app, stream, host, app, stream, host, u.port or 1985, app, stream))
    return True


def ensure_stream_free(api, target):
    """Refuse to publish onto a stream another client already publishes: SRS would reject the second
    publisher (or, for some protocols, the verify step would see the other one and pass). A publisher
    this OBS just stopped (--stop-first) can stay listed for a moment, so allow a few seconds."""
    end = time.time() + 5
    while True:
        try:
            s = srs_find_stream(api, target.app, target.stream)
        except (urllib.error.URLError, OSError, ValueError) as e:
            log("note: cannot check %s/%s on the SRS API %s before publishing (%s)" % (target.app, target.stream, api, e))
            return
        if not (s and s.get("publish", {}).get("active")) or time.time() >= end:
            break
        time.sleep(1.0)
    if s and s.get("publish", {}).get("active"):
        raise obsws.LocalStateError(
            "SRS already has a publisher on %s/%s (client %s); give each test its own stream name, e.g. "
            "rtmp://host/%s/{instance} with --instance" % (target.app, target.stream, s.get("publish", {}).get("cid"), target.app))


def srs_api_from_target(args, target):
    if args.srs_api:
        return args.srs_api
    host = target.host or "localhost"
    return "http://%s:1985" % host


# --------------------------------------------------------------------------- commands
def cmd_setup(args):
    target = parse_target(args)
    width, height, fps = args.width, args.height, args.fps
    audio_encoder = "opus" if target.kind == "whip" else "aac"
    c = obsws.connect(args)
    try:
        ver = c.call("GetVersion")
        log("connected: OBS %s, obs-websocket %s (%s)" % (ver.get("obsVersion"), ver.get("obsWebSocketVersion"), ver.get("platform")))
        ensure_no_active_stream(c, args.stop_first)
        if args.start:
            ensure_stream_free(srs_api_from_target(args, target), target)

        all_profiles = ensure_profile(c, args.profile)
        if args.profile not in all_profiles:
            all_profiles = all_profiles + [args.profile]
        c.call("SetVideoSettings", {"baseWidth": width, "baseHeight": height, "outputWidth": width, "outputHeight": height,
                                    "fpsNumerator": fps, "fpsDenominator": 1})
        params = {
            ("Output", "Mode"): "Simple",
            ("SimpleOutput", "VBitrate"): str(args.video_bitrate),
            ("SimpleOutput", "ABitrate"): str(args.audio_bitrate),
            ("SimpleOutput", "StreamEncoder"): args.encoder,
            ("SimpleOutput", "StreamAudioEncoder"): audio_encoder,
            ("SimpleOutput", "Preset"): args.preset,
            ("Output", "Reconnect"): "true" if args.reconnect else "false",
        }
        changed = set_profile_params(c, params)
        if changed:
            log("profile parameters written: %s" % ", ".join(changed))

        # Service before the profile reload: on activation OBS runs UpdateProfileEncoders(), which rewrites
        # the simple-mode encoders to match the *current* service's supported codecs (e.g. forces opus while
        # a WHIP service is selected). Setting the service first makes that pass agree with our parameters.
        c.call("SetStreamServiceSettings", {"streamServiceType": target.service_type, "streamServiceSettings": target.settings})
        log("stream service: %s -> %s" % (target.service_type, target.describe()))
        # The output handler (mode, encoder ids, service binding) is rebuilt only on profile activation, so
        # re-activate whenever anything that feeds it may have changed.
        reload_profile(c, args.profile, all_profiles)

        ensure_collection(c, args.collection)
        ensure_scene(c, args.scene)
        build_sources(c, args, args.scene, width, height, fps, target)

        summary = {
            "profile": args.profile, "collection": args.collection, "scene": args.scene,
            "video": "%dx%d@%d" % (width, height, fps), "video_bitrate_kbps": args.video_bitrate,
            "encoder": args.encoder, "audio_encoder": audio_encoder, "target": target.describe(),
            "srs_app": target.app, "srs_stream": target.stream,
        }
        if args.start:
            c.call("StartStream")
            status = wait_stream(c, active=True, timeout=args.timeout)
            log("stream active: timecode %s, %s bytes" % (status.get("outputTimecode"), status.get("outputBytes")))
            summary["stream_active"] = True
    finally:
        c.close()
    log(json.dumps(summary, indent=2))
    if args.start and args.verify:
        ok = verify_srs(srs_api_from_target(args, target), target.app, target.stream, args.timeout, wait_bitrate=args.bitrate)
        return 0 if ok else 1
    return 0


def cmd_stream(args):
    c = obsws.connect(args)
    try:
        if args.action == "start":
            if c.call("GetStreamStatus").get("outputActive"):
                log("stream already active")
            else:
                c.call("StartStream")
            s = wait_stream(c, active=True, timeout=args.timeout)
        elif args.action == "stop":
            if not c.call("GetStreamStatus").get("outputActive"):
                log("stream already inactive")
            else:
                c.call("StopStream")
            s = wait_stream(c, active=False, timeout=args.timeout)
        else:
            s = c.call("GetStreamStatus")
        svc = c.call("GetStreamServiceSettings")
        s["service"] = svc
        obsws.dump(s)
        return 0
    finally:
        c.close()


def cmd_verify(args):
    ok = verify_srs(args.srs_api, args.app, args.stream, args.timeout, expect_active=not args.expect_inactive,
                    wait_bitrate=args.bitrate)
    return 0 if ok else 1


def cmd_teardown(args):
    c = obsws.connect(args)
    try:
        if c.call("GetStreamStatus").get("outputActive"):
            c.try_call("StopStream")
            wait_stream(c, active=False, timeout=20)
            log("stream stopped")
        clist = c.call("GetSceneCollectionList")
        if args.collection in clist["sceneCollections"]:
            if clist["currentSceneCollectionName"] == args.collection:
                others = [x for x in clist["sceneCollections"] if x != args.collection]
                if others:
                    c.call("SetCurrentSceneCollection", {"sceneCollectionName": others[0]})
                    log("switched scene collection to %s" % others[0])
                else:
                    log("cannot remove the only scene collection %r" % args.collection)
            if not args.keep_files:
                _, scenes_dir = obsws.basic_dirs()
                removed = False
                for fn in os.listdir(scenes_dir) if os.path.isdir(scenes_dir) else []:
                    if not fn.endswith(".json"):
                        continue
                    p = os.path.join(scenes_dir, fn)
                    try:
                        with open(p, encoding="utf-8") as f:
                            if json.load(f).get("name") != args.collection:
                                continue
                    except (OSError, ValueError):
                        continue
                    os.remove(p)
                    for extra in (p + ".bak",):
                        if os.path.exists(extra):
                            os.remove(extra)
                    removed = True
                    log("deleted %s (obs-websocket has no RemoveSceneCollection; OBS keeps the name in its list and "
                        "would re-create the file if it is selected again - `obsws.py quit && obsws.py launch` purges it)" % p)
                if not removed:
                    log("scene collection file for %r not found" % args.collection)
        plist = c.call("GetProfileList")
        if args.profile in plist["profiles"]:
            if len(plist["profiles"]) == 1:
                log("cannot remove the only profile %r" % args.profile)
            else:
                # RemoveProfile on the *current* profile makes OBS open a modal "confirm removal" dialog
                # that blocks every later request, so switch away first (the non-current path is silent).
                if plist["currentProfileName"] == args.profile:
                    other = [x for x in plist["profiles"] if x != args.profile][0]
                    c.call("SetCurrentProfile", {"profileName": other})
                    log("switched profile to %s" % other)
                c.call("RemoveProfile", {"profileName": args.profile})
                log("profile removed: %s" % args.profile)
        return 0
    finally:
        c.close()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        obsws.add_conn_args(p)
        p.add_argument("--profile", help="default '%s', plus ' <instance>' with --instance" % DEFAULT_PROFILE)
        p.add_argument("--collection", help="default '%s', plus ' <instance>' with --instance" % DEFAULT_COLLECTION)
        p.add_argument("--scene", help="default '%s', plus ' <instance>' with --instance" % DEFAULT_SCENE)

    p = sub.add_parser("setup", help="create/update profile, collection, scene, sources and stream service",
                       formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    common(p)
    dest = p.add_argument_group("destination (one of)")
    dest.add_argument("--rtmp", help="rtmp://host[:port]/app/stream (key = last path segment)")
    dest.add_argument("--rtmp-server", help="rtmp://host/app  (with --rtmp-key)")
    dest.add_argument("--rtmp-key")
    dest.add_argument("--srt", help="srt://host:10080?streamid=#!::r=app/stream,m=publish")
    dest.add_argument("--whip", help="http://host:1985/rtc/v1/whip/?app=live&stream=livestream")
    dest.add_argument("--bearer-token", help="WHIP bearer token (SRS: leave empty unless secured)")
    dest.add_argument("--eip", help="WHIP: SRS ICE candidate IP to advertise (?eip=); needed when SRS runs in Docker on another host")
    dest.add_argument("--no-eip", action="store_true", help="WHIP: do not auto-add eip=127.0.0.1 for localhost targets")
    p.add_argument("--app", help="SRS app for verification (default parsed from the URL)")
    p.add_argument("--stream", help="SRS stream name for verification (default parsed from the URL)")
    vid = p.add_argument_group("video/output")
    vid.add_argument("--width", type=int, default=1280)
    vid.add_argument("--height", type=int, default=720)
    vid.add_argument("--fps", type=int, default=30)
    vid.add_argument("--video-bitrate", type=int, default=2000, help="kbps")
    vid.add_argument("--audio-bitrate", type=int, default=160, help="kbps")
    vid.add_argument("--encoder", default="x264", help="SimpleOutput encoder: %s" % ", ".join(SIMPLE_ENCODERS))
    vid.add_argument("--preset", default="veryfast", help="x264 preset")
    vid.add_argument("--reconnect", action=argparse.BooleanOptionalAction, default=True,
                     help="auto-reconnect the stream output (RTMP/SRT; OBS's WHIP output stops instead of reconnecting)")
    src = p.add_argument_group("sources")
    src.add_argument("--sources", default="color,media,text", help="comma list: color,text,media[:path|auto],clock,browser[:url],image:path")
    src.add_argument("--color", default="#1F2A44", help="background colour #RRGGBB")
    src.add_argument("--text", help="label text (default describes the target)")
    src.add_argument("--media-file", help="video file for the media source (default: ffmpeg-generated test pattern)")
    src.add_argument("--browser-url")
    p.add_argument("--stop-first", action="store_true", help="stop an active stream before reconfiguring")
    p.add_argument("--start", action="store_true", help="start streaming after setup")
    p.add_argument("--verify", action="store_true", help="with --start: check the SRS API that the stream is published")
    p.add_argument("--bitrate", action="store_true", help="with --verify: also wait (<= 40 s) for SRS's 30 s bitrate window to fill")
    p.add_argument("--srs-api", help="SRS HTTP API base (default: http://<target host>:1985)")
    p.set_defaults(func=cmd_setup, timeout=30.0)

    p = sub.add_parser("stream", help="start | stop | status")
    obsws.add_conn_args(p)
    p.add_argument("action", choices=["start", "stop", "status"])
    p.set_defaults(func=cmd_stream, timeout=30.0)

    p = sub.add_parser("verify", help="check the SRS HTTP API for the published stream")
    p.add_argument("--srs-api", default="http://localhost:1985")
    p.add_argument("--app", default="live")
    p.add_argument("--stream", default="livestream")
    p.add_argument("--timeout", type=float, default=30.0)
    p.add_argument("--expect-inactive", action="store_true")
    p.add_argument("--bitrate", action="store_true", help="also wait (<= 40 s) for SRS's 30 s bitrate window to fill")
    p.add_argument("--instance", help="expands {instance} in --app/--stream (default $OBS_INSTANCE)")
    p.set_defaults(func=cmd_verify)

    p = sub.add_parser("teardown", help="stop streaming, remove the test profile and scene collection")
    common(p)
    p.add_argument("--keep-files", action="store_true", help="do not delete the scene collection JSON")
    p.set_defaults(func=cmd_teardown)

    args = ap.parse_args(argv)
    try:
        obsws.apply_instance_args(args)
        resolve_args(args)
        return args.func(args) or 0
    except obsws.OBSError as e:
        print("error: %s" % e, file=sys.stderr)
        return e.exit_code
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
