# SAP 기능 검증 현황

[English](SAP_STATUS.md) · 한국어

Ponte는 AES67eleven에 SAP(RFC 2974) 알림·발견 기능을 추가한 포크다.
목적은 Dante Controller 같은 SAP 기반 장비가 Ponte 스트림을 찾을 수 있게 하는 것이다.
이 문서는 무엇이 실제로 확인됐고, 무엇이 아직 확인되지 않았는지를 구분해 적는다.

> **요약:** 실제 Dante 장비에서 소리를 받는 것까지 확인했다. AES67 모드를 켠
> Shure ULXD4D가 PTP 기준 시계가 됐고, Ponte가 SAP로 그 스트림을 찾아 받았으며,
> DAW에서 Ponte 입력으로 마이크 소리를 녹음했다.
> **맥에서 Dante 장비로 소리를 보내는 것은 아직 확인하지 않았다.** 시험에 쓴 장비에
> 수신 채널이 없었기 때문이다.

## 표기

| 표시 | 뜻 |
|---|---|
| ✅ 확인됨 | 직접 실행해서 결과를 눈으로 확인함 |
| 🟡 부분 확인 | 일부 조건에서만 확인함. 한계를 함께 적음 |
| ❌ 확인 안 됨 | 시험하지 않았거나 시험할 장비가 없었음 |

## 시험 환경

**맥 한 대 시험 (1장, 2장), 2026-11-14 ~ 2026-11-15.** 이름을 Ponte로 바꾸기 전,
AES67eleven 이름으로 진행했다.

- macOS 15.8.1, 맥 한 대, 외부 AES67/Dante 하드웨어 없음
- 네트워크: USB 기가비트 LAN 어댑터 (`en7`)
- PTP 기준 시계: [`tools/ptp_test_gm.py`](tools/ptp_test_gm.py) (소프트웨어 타임스탬프, 시험 전용)

**Dante 장비 시험 (3장), 2026-11-16 ~ 2026-11-17.** 릴리스 설치 파일로 설치한
Ponte 0.1.1로 진행했다.

- 같은 맥과 USB 어댑터(`en7`), 링크 로컬 주소(169.254.x.x), 스위치 없이 케이블 직결
- Dante Controller: 같은 맥에서 실행
- 시험한 장비:

| 장비 | Dante 플랫폼 | AES67 | 결과 |
|---|---|---|---|
| PreSonus AVB-D16 (Dante 포트) | Audinate DCM, 라우터 4.0.1 | 지원 안 함 (AES67 Config 탭 없음, PTPv1만 송신) | Ponte와 연결 불가 |
| Shure ULXD4D | Audinate DCM, 라우터 4.2 | 지원, 켬 | 아래 시험에 사용 |

## 1. 단위 시험 — ✅ 확인됨

[`tests/sap/`](tests/sap/)의 시험 프로그램 3개가 모두 통과했다 (2026-11-15에 다시 실행해 확인).

| 시험 | 확인한 내용 | 결과 |
|---|---|---|
| `sap_test` | SAP 패킷 형식(헤더, `application/sdp`, 출발지 IP), 주기적 재알림 시 같은 해시 유지, 내용이 바뀌면 이전 버전 삭제 후 새 버전 알림, 스트림이 사라지면 삭제 패킷 송신 | ✅ |
| `sap_listen_test` | 수신한 알림 등록, 같은 세션의 새 버전이 기존 항목 대체, 잘못된 패킷 무시, 삭제 패킷 수신 시 목록에서 제거 | ✅ |
| `refclk_test` | `a=ts-refclk` 의 기준 시계 ID를 RFC 7273 형식(하이픈, 대문자)으로 출력하고 다시 읽기 | ✅ |

빌드·실행 방법 (저장소 최상위 폴더에서):

```bash
for t in sap_test sap_listen_test refclk_test; do
  clang++ -std=c++17 -Icommon -Ithird_party tests/sap/$t.cpp \
    common/rtp/sap.cpp common/rtp/sdp.cpp common/compat/net_compat.cpp \
    common/compat/time_compat.cpp common/log.cpp -o /tmp/$t && /tmp/$t
done
```

이 시험들은 루프백(`lo0`)에서만 돈다. 실제 네트워크나 다른 장비는 거치지 않는다.

## 2. 맥 한 대 안에서의 통합 시험

