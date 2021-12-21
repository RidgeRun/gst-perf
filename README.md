# GstPerf

A GStreamer element to measure framerate, bitrate and CPU usage

## Build Instructions

On Debian-based systems:
```bash
./autogen.sh
./configure --prefix /usr/ --libdir /usr/lib/x86_64-linux-gnu/
make
sudo make install
```

Configure options may vary according to your specific system.

## Usage

Just link in the `perf` element wherever you want to take the
measurements from. For example, record an MP4 file and print
framerate, bitrate and CPU usage measurements at the output of the
encoder:

```bash
gst-launch-1.0 -e videotestsrc ! x264enc ! perf ! qtmux print-arm-load=true ! filesink location=test.mp4
```

## Building a Debian package

1. Install build dependencies (one-time step):
   `sudo apt install -y debhelper devscripts`
2. After cloning, run `dpkg-buildpackage -us -uc` in the source directory.
   The package will be left in the parent directory.

## Legal

Copyright (c) 2019 RidgeRun, LLC.

Portions copyright (c) 2020 D3 Engineering, LLC.

Licensed LGPL2+ (LGPL-2.0-or-later); see file [LICENSE](LICENSE) for details.
