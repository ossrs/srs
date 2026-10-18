import argparse
import contextlib
import io
import json
import threading
import unittest
from http.server import BaseHTTPRequestHandler, HTTPServer
from unittest.mock import Mock, patch

import obs_srs_test as recipe


class TargetTest(unittest.TestCase):
    def target(self, **kwargs):
        values = dict(whip=None, srt=None, srt_streamid=None, srt_passphrase=None,
                      rtmp=None, rtmp_server=None, rtmp_key=None, app=None, stream=None)
        values.update(kwargs)
        return recipe.parse_target(argparse.Namespace(**values))

    def test_srt_explicit_streamid_and_passphrase(self):
        target = self.target(srt="srt://host:10080?latency=200", srt_streamid="#!::r=app/custom,m=publish",
                             srt_passphrase="example-passphrase")
        self.assertEqual(target.settings, dict(server="srt://host:10080?latency=200",
                                              key="#!::r=app/custom,m=publish", use_auth=True,
                                              password="example-passphrase"))
        self.assertEqual((target.app, target.stream, target.host), ("app", "custom", "host"))

    def test_srt_url_streamid_forms(self):
        for streamid in ("#!::r=app/custom,m=publish", "%23!%3A%3Ar%3Dapp%2Fcustom%2Cm%3Dpublish"):
            with self.subTest(streamid=streamid):
                target = self.target(srt="srt://host:10080?latency=200&streamid=" + streamid + "&mode=caller")
                self.assertEqual(target.settings, dict(server="srt://host:10080?latency=200&mode=caller",
                                                      key="#!::r=app/custom,m=publish", use_auth=False))
                self.assertEqual((target.app, target.stream), ("app", "custom"))

    def test_srt_url_streamid_overrides_explicit(self):
        target = self.target(srt="srt://host:10080?streamid=#!::r=live/url,m=publish",
                             srt_streamid="#!::r=live/key,m=publish")
        self.assertEqual(target.settings["key"], "#!::r=live/url,m=publish")
        self.assertEqual(target.stream, "url")

    def test_srt_defaults_and_verify_overrides(self):
        target = self.target(srt="srt://host:10080")
        self.assertEqual((target.app, target.stream), ("live", "livestream"))
        self.assertEqual(target.settings, dict(server="srt://host:10080", key="", use_auth=False))
        target = self.target(srt="srt://host:10080", srt_streamid="#!::r=live/key,m=publish",
                             app="override", stream="name")
        self.assertEqual((target.app, target.stream), ("override", "name"))

    def test_rtmp_url_keeps_query_in_key(self):
        target = self.target(rtmp="rtmp://host:1935/app/custom?x=y&vhost=example.test")
        self.assertEqual(target.settings, dict(server="rtmp://host:1935/app",
                                              key="custom?x=y&vhost=example.test", use_auth=False))
        self.assertEqual((target.app, target.stream), ("app", "custom"))

    def test_rtmp_separate_key_query_and_plain_url(self):
        target = self.target(rtmp_server="rtmp://host/app", rtmp_key="custom?x=y")
        self.assertEqual(target.settings["key"], "custom?x=y")
        self.assertEqual(target.stream, "custom")
        target = self.target(rtmp="rtmp://host/app/custom")
        self.assertEqual(target.settings["key"], "custom")
        self.assertEqual(target.stream, "custom")

    def test_srt_cli_accepts_options(self):
        with patch.object(recipe, "resolve_args"), patch.object(recipe, "cmd_setup") as setup:
            with patch("sys.argv", ["obs_srs_test.py", "setup", "--srt", "srt://host:10080",
                                    "--srt-streamid", "#!::r=app/custom,m=publish",
                                    "--srt-passphrase", "example-passphrase"]):
                recipe.main()
        self.assertEqual(setup.call_args.args[0].srt_streamid, "#!::r=app/custom,m=publish")
        self.assertEqual(setup.call_args.args[0].srt_passphrase, "example-passphrase")


