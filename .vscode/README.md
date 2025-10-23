# Debug with VSCode

Support run and debug with VSCode.

## macOS: SRS Server

Install the following extensions:

- CMake Tools
- CodeLLDB
- C/C++ Extension Pack

Open the folder like `$HOME/git/srs` in VSCode, after you clone srs to
`$HOME/git/srs` directory.

Run commmand to configure the project by pressing `Command+Shift+P`, then type `CMake: Configure` 
then select `Clang` as the toolchain. Or run the command manually in terminal:

```bash
cmake -S $HOME/git/srs/trunk/cmake -B $HOME/git/srs/trunk/cmake/build
```

> Note: Sometimes it may fail to configure when building libsrtp. Just retry, and it will succeed.

> Note: Make sure you have `xcode` installed, and run `xcode-select --install` to setup the toolchains.

> Note: The `settings.json` is used to configure the cmake. It will use `$HOME/git/srs/trunk/cmake/CMakeLists.txt` 
> and `$HOME/git/srs/trunk/cmake/build` as the source file and build directory.

Click the `Run > Run Without Debugging` or `Run > Start Debugging` from menu to start or 
debug the server. It will invoke the `build` task defined in `tasks.json`, or you can run 
it manually:

```bash
cmake --build $HOME/git/srs/trunk/cmake/build
```

> Note: The `launch.json` is used for running and debugging. The build will output the binary to
> `$HOME/git/srs/trunk/cmake/build/srs`.

## macOS: SRS UTest

Install the following extensions:

- C++ TestMate

Open the folder like `$HOME/git/srs` in VSCode, after you clone srs to
`$HOME/git/srs` directory.

Run commmand to configure the project by pressing `Command+Shift+P`, then type `CMake: Configure` 
then select `Clang` as the toolchain. Or run the command manually in terminal:

```bash
cmake -S $HOME/git/srs/trunk/cmake -B $HOME/git/srs/trunk/cmake/build
```

> Note: Sometimes it may fail to configure when building libsrtp. Just retry, and it will succeed.

Afterwards, build the utest by pressing `Command+Shift+P`, then type `CMake: Build` to run the 
build command. It will invoke the `build` task defined in `tasks.json`, or you can run it manually:

```bash
cmake --build $HOME/git/srs/trunk/cmake/build
```

Then you will discover all the unit testcases from the `View > Testing` panel. You can 
open utest source file like `trunk/src/utest/srs_utest.cpp`, then click the `Run Test` or `Debug Test`
on each testcase such as `FastSampleInt64Test`.

## macOS: SRS Regression Test

Follow the [srs-bench](../trunk/3rdparty/srs-bench/README.md) to setup the environment.

Open the test panel by clicking `View > Testing`, run the regression tests under:

```
+ Go
  + github.com/ossrs/srs-bench
    + blackbox
    + gb28181
    + srs
```

## macOS: Proxy

Install the following extensions:

- Go

Open the folder like `~/git/srs` in VSCode.

Click the `View > Run` and select `Launch srs-proxy` to start the proxy server.

Click the `Run > Run Without Debugging` button to start the server.

> Note: The `launch.json` is used for running and debugging.
