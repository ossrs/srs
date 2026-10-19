#!/bin/bash
# Usage: perf-source-check.sh <flv> <target video kbps>
#
# Measures how flat a source's video bitrate is and prints its flatness JSON.
# It exits 1 when the source is not flat:
#   - a whole second after the first leaves +-3% of the target, or
#   - the per-frame sd is over 3% of the frame mean.
# The first second and the first frame (the stream's first IDR) are reported,
# not judged; a looped source repeats them once per loop.
set -euo pipefail

flv="${1:?usage: perf-source-check.sh <flv> <target kbps>}"
target="${2:?usage: perf-source-check.sh <flv> <target kbps>}"
[[ -s "$flv" ]] || { echo "No source file: $flv" >&2; exit 1; }

version=$(ffmpeg -version | head -1)
audio=$(ffprobe -v error -select_streams a -show_entries packet=size -of csv=p=0 "$flv" |
    awk -F, '{b += $1 * 8} END {printf "%.0f", b}')
duration=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$flv")

ffprobe -v error -select_streams v -show_entries packet=pts_time,size -of csv=p=0 "$flv" |
    awk -F, -v target="$target" -v file="$flv" -v ffmpeg="$version" \
        -v audio_bits="$audio" -v duration="$duration" '
    $1 != "" && $2 != "" {
        s = int($1); bits[s] += $2 * 8; frames[s]++
        if (s > last) last = s
        f = $2 * 8 / 1000
        if (n == 0) first_frame = f
        else { fn++; ft += f; fq += f * f; if (f > fmax) fmax = f }
        n++
    }
    END {
        if (n < 2) { printf "{\"file\":\"%s\",\"flat\":false,\"reason\":\"no video\"}\n", file; exit 1 }
        lo = target * 0.97; hi = target * 1.03; reason = ""
        # The last second is judged only when it is whole.
        for (k = 1; k <= last; k++) {
            if (k == last && frames[k] < frames[1]) continue
            v = bits[k] / 1000; c++; t += v; q += v * v
            if (c == 1 || v < mn) mn = v
            if (v > mx) mx = v
            if ((v < lo || v > hi) && reason == "")
                reason = sprintf("second %d is %.1f kbps, outside %.1f-%.1f", k, v, lo, hi)
        }
        if (c == 0) { printf "{\"file\":\"%s\",\"flat\":false,\"reason\":\"under 2 seconds\"}\n", file; exit 1 }
        m = t / c; sd = sqrt((q / c - m * m > 0) ? q / c - m * m : 0)
        fm = ft / fn; fsd = sqrt((fq / fn - fm * fm > 0) ? fq / fn - fm * fm : 0)
        if (fsd > 0.03 * fm && reason == "")
            reason = sprintf("frame sd %.2f kbit is over 3%% of the mean %.2f", fsd, fm)
        flat = (reason == "") ? "true" : "false"
        printf "{\"file\":\"%s\",\"flat\":%s,\"reason\":\"%s\",\"target_kbps\":%s,\"band\":0.03,", file, flat, reason, target
        printf "\"duration\":%.3f,\"audio_kbps\":%.1f,", duration, audio_bits / duration / 1000
        printf "\"first_second_kbps\":%.1f,\"first_frame_kbit\":%.2f,", bits[0] / 1000, first_frame
        printf "\"second\":{\"count\":%d,\"min\":%.1f,\"max\":%.1f,\"mean\":%.2f,\"sd\":%.2f},", c, mn, mx, m, sd
        printf "\"frame\":{\"count\":%d,\"max\":%.2f,\"mean\":%.3f,\"sd\":%.3f},", fn, fmax, fm, fsd
        printf "\"ffmpeg\":\"%s\"}\n", ffmpeg
        exit (flat == "true") ? 0 : 1
    }'
