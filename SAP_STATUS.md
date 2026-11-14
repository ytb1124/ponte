# SAP verification status

English · [한국어](SAP_STATUS.ko.md)

Ponte is a fork of AES67eleven that adds SAP (RFC 2974) announce and
discovery. The goal is that SAP-based tools such as Dante Controller can find
Ponte's streams. This page separates what was actually verified from what was
not.

All tests below were run before the rename to Ponte, under the AES67eleven
name. After the rename, only the unit tests and the installer build were
re-checked.

> **Summary:** Behaviour on a single Mac and Dante Controller listing the
> stream are verified. **Audio exchange with real Dante hardware has never
> been tested.**

## Legend

| Mark | Meaning |
|---|---|
| ✅ Verified | Run and the result was checked directly |
| 🟡 Partial | Checked only under some conditions; limits noted |
| ❌ Not verified | Not tested, or no hardware to test with |

## Test setup

- macOS 15.8.1, one Mac (no external AES67 or Dante hardware)
- Network: USB gigabit Ethernet adapter (`en7`)
- PTP grandmaster: [`tools/ptp_test_gm.py`](tools/ptp_test_gm.py) instead of
  real hardware (software timestamps, test use only)
- Dante Controller running on the same Mac
- Test dates: 2026-11-14 to 2026-11-15

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
| PTP lock to the test grandmaster | ✅ | With `ptp_test_gm.py` only. Not checked against real PTP hardware |
| Sender (2-channel test tone, `239.69.145.10`) | ✅ | |
| The receiver discovers the node's own SAP announcement | ✅ | Shown in the web UI **SAP streams** card |
| Connecting a receiver to a SAP-discovered stream (raw SDP / `sap_key` / web UI Connect button) | ✅ | All three methods reached status Healthy |
| Received audio | ✅ | Both channels at -18.0 dB (the tone level sent), 0 lost and 0 concealed packets |
| Long-run stability | ❌ | Only ran for a few minutes |

## 3. Dante compatibility

| Item | Status | Notes |
|---|---|---|
| Dante Controller lists the stream | ✅ | "AES67eleven SAP test" shown as a 2-channel transmitter |
| Dante Controller sees a PTP leader clock | ❌ | No Dante device; it showed the leader clock as unknown |
| Ponte locks to a Dante device as PTP grandmaster | ❌ | Ponte is slave-only and cannot be the clock leader, so this is the first thing to check |
| Audio from Mac → Dante device | ❌ | No Dante hardware |
| Audio from Dante device → Mac | ❌ | No Dante hardware |
| Ponte parses SAP announcements sent by a Dante device | ❌ | The format follows the RFC, but no real Dante packet was ever tested |
| The stream disappears from Dante Controller when stopped | ❌ | Deletion sending is only covered by the unit test |
| Long-run clock drift (dropouts, ticks) | ❌ | |

## 4. Not tested at all

- Several streams announced at once (for example 4 × 8 channels = 32)
- Redundant (ST 2022-7 / DUP) streams. The code announces each interface's leg
  separately, but it was never run with two Ethernet ports
- Other SAP-based devices (RAVENNA, Livewire+ and others)
- Transfer between two Macs, and networks with a managed switch (IGMP snooping)
- Docker / Linux build

## 5. Next test steps (once Dante hardware is available)

1. Turn on AES67 mode on the Dante device and reboot it.
2. Check in the web UI that Ponte locks to the Dante device's PTP clock.
3. In Dante Controller, route the Ponte stream to Dante receive channels and
   listen.
4. Create an AES67 multicast flow on the Dante device and receive it with
   **Connect** in Ponte's **SAP streams** card.
5. Run for 30 to 60 minutes and watch lost/concealed packet counts and the
   audio.

Note: in practice Dante AES67 flows are planned as 8 channels per flow. Send
more channels as several flows.
