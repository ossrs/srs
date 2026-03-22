# How to Run and Test the Project

When running the project for testing or development, you should:
1. Build and start the proxy server
2. Start SRS origin server
3. Verify SRS registration with proxy
4. Publish a test stream using FFmpeg
5. Verify the stream is working using ffprobe

## Step 1: Build and Start Proxy Server

```bash
make && env PROXY_RTMP_SERVER=1935 PROXY_HTTP_SERVER=8080 \
    PROXY_HTTP_API=1985 PROXY_WEBRTC_SERVER=8000 PROXY_SRT_SERVER=10080 \
    PROXY_SYSTEM_API=12025 PROXY_LOAD_BALANCER_TYPE=memory ./srs-proxy
```

The proxy server should start and listen on the configured ports.

## Step 2: Start SRS Origin Server

In a new terminal, start the SRS origin server. You may need to increase the file descriptor limit and use bash explicitly:

```bash
ulimit -n 10000 && bash -c "cd ~/git/srs/trunk && ./objs/srs -c conf/origin1-for-proxy.conf"
```

The SRS origin server should start and be ready to receive and serve streams. Check the console output for startup messages.

## Step 3: Verify SRS Registration

Check the proxy logs to confirm SRS has registered itself with the proxy:

The proxy logs are printed to the console where you started the proxy server. Check the terminal running the proxy for messages indicating:
- "Register SRS media server" messages when SRS registers itself with the proxy

The SRS origin server should automatically register itself with the proxy when it starts. Look for successful registration messages in proxy console outputs.

## Step 4: Publish a Test Stream

In a new terminal, publish a test stream using FFmpeg:

```bash
ffmpeg -stream_loop -1 -re -i ~/git/srs/trunk/doc/source.flv -c copy -f flv rtmp://localhost/live/livestream
```

> Note: `-stream_loop -1` makes FFmpeg loop the input file infinitely, ensuring the stream doesn't quit after the file ends.

## Step 5: Verify Stream with ffprobe

In another terminal, use ffprobe to verify the stream is working:

**Test RTMP stream:**
```bash
ffprobe rtmp://localhost/live/livestream
```

**Test HTTP-FLV stream:**
```bash
ffprobe http://localhost:8080/live/livestream.flv
```

Both commands should successfully detect the stream and display video/audio codec information. If ffprobe shows stream details without errors, the proxy is working correctly.

## Code Conventions

## Factory Functions
- Factory functions should use explicit interface names: `NewBootstrap()`, `NewMemoryLoadBalancer()`, etc.
- **Do not** use generic `New()` function names
- This improves code clarity and makes the constructed type explicit at the call site
- Example:
  ```go
  // Good
  bs := bootstrap.NewBootstrap()

  // Avoid
  bs := bootstrap.New()
  ```

## Global Variables
- Avoid global variables for service instances
- This improves testability and makes code flow explicit
