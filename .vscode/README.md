# Debug with VSCode

Support run and debug with VSCode.

## SRS

Install the following extensions:

- CMake Tools
- CodeLLDB
- C/C++ Extension Pack

Open the folder like `~/git/srs` in VSCode.
Run commmand `> CMake: Configure` to configure the project.

> Note: You can press `Ctrl+R`, then type `CMake: Configure` then select `Clang` as the toolchain.

> Note: The `settings.json` is used to configure the cmake. It will use `${workspaceFolder}/trunk/ide/srs_clion/CMakeLists.txt` 
> and `${workspaceFolder}/trunk/ide/vscode-build` as the source file and build directory.

Click the `Run > Run Without Debugging` button to start the server.

> Note: The `launch.json` is used for running and debugging. The build will output the binary to
> `${workspaceFolder}/trunk/ide/vscode-build/srs`.

## Proxy

Install the following extensions:

- Go

Open the folder like `~/git/srs` in VSCode.

Select the `View > Run` and select `Launch srs-proxy` to start the proxy server.

Click the `Run > Run Without Debugging` button to start the server.

> Note: The `launch.json` is used for running and debugging.
