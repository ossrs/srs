# Visual Studio

`srs.slnx` is the Visual Studio solution for SRS on Windows, to build, run and debug `srs.exe` in the
IDE. It needs Visual Studio 2022 17.13 or later.

1. Generate the projects it opens, in Git Bash with the MSVC x64 environment:
   * From the Start menu, open the "x64 Native Tools Command Prompt" of your Visual Studio, such as
     "x64 Native Tools Command Prompt for VS 2026". It runs `vcvars64.bat`, which sets the paths of
     the compiler, CMake and vcpkg.
   * In it, start Git Bash with its full path, which keeps that environment. Plain `bash` may start
     WSL instead:

     ```bat
     "C:\Program Files\Git\bin\bash.exe"
     ```

   * In Git Bash, go to the root of the checkout and run the `vs` step:

     ```bash
     cd ~/projects/srs
     bash skills/srs-develop/scripts/srs-windows-runner.sh vs
     ```

2. Open `.vs\srs.slnx` in Visual Studio.
3. Build the `Debug` configuration, then press F5 to debug `srs.exe -c conf/console.conf`.

The tools to install, the utest, and how to debug the ST coroutine switch are in
[Visual Studio](../skills/internal-docs-for-srs/references/cpp-docs/doc/windows.md#visual-studio).
