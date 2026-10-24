import base64
import os
import io
import shutil
import subprocess
import tempfile
import unittest
from argparse import Namespace
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import patch

import obsws

REAL_RUN = subprocess.run
# Windows has no /tmp; its temp dir stands in there.
TMP = tempfile.gettempdir() if os.name == "nt" else "/tmp"


class ProcessSafetyTest(unittest.TestCase):
    def launch(self, instance=None, keep=False):
        output = io.StringIO()
        with patch.object(obsws, "INSTANCE", instance), \
                patch.object(obsws, "obs_pids", return_value=[]), \
                patch.object(obsws, "all_obs_pids", return_value=[]), \
                patch.object(obsws, "find_obs_binary", return_value=("obs", None)), \
                patch.object(obsws, "stale_sentinels", return_value=["sentinel-one", "sentinel-two"]), \
                patch.object(obsws, "config_dir", return_value="config"), \
                patch.object(obsws, "seed_instance", return_value=15000), \
                patch.object(obsws, "instance_env", return_value={}), \
                patch.object(obsws, "build_launch_args", return_value=[]), \
                patch.object(obsws, "read_websocket_config", return_value={"server_enabled": True}), \
                patch.object(obsws, "system", return_value="Windows"), \
                patch.object(obsws, "link_windows_instance", return_value="obs"), \
                patch.object(obsws, "start_windows_instance", return_value=4321), \
                patch.object(obsws.subprocess, "Popen"), \
                patch("builtins.open", unittest.mock.mock_open()), \
                patch.object(obsws.os, "remove") as remove, redirect_stdout(output):
            self.assertEqual(obsws.cmd_launch(Namespace(keep_sentinel=keep)), 0)
        return remove, output.getvalue()

    def test_main_launch_preserves_and_lists_crash_sentinels(self):
        remove, output = self.launch()
        remove.assert_not_called()
        self.assertIn("sentinel-one", output)
        self.assertIn("sentinel-two", output)
        self.assertIn("crash", output)
        self.assertIn("remove", output)

    def test_instance_launch_clears_only_its_sentinels(self):
        # Accepted behavior: isolated test instances clear their own recovery state.
        remove, output = self.launch(instance="t12345")
        self.assertEqual(remove.call_args_list,
                         [unittest.mock.call("sentinel-one"), unittest.mock.call("sentinel-two")])
        self.assertIn("removed 2", output)

    def test_keep_sentinel_preserves_instance_recovery_state(self):
        # Accepted behavior: the existing opt-out also applies to test instances.
        remove, _ = self.launch(instance="t12345", keep=True)
        remove.assert_not_called()

    def test_windows_quit_addresses_each_selected_pid(self):
        with patch.object(obsws, "system", return_value="Windows"), \
                patch.object(obsws, "obs_pids", return_value=[123, 456]), \
                patch.object(obsws, "connect", side_effect=obsws.OBSError("offline")), \
                patch.object(obsws, "pid_is_obs", return_value=False), \
                patch.object(obsws.subprocess, "run") as run, redirect_stdout(io.StringIO()):
            self.assertEqual(obsws.cmd_quit(Namespace(timeout=1)), 0)
        self.assertEqual(run.call_args_list, [
            unittest.mock.call(["taskkill", "/PID", "123"], capture_output=True, timeout=15),
            unittest.mock.call(["taskkill", "/PID", "456"], capture_output=True, timeout=15)])


