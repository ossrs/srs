#!/bin/bash
# Helper for the proxy-e2e-*.sh scripts, not a standalone test. Source it, then start
# SRS origin 1 or 2 with environment variables only (-e), the same setup as
# trunk/conf/origin1-for-proxy.conf and trunk/conf/origin2-for-proxy.conf.
#
# Run it in the background from trunk/, so $! is the SRS process:
#   srs_proxy_origin 1 [NAME=value ...] >/tmp/origin.log 2>&1 &
# Extra NAME=value arguments add or overwrite variables, such as
# SRS_RTC_SERVER_CANDIDATE=127.0.0.1.
# Every port follows PORT_BASE, the SRS_TEST_PORT_BASE of the script that sources this helper.

srs_proxy_origin() {
  local index="$1"
  shift
  local port_base=$PORT_BASE

  local base
  case "$index" in
    1) base=$((port_base + 20)) ;;
    2) base=$((port_base + 30)) ;;
    *) echo "Error: unknown proxy origin $index" >&2; return 1 ;;
  esac

  exec env SRS_RTMP_LISTEN=$base \
    SRS_HTTP_SERVER_ENABLED=on SRS_HTTP_SERVER_LISTEN=$((base + 1)) \
    SRS_HTTP_API_ENABLED=on SRS_HTTP_API_LISTEN=$((base + 2)) \
    SRS_RTC_SERVER_ENABLED=on SRS_RTC_SERVER_LISTEN=$((base + 3)) \
    SRS_SRT_SERVER_ENABLED=on SRS_SRT_SERVER_LISTEN=$((base + 4)) \
    SRS_SRT_SERVER_TSBPDMODE=off SRS_SRT_SERVER_TLPKTDROP=off \
    SRS_HEARTBEAT_ENABLED=on SRS_HEARTBEAT_INTERVAL=9 \
    SRS_HEARTBEAT_URL=http://127.0.0.1:$((port_base + 5))/api/v1/srs/register \
    SRS_HEARTBEAT_DEVICE_ID=origin$index SRS_HEARTBEAT_PORTS=on \
    SRS_VHOST_HTTP_REMUX_ENABLED=on SRS_VHOST_HLS_ENABLED=on \
    SRS_VHOST_RTC_ENABLED=on SRS_VHOST_RTC_RTMP_TO_RTC=on SRS_VHOST_RTC_RTC_TO_RTMP=on \
    SRS_VHOST_SRT_ENABLED=on SRS_VHOST_SRT_SRT_TO_RTMP=on \
    "$@" ./objs/srs -e
}
