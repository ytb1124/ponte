# Ponte

AES67 virtual soundcard for macOS, with SAP so Dante Controller can see its
streams. 64 channels in and out, a device clock locked to PTP, free and open
source.

*Ponte* (Italian, "bridge") is the passage in a piece of music that links one
section to the next. Here it links a Mac to an AES67 / Dante network.

Ponte is a fork of [AES67eleven](https://github.com/Gemini2350/aes67eleven) by
Gemini2350. It adds SAP announce and discovery on top of it. All the audio,
PTP and NMOS work comes from AES67eleven.

**[Download for macOS](https://github.com/ytb1124/ponte/releases/latest)** ·
[Verification status](SAP_STATUS.md) ([한국어](SAP_STATUS.ko.md))

> **Status:** Dante Controller lists Ponte's streams, and send/receive works
> between Ponte instances on one Mac. Audio exchange with **real Dante
> hardware has not been tested yet**. See [SAP_STATUS.md](SAP_STATUS.md) for
> exactly what was and was not verified.

## Features

- **64 × 64 Core Audio device** at 48 kHz. The clock follows the PTP
  grandmaster, so there is no sample-rate conversion in the audio path.
- **Up to 64 senders and 64 receivers**, up to 8 channels each (L24 or L16,
  1 ms), placed anywhere on the device channels.
- **SAP (RFC 2974)**: every sender is announced on 239.255.255.255:9875 so
  SAP-based tools such as Dante Controller list it. Streams announced by other
  devices show up in the web UI under **SAP streams** and connect with one
  click.
- **ST 2022-7** seamless redundancy on two interfaces.
- **NMOS**: IS-04, IS-05, IS-12, IS-13, BCP-008 sender and receiver monitors.
- **Web UI** with stream tiles, level meters and PTP status, plus a four-deck
  WAV/MP3 player and a local monitor output.

## Install on macOS

The installer is **not signed by Apple** (no paid developer certificate), so
macOS blocks it the first time. Install it one of these two ways.

**A. Finder**

1. Download `Ponte-<version>.pkg` from the
   [latest release](https://github.com/ytb1124/ponte/releases/latest).
2. Double-click it. macOS says it cannot verify the package. Click **Done**.
3. Open **System Settings → Privacy & Security**, scroll down, and click
   **Open Anyway** next to the Ponte message. Enter your password.
4. Follow the installer.

**B. Terminal**

```bash
sudo installer -pkg ~/Downloads/Ponte-0.1.0.pkg -target /
```

After installing:

1. Allow **Local Network** when macOS asks. Without it no multicast arrives.
2. Open `http://localhost:8180`, or use the menu bar icon.
3. Select **Ponte** as the audio device in your app.

It needs macOS 13 or later and runs on Apple Silicon and Intel. Use wired
Ethernet: PTP does not work over Wi-Fi. Installing restarts Core Audio, which
interrupts all system audio for a moment. A PTP grandmaster must be on the
network (for example a Dante device in AES67 mode); Ponte cannot be the clock
leader itself.

If AES67eleven is installed too, uninstall it first: both use web port 8180.

To uninstall:

```bash
bash /usr/local/share/ponte/uninstall.sh
```

To build from source instead (Xcode Command Line Tools and CMake):

```bash
./scripts/install.sh      # build and install on this Mac
./scripts/package.sh      # build dist/Ponte-<version>.pkg
```

## Bench test without hardware

[`tools/ptp_test_gm.py`](tools/ptp_test_gm.py) is a minimal software PTP
grandmaster. It lets Ponte lock and stream on a single Mac with no AES67
hardware. It is for tests only, not a production clock.

```bash
python3 tools/ptp_test_gm.py <your-ethernet-ip>
```

## Where things are

| | |
|---|---|
| Web UI | `http://localhost:8180` |
| NMOS Node API | `http://<ip>:8081/x-nmos/node/v1.3/` |
| Config | `~/Library/Application Support/Ponte/config.json` |
| Log | `~/Library/Logs/Ponte/daemon.log` |

## Limitations

- 48 kHz only, and at most 8 channels per stream. Dante's AES67 flows are
  also handled as 8 channels per flow in practice, so send more channels as
  several streams.
- Not verified with real Dante hardware yet ([details](SAP_STATUS.md)).
- Slave-only software PTP without hardware timestamping.
- The daemon runs only while a user is logged in.
- The Linux / Docker gateway from AES67eleven is still in the tree but was not
  renamed or tested in this fork.

## More

- [Technical notes](TECHNICAL.md): architecture, stream API, packaging,
  conformance (inherited from AES67eleven).
- Upstream project: [Gemini2350/aes67eleven](https://github.com/Gemini2350/aes67eleven).

## License

MIT, see [LICENSE](LICENSE). Based on AES67eleven (MIT, © 2026 Gemini2350).
Bundled third-party components: civetweb (MIT), nlohmann/json (MIT), dr_wav
(public domain / MIT-0), minimp3 (CC0), libASPL (MIT, fetched by CMake).