| 항목 | 상태 | 비고 |
|---|---|---|
| 빌드와 설치 (`scripts/install.sh`) | ✅ | 서명 인증서가 없을 때 스크립트가 조용히 멈추던 문제를 고쳤다 |
| 데몬이 실제 랜 포트로 SAP 알림 송신 | ✅ | 처음 한 번은 macOS "로컬 네트워크" 권한 허용 전이라 실패했다. 허용 후 정상 송신. 실패하면 1초 뒤 다시 보낸다 |
| 시험용 기준 시계에 PTP 동기화 | ✅ | `ptp_test_gm.py` 기준 |
| 송신 스트림 (2채널 테스트 톤, `239.69.145.10`) | ✅ | |
| 자기 자신이 보낸 SAP 알림을 수신기가 발견 | ✅ | 웹 UI "SAP streams" 카드에 표시됨 |
| SAP로 찾은 스트림에 수신 연결 (SDP 직접 / `sap_key` / 웹 UI Connect 버튼) | ✅ | 세 방법 모두 상태 Healthy |
| 수신한 소리 | ✅ | 두 채널 모두 -18.0 dB (보낸 톤과 같은 레벨), 손실·보정 패킷 0 |

## 3. Dante 호환

| 항목 | 상태 | 비고 |
|---|---|---|
| Dante Controller가 Ponte 스트림을 목록에 표시 | ✅ | Shure 장비 옆에 "Ponte AES67 Out ch 1 2 3 4 5 6 7 8"이 송신기로 표시됨 |
| Dante 장비가 기준 시계일 때 Ponte가 동기화 | ✅ | ULXD4D(`00:0e:dd:ff:fe:f0:04:66`, 도메인 0)에 PTPv2로 동기화. 오프셋은 -489 µs ~ +9 µs 범위 (소프트웨어 타임스탬프) |
| Dante 장비가 보내는 SAP 알림을 Ponte가 해석 | ✅ | "ULXD4D-f00466 : 32", 2채널 L24/48000, 1 ms, `239.69.69.131`, `a=ts-refclk`·`a=mediaclk` 포함 |
| Dante 장비 → 맥으로 소리 전달 | ✅ | `sap_key`로 연결. 말소리가 -24 ~ -40 dB로 들어왔고, DAW에서 Ponte 입력으로 녹음됨 |
| 10분 안정성 (Dante 장비 → 맥) | 🟡 | 패킷 598,561개, 손실 0, 보정 0, PTP 동기 유지. 마이크가 대부분 꺼져 있어 패킷과 클럭만 확인했고, 귀로 들리는 끊김은 확인하지 않았다. 장치 읽기 언더런이 데몬 시작 후 누적 19회였는데, 이 시험 중에 생긴 것인지는 따로 나누지 않았다 |
| 맥 → Dante 장비로 소리 전달 | ❌ | ULXD4D에는 Dante 수신 채널이 없다. 수신할 수 있는 장비가 필요하다 |
| 스트림을 끌 때 Dante Controller 목록에서 사라짐 | ❌ | 삭제 패킷 송신은 단위 시험으로만 확인했다 |
| 장시간 (30분 ~ 1시간 이상) | ❌ | |
| AES67을 지원하지 않는 장비 | ✅ (예상대로) | AVB-D16은 PTPv1만 보내고 SAP도 보내지 않아서, Ponte가 동기화하거나 스트림을 찾을 수 없다. Ponte 문제가 아니라 장비의 한계다 |

## 4. 시험하지 않은 범위

- 스트림 여러 개를 동시에 알림 (예: 8채널 × 4개 = 32채널)
- 이중화(DUP) 스트림. 코드는 랜 포트별로 따로 알리도록 나누지만, 랜 포트 두 개로 시험하지 않았다
- 다른 SAP 기반 장비 (RAVENNA, Livewire+ 등)
- 맥 두 대 사이 전송, 관리형 스위치(IGMP snooping) 환경
- Docker / Linux 빌드

## 5. 다음 시험 순서

1. 수신 채널이 있고 AES67을 켠 Dante 장비로, Dante Controller에서 Ponte 스트림을 연결하고 소리를 확인한다.
2. Ponte 송신을 끄고 Dante Controller 목록에서 사라지는지 확인한다.
3. 소리를 계속 넣으면서 30분 ~ 1시간 돌리고, 끊김을 귀로 확인하고, 장치 언더런 수를 시험 전후 차이로 기록한다.
4. IGMP snooping을 켠 관리형 스위치를 거쳐 다시 확인한다.

참고:

- Dante Controller는 AES67을 지원하는 장비에만 **AES67 Config** 탭을 보여 준다. 그 탭에서 AES67 모드를 켜고 장비를 재부팅한다.
- Dante의 AES67 흐름은 실무상 흐름 하나에 8채널을 기준으로 잡는다. 더 많은 채널은 흐름을 여러 개로 나눠 보낸다.
