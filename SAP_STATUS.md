# SAP verification status

English · [한국어](SAP_STATUS.ko.md)

Ponte is a fork of AES67eleven that adds SAP (RFC 2974) announce and
discovery. The goal is that SAP-based tools such as Dante Controller can find
Ponte's streams. This page separates what was actually verified from what was
not.

> **Summary:** Receiving audio from real Dante hardware works. A Shure ULXD4D
> in AES67 mode was the PTP grandmaster, Ponte found its stream through SAP,
> and a DAW recorded the microphone from the Ponte device. **Sending audio
> from the Mac to a Dante device has not been tested yet**, because the
> device used has no receive channels.

## Legend

| Mark | Meaning |
|---|---|
| ✅ Verified | Run and the result was checked directly |
| 🟡 Partial | Checked only under some conditions; limits noted |
| ❌ Not verified | Not tested, or no hardware to test with |

## Test setup

**Single-Mac tests (sections 1 and 2), 2026-11-14 to 2026-11-15**, run under
the AES67eleven name before the rename to Ponte:

- macOS 15.8.1, one Mac, no external AES67 or Dante hardware
- Network: USB gigabit Ethernet adapter (`en7`)
- PTP grandmaster: [`tools/ptp_test_gm.py`](tools/ptp_test_gm.py) (software
  timestamps, test use only)

**Dante hardware tests (section 3), 2026-11-16 to 2026-11-17**, with Ponte
0.1.1 installed from the release package:

- Same Mac and USB adapter (`en7`), link-local addresses (169.254.x.x),
  direct cable, no switch
- Dante Controller on the same Mac
- Devices tried:

| Device | Dante platform | AES67 | Result |
|---|---|---|---|
| PreSonus AVB-D16 (Dante port) | Audinate DCM, router 4.0.1 | Not supported (no AES67 Config tab, PTPv1 only) | Cannot work with Ponte |
| Shure ULXD4D | Audinate DCM, router 4.2 | Supported, enabled | Used for the tests below |

## 1. Unit tests: ✅ verified

All three test programs in [`tests/sap/`](tests/sap/) pass. They were re-run
on 2026-11-15.

| Test | What it checks | Result |
|---|---|---|
| `sap_test` | SAP packet format (header, `application/sdp`, origin IP); periodic re-announce keeps the same hash; a content change sends a deletion of the old version, then the new one; a vanished stream gets a deletion | ✅ |
| `sap_listen_test` | Received announcements are listed; a new version of the same session replaces the entry; malformed packets are ignored; a deletion removes the entry | ✅ |
| `refclk_test` | The grandmaster id in `a=ts-refclk` is written in RFC 7273 form (hyphens, upper case) and parses back | ✅ |

Build and run (from the repository root):

```bash
for t in sap_test sap_listen_test refclk_test; do
  clang++ -std=c++17 -Icommon -Ithird_party tests/sap/$t.cpp \
    common/rtp/sap.cpp common/rtp/sdp.cpp common/compat/net_compat.cpp \
    common/compat/time_compat.cpp common/log.cpp -o /tmp/$t && /tmp/$t
done
```

These tests run on loopback (`lo0`) only. They do not touch a real network or
other devices.

## 2. Integration on a single Mac

| Item | Status | Notes |
|---|---|---|
| Build and install (`scripts/install.sh`) | ✅ | Fixed the script stopping silently when no signing certificate exists |
| Daemon sends SAP announcements on the real Ethernet port | ✅ | The first send failed before the macOS "Local Network" permission was granted; it worked once allowed. Failed sends retry after 1 s |
| PTP lock to the test grandmaster | ✅ | With `ptp_test_gm.py` |
| Sender (2-channel test tone, `239.69.145.10`) | ✅ | |
| The receiver discovers the node's own SAP announcement | ✅ | Shown in the web UI **SAP streams** card |
| Connecting a receiver to a SAP-discovered stream (raw SDP / `sap_key` / web UI Connect button) | ✅ | All three methods reached status Healthy |
| Received audio | ✅ | Both channels at -18.0 dB (the tone level sent), 0 lost and 0 concealed packets |

## 3. Dante compatibility

| Item | Status | Notes |
|---|---|---|
| Dante Controller lists Ponte's stream | ✅ | "Ponte AES67 Out ch 1 2 3 4 5 6 7 8" shown as a transmitter next to the Shure device |
| Ponte locks to a Dante device as PTP grandmaster | ✅ | Locked to the ULXD4D (`00:0e:dd:ff:fe:f0:04:66`, domain 0) over PTPv2. Offset stayed between -489 µs and +9 µs (software timestamps) |
| Ponte parses SAP announcements sent by a Dante device | ✅ | "ULXD4D-f00466 : 32", 2 ch L24/48000, 1 ms, `239.69.69.131`, with `a=ts-refclk` and `a=mediaclk` |
| Audio from Dante device → Mac | ✅ | Connected with `sap_key`. Speech arrived at -24 to -40 dB and a DAW recorded it from the Ponte input |
| 10-minute stability, Dante device → Mac | 🟡 | 598,561 packets, 0 lost, 0 concealed, PTP stayed locked. The microphone was mostly off, so this checks packets and clock, not audible clicks. The device read counter showed 19 underruns since daemon start; whether they happened during this run was not separated |
| Audio from Mac → Dante device | ❌ | The ULXD4D has no Dante receive channels. Needs a device that can receive |
| The stream disappears from Dante Controller when stopped | ❌ | Deletion sending is only covered by the unit test |
| Longer runs (30 to 60 minutes and more) | ❌ | |
| Devices without AES67 support | ✅ (as expected) | The AVB-D16 sends only PTPv1 and no SAP, so Ponte cannot lock or see streams. This is a device limit, not a Ponte bug |

## 4. Not tested at all

- Several streams announced at once (for example 4 × 8 channels = 32)
- Redundant (ST 2022-7 / DUP) streams. The code announces each interface's leg
  separately, but it was never run with two Ethernet ports
- Other SAP-based devices (RAVENNA, Livewire+ and others)
- Transfer between two Macs, and networks with a managed switch (IGMP snooping)
- Docker / Linux build

## 5. Next test steps

1. With a Dante device that has receive channels and AES67 enabled, route
   Ponte's stream to it in Dante Controller and listen.
2. Stop a Ponte sender and check that it leaves Dante Controller's list.
3. Run 30 to 60 minutes with continuous audio, listen for clicks, and track
   the device underrun counter as a difference over the run.
4. Repeat through a managed switch with IGMP snooping.

Notes:

- Dante Controller shows an **AES67 Config** tab only on devices that
  support AES67. Turn AES67 mode on there and reboot the device.
- In practice Dante AES67 flows are planned as 8 channels per flow. Send more
  channels as several flows.