class DisplayPropertiesTest(unittest.TestCase):
    def test_display_uuid_refused_before_connection(self):
        error = io.StringIO()
        with patch.object(obsws, "connect") as connect, redirect_stderr(error):
            connect.return_value.request.return_value = {"requestStatus": {"result": False}}
            result = obsws.main(["call", "GetInputPropertiesListPropertyItems",
                                 '{"inputName":"screen","propertyName":"display_uuid"}'])
        self.assertEqual(result, 1)
        connect.assert_not_called()
        self.assertIn("CoreGraphics", error.getvalue())
        self.assertIn("--force", error.getvalue())

    def test_force_sends_display_uuid_request(self):
        with patch.object(obsws, "connect") as connect, redirect_stdout(io.StringIO()):
            connect.return_value.request.return_value = {"requestStatus": {"result": True}}
            result = obsws.main(["call", "--force", "GetInputPropertiesListPropertyItems",
                                 '{"inputName":"screen","propertyName":"display_uuid"}'])
        self.assertEqual(result, 0)
        connect.return_value.request.assert_called_once_with(
            "GetInputPropertiesListPropertyItems", {"inputName": "screen", "propertyName": "display_uuid"})
        connect.return_value.close.assert_called_once_with()

    def test_other_properties_and_requests_still_work(self):
        # Accepted behavior: only the known dangerous request/property pair is guarded.
        for request, data in (("GetInputPropertiesListPropertyItems", '{"propertyName":"device"}'),
                              ("GetInputSettings", '{"propertyName":"display_uuid"}'),
                              ("GetVersion", None)):
            with self.subTest(request=request), patch.object(obsws, "connect") as connect, redirect_stdout(io.StringIO()):
                connect.return_value.request.return_value = {"requestStatus": {"result": True}}
                self.assertEqual(obsws.main(["call", request] + ([data] if data else [])), 0)
                connect.return_value.request.assert_called_once()