class VerifyTest(unittest.TestCase):
    def setUp(self):
        self.streams = []
        owner = self
        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                owner.assertEqual(self.path, "/api/v1/streams/")
                self.send_response(200)
                self.end_headers()
                self.wfile.write(json.dumps({"streams": owner.streams}).encode())

            def log_message(self, *args):
                pass
        self.server = HTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.api = "http://127.0.0.1:%d" % self.server.server_port
        self.output = io.StringIO()

    def tearDown(self):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()

    def live(self):
        self.streams = [{"app": "live", "name": "livestream", "publish": {"active": True},
                         "video": {"codec": "H264"}}]

    def verify(self, *extra):
        with contextlib.redirect_stdout(self.output):
            return recipe.main(["verify", "--srs-api", self.api, "--timeout", "0"] + list(extra))

    def test_missing_and_inactive_publish_exit_six(self):
        self.assertEqual(self.verify(), 6)
        self.streams = [{"app": "live", "name": "livestream", "publish": {"active": False}}]
        self.assertEqual(self.verify(), 6)

    def test_expect_inactive_success_and_failure(self):
        self.assertEqual(self.verify("--expect-inactive"), 0)
        self.live()
        self.assertEqual(self.verify("--expect-inactive"), 6)

    def test_explicit_http_origin_and_api_whep(self):
        self.live()
        self.assertEqual(self.verify("--http-url", "https://media.example.test:15180/base/"), 0)
        output = self.output.getvalue()
        self.assertIn("https://media.example.test:15180/base/live/livestream.flv", output)
        self.assertIn("https://media.example.test:15180/base/live/livestream.m3u8", output)
        self.assertIn(self.api + "/rtc/v1/whep/?app=live&stream=livestream", output)
        self.assertNotIn("rtmp://", output)
        self.assertNotIn(":8080", output)

    def test_default_http_port_is_reported(self):
        self.live()
        self.assertEqual(self.verify(), 0)
        self.assertIn("http://127.0.0.1:8080/live/livestream.flv", self.output.getvalue())
        self.assertIn("default", self.output.getvalue())

    def test_setup_verify_uses_http_origin_and_exit_six(self):
        with patch.object(recipe, "resolve_args"), patch.object(recipe, "cmd_setup") as setup:
            recipe.main(["setup", "--rtmp", "rtmp://127.0.0.1/live/livestream", "--start", "--verify",
                         "--srs-api", self.api, "--http-url", "http://media.example.test:15180"])
        args = setup.call_args.args[0]
        args.profile, args.collection, args.scene = "test", "test", "test"
        with contextlib.ExitStack() as stack:
            stack.enter_context(contextlib.redirect_stdout(self.output))
            stack.enter_context(patch.object(recipe.obsws, "connect", return_value=Mock()))
            for name in ("ensure_no_active_stream", "ensure_stream_free", "set_profile_params", "reload_profile",
                         "ensure_collection", "ensure_scene", "build_sources"):
                stack.enter_context(patch.object(recipe, name, return_value=[]))
            stack.enter_context(patch.object(recipe, "ensure_profile", return_value=["test"]))
            stack.enter_context(patch.object(recipe, "wait_stream", return_value={}))
            args.timeout = 0
            self.assertEqual(recipe.cmd_setup(args), 6)
            self.live()
            self.assertEqual(recipe.cmd_setup(args), 0)
        self.assertIn("http://media.example.test:15180/live/livestream.flv", self.output.getvalue())


class StreamRestartTest(unittest.TestCase):
    def run_action(self, action, active=True, stalled=False, changed=False, reconnecting=False):
        client = Mock()
        def call(name, *args):
            nonlocal active
            if name == "StopStream":
                active = False
            if name == "StartStream" and reconnecting:
                active = True
            if name == "GetStreamStatus":
                return {"outputActive": active}
            if name == "GetProfileList":
                return {"currentProfileName": "test", "profiles": ["other", "test"]}
            return {}
        client.call.side_effect = call
        args = argparse.Namespace(action=action, timeout=2)
        with patch.object(recipe.obsws, "connect", return_value=client), \
                patch.object(recipe.obsws, "dump"), \
                patch.object(recipe.time, "sleep") as sleep, \
                patch.object(recipe, "wait_stream") as wait, \
                patch.object(recipe, "stream_log_state", create=True) as logs:
            logs.side_effect = [0, 1 if changed else 0]
            wait.side_effect = ([{"outputActive": False}, recipe.obsws.TimeoutError_("stalled"),
                                 {"outputActive": True}] if stalled else
                                [{"outputActive": False}, {"outputActive": True}])
            recipe.cmd_stream(args)
        return client, sleep, wait

    def test_restart_stops_settles_and_starts(self):
        client, sleep, wait = self.run_action("restart")
        requests = [call.args[0] for call in client.call.call_args_list]
        self.assertIn("StopStream", requests)
        self.assertIn("StartStream", requests)
        self.assertLess(requests.index("StopStream"), requests.index("StartStream"))
        sleep.assert_called_once_with(1.0)
        self.assertEqual([call.kwargs["active"] for call in wait.call_args_list], [False, True])

    def test_start_inactive_waits_for_stop_to_settle(self):
        client, sleep, wait = self.run_action("start", active=False)
        sleep.assert_called_once_with(1.0)
        self.assertEqual([call.kwargs["active"] for call in wait.call_args_list], [False, True])

    def test_silent_stalled_restart_reloads_then_retries(self):
        client, _, _ = self.run_action("restart", stalled=True)
        requests = [call.args[0] for call in client.call.call_args_list]
        self.assertEqual(requests.count("StartStream"), 2)
        profiles = [call.args[1]["profileName"] for call in client.call.call_args_list
                    if call.args[0] == "SetCurrentProfile"]
        self.assertEqual(profiles, ["other", "test"])

    def test_failed_start_with_new_logs_is_not_retried(self):
        with self.assertRaises(recipe.obsws.TimeoutError_):
            self.run_action("restart", stalled=True, changed=True)

    def test_reconnecting_active_output_is_not_reloaded(self):
        with self.assertRaises(recipe.obsws.TimeoutError_):
            self.run_action("restart", stalled=True, reconnecting=True)


if __name__ == "__main__":
    unittest.main()