class InstancePortsTest(unittest.TestCase):
    def setUp(self):
        os.mkdir(os.path.join(TMP, "srs-obs-ports-unit"))
        self.addCleanup(shutil.rmtree, os.path.join(TMP, "srs-obs-ports-unit"))
        self.env = patch.dict(os.environ, {"OBS_CONTROL_HOME": os.path.join(TMP, "srs-obs-ports-unit")}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.main = patch.object(obsws, "main_config_dir", return_value=os.path.join(TMP, "srs-obs-ports-unit", "main"))
        self.main.start()
        self.addCleanup(self.main.stop)
        self.free = patch.object(obsws, "port_free", return_value=True)
        self.free.start()
        self.addCleanup(self.free.stop)

    def test_default_range(self):
        # Accepted compatibility behavior: unset preserves the human-use defaults.
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 4460)

    def test_custom_range_allocates_distinct_ports_under_lock(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15099"
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15000)
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15001)

    def test_exhaustion_names_requested_range(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15000"
        obsws.seed_instance(obsws.new_instance())
        with self.assertRaisesRegex(obsws.LocalStateError, "no free websocket port in 15000-15000"):
            obsws.seed_instance(obsws.new_instance())

    def test_skips_occupied_port(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15002"
        with patch.object(obsws, "port_free", side_effect=lambda port: port != 15000):
            self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15001)

    def test_skips_main_obs_port(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15002"
        obsws.write_websocket_config({"server_port": 15000}, obsws.main_config_dir())
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15001)

    def test_relaunch_preserves_existing_port(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15002"
        instance = obsws.new_instance()
        self.assertEqual(obsws.seed_instance(instance), 15000)
        os.environ["OBS_CONTROL_WS_PORTS"] = "15100-15102"
        self.assertEqual(obsws.seed_instance(instance), 15000)

    def test_invalid_ranges_fail_clearly(self):
        for value in ("", "15000", "x-y", "15002-15000", "0-1", "65535-65536", "-1-2"):
            with self.subTest(value=value):
                os.environ["OBS_CONTROL_WS_PORTS"] = value
                with self.assertRaisesRegex(obsws.LocalStateError, "OBS_CONTROL_WS_PORTS"):
                    obsws.seed_instance(obsws.new_instance())


class WindowsInstanceTest(unittest.TestCase):
    ROOT = os.path.join(TMP, "srs-obs-windows-unit")

    def setUp(self):
        os.mkdir(self.ROOT)
        self.addCleanup(shutil.rmtree, self.ROOT)
        for p in (patch.dict(os.environ, {"OBS_CONTROL_HOME": self.ROOT}, clear=True),
                  patch.object(obsws, "system", return_value="Windows"),
                  patch.object(obsws, "main_config_dir", return_value=os.path.join(self.ROOT, "main")),
                  patch.object(obsws, "port_free", return_value=True)):
            p.start()
            self.addCleanup(p.stop)
        self.addCleanup(obsws.set_instance, None)
        self.install = os.path.join(self.ROOT, "install", "obs-studio")
        self.binary = os.path.join(self.install, "bin", "64bit", "obs64.exe")
        for sub in obsws.WINDOWS_INSTALL_LINKS:
            os.makedirs(os.path.join(self.install, sub))
            with open(os.path.join(self.install, sub, "keep"), "w") as f:
                f.write("install file\n")
        self.calls = []

    def fake_run(self, powershell_rc=0, powershell_out="4321\n", rmdir_rc=0):
        def run(argv, **kwargs):
            self.calls.append(argv)
            if argv[:2] == ["cmd", "/c"] and argv[2] == "rmdir" and rmdir_rc:
                return subprocess.CompletedProcess(argv, rmdir_rc, "", "Access is denied.")
            if argv[:4] == ["cmd", "/c", "mklink", "/J"]:
                if os.name == "nt":
                    import _winapi
                    _winapi.CreateJunction(argv[5], argv[4])  # a real junction on Windows
                else:
                    os.symlink(argv[5], argv[4])
            elif argv[:3] == ["cmd", "/c", "rmdir"]:
                if os.name == "nt":
                    os.rmdir(argv[3])  # removes the junction, not its target
                else:
                    os.unlink(argv[3])
            elif argv[0] == "powershell":
                return subprocess.CompletedProcess(argv, powershell_rc, powershell_out, "")
            return subprocess.CompletedProcess(argv, 0, "", "")
        return run

    def scripts(self):
        return [base64.b64decode(argv[argv.index("-EncodedCommand") + 1]).decode("utf-16-le")
                for argv in self.calls if argv[0] == "powershell"]

    def launch(self, **run):
        name = obsws.new_instance()
        obsws.set_instance(name)
        with patch.object(obsws, "find_obs_binary", return_value=(self.binary, os.path.dirname(self.binary))), \
                patch.object(obsws, "obs_pids", return_value=[]), \
                patch.object(obsws, "all_obs_pids", return_value=[]), \
                patch.object(obsws.subprocess, "run", side_effect=self.fake_run(**run)), \
                patch.object(obsws.subprocess, "Popen") as popen, redirect_stdout(io.StringIO()):
            obsws.cmd_launch(Namespace(minimize_to_tray=True, wait=0))
        popen.assert_not_called()
        return name

    def test_instances_are_allowed(self):
        obsws.set_instance("t12345")
        self.assertEqual(obsws.INSTANCE, "t12345")

    def test_config_is_the_portable_config_of_the_instance(self):
        self.assertEqual(obsws.instance_config_dir("t12345"),
                         os.path.join(self.ROOT, "instances", "t12345", "config", "obs-studio"))
        env = obsws.instance_env("t12345")
        self.assertNotIn("XDG_CONFIG_HOME", env)
        self.assertNotIn("CFFIXED_USER_HOME", env)

    def test_launch_links_the_install_and_starts_portable_obs_on_the_desktop(self):
        name = self.launch()
        inst = os.path.normpath(obsws.instance_dir(name))
        exe = os.path.join(inst, "bin", "64bit", "obs64.exe")
        links = [c for c in self.calls if c[:2] == ["cmd", "/c"]]
        self.assertEqual(links, [["cmd", "/c", "mklink", "/J", os.path.join(inst, sub), os.path.normpath(os.path.join(self.install, sub))]
                                 for sub in obsws.WINDOWS_INSTALL_LINKS])
        script, = self.scripts()
        for text in (exe, "--portable", "--multi", "--minimize-to-tray", "--disable-updater",
                     "-WorkingDirectory '%s'" % os.path.dirname(exe), "-LogonType Interactive",
                     "[Security.Principal.WindowsIdentity]::GetCurrent().Name", "Start-ScheduledTask",
                     "internal-obs-control-" + name):
            self.assertIn(text, script)
        with open(os.path.join(inst, "obs.pid")) as f:
            self.assertEqual(f.read().strip(), "4321")
        self.assertTrue(os.path.isfile(os.path.join(inst, "config", "obs-studio", "plugin_config", "obs-websocket", "config.json")))

    def test_launch_without_a_desktop_session_fails_clearly(self):
        with self.assertRaisesRegex(obsws.LocalStateError, "desktop"):
            self.launch(powershell_rc=3, powershell_out="")

    def test_pid_check_reads_tasklist_and_never_signals(self):
        rows = {"4321": '"obs64.exe","4321","Console","1","100,000 K"\n',
                "99": '"notepad.exe","99","Console","1","9,000 K"\n',
                "7": "INFO: No tasks are running which match the specified criteria.\n"}

        def run(argv, **kwargs):
            return subprocess.CompletedProcess(argv, 0, rows[argv[argv.index("/FI") + 1].split()[-1]], "")
        with patch.object(obsws.subprocess, "run", side_effect=run), patch.object(obsws.os, "kill") as kill:
            self.assertTrue(obsws.pid_is_obs(4321))
            self.assertFalse(obsws.pid_is_obs(99))
            self.assertFalse(obsws.pid_is_obs(7))
        kill.assert_not_called()

    def test_force_quit_uses_taskkill(self):
        with patch.object(obsws, "obs_pids", return_value=[123]), \
                patch.object(obsws, "connect", side_effect=obsws.OBSError("offline")), \
                patch.object(obsws, "pid_is_obs", return_value=True), \
                patch.object(obsws.time, "sleep"), \
                patch.object(obsws.os, "kill") as kill, \
                patch.object(obsws.subprocess, "run") as run, redirect_stdout(io.StringIO()):
            self.assertEqual(obsws.cmd_quit(Namespace(timeout=0, force=True)), 0)
        kill.assert_not_called()
        self.assertIn(unittest.mock.call(["taskkill", "/F", "/PID", "123"], capture_output=True, timeout=15),
                      run.call_args_list)

    def test_instance_quit_closes_obs_from_the_desktop(self):
        obsws.set_instance("t12345")
        with patch.object(obsws, "obs_pids", return_value=[123]), \
                patch.object(obsws, "connect", side_effect=obsws.OBSError("offline")), \
                patch.object(obsws, "pid_is_obs", return_value=False), \
                patch.object(obsws.subprocess, "run", side_effect=self.fake_run(powershell_out="")), \
                redirect_stdout(io.StringIO()):
            self.assertEqual(obsws.cmd_quit(Namespace(timeout=1)), 0)
        script, = self.scripts()
        for text in ("taskkill.exe", "'/PID 123'", "-LogonType Interactive", "Start-ScheduledTask",
                     "Unregister-ScheduledTask", "internal-obs-control-t12345-quit"):
            self.assertIn(text, script)
        self.assertNotIn(["taskkill", "/PID", "123"], self.calls)

    def remove(self, name, **run):
        with patch.object(obsws, "obs_pids", return_value=[]), \
                patch.object(obsws.subprocess, "run", side_effect=self.fake_run(**run)), redirect_stdout(io.StringIO()):
            return obsws.main(["instance", "remove", "--instance", name, "--force", "--timeout", "1"])

    def test_remove_unlinks_the_install_before_deleting_the_instance(self):
        name = self.launch()
        self.calls = []
        self.assertEqual(self.remove(name), 0)
        inst = os.path.normpath(obsws.instance_dir(name))
        self.assertFalse(os.path.exists(inst))
        self.assertEqual([c for c in self.calls if c[:2] == ["cmd", "/c"]],
                         [["cmd", "/c", "rmdir", os.path.join(inst, sub)] for sub in obsws.WINDOWS_INSTALL_LINKS])
        self.assertTrue(any("Unregister-ScheduledTask" in s and "internal-obs-control-" + name in s for s in self.scripts()))
        for sub in obsws.WINDOWS_INSTALL_LINKS:
            self.assertTrue(os.path.isfile(os.path.join(self.install, sub, "keep")))

    def test_remove_keeps_the_instance_when_a_link_stays(self):
        name = self.launch()
        self.assertNotEqual(self.remove(name, rmdir_rc=5), 0)
        self.assertTrue(os.path.isdir(obsws.instance_dir(name)))
        for sub in obsws.WINDOWS_INSTALL_LINKS:
            self.assertTrue(os.path.isfile(os.path.join(self.install, sub, "keep")))


if __name__ == "__main__":
    unittest.main()
