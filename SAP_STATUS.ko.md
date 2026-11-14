# SAP 기능 검증 현황

[English](SAP_STATUS.md) · 한국어

Ponte는 aes67eleven에 SAP(RFC 2974) 알림·발견 기능을 추가한 포크다.
목적은 Dante Controller 같은 SAP 기반 장비가 aes67eleven 스트림을 찾을 수 있게 하는 것이다.
이 문서는 무엇이 실제로 확인됐고, 무엇이 아직 확인되지 않았는지를 구분해 적는다.

아래 시험은 모두 이름을 Ponte로 바꾸기 전(aes67eleven 이름 그대로)에 했다.
이름 변경 뒤에는 단위 시험과 설치 파일 빌드만 다시 확인했다.

> **요약:** 맥 한 대 안에서의 동작과 Dante Controller의 스트림 표시까지는 확인했다.
> **실제 Dante 장비와 소리를 주고받는 것은 아직 한 번도 확인하지 않았다.**

## 표기

| 표시 | 뜻 |
|---|---|
| ✅ 확인됨 | 직접 실행해서 결과를 눈으로 확인함 |
| 🟡 부분 확인 | 일부 조건에서만 확인함. 한계를 함께 적음 |
| ❌ 확인 안 됨 | 시험하지 않았거나 시험할 장비가 없었음 |

## 시험 환경

- macOS 15.8.1, 맥 한 대 (외부 AES67/Dante 하드웨어 없음)
- 네트워크: USB 기가비트 LAN 어댑터 (`en7`)
- PTP 기준 시계: 실제 장비 대신 [`tools/ptp_test_gm.py`](tools/ptp_test_gm.py) (소프트웨어 타임스탬프, 시험 전용)
- Dante Controller: 같은 맥에서 실행
- 시험 일자: 2026-11-14 ~ 2026-11-15

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
| 시험용 기준 시계에 PTP 동기화 | ✅ | `ptp_test_gm.py` 기준. 실제 PTP 장비로는 확인 안 함 |
| 송신 스트림 (2채널 테스트 톤, `239.69.145.10`) | ✅ | |
| 자기 자신이 보낸 SAP 알림을 수신기가 발견 | ✅ | 웹 UI "SAP streams" 카드에 표시됨 |
| SAP로 찾은 스트림에 수신 연결 (SDP 직접 / `sap_key` / 웹 UI Connect 버튼) | ✅ | 세 방법 모두 상태 Healthy |
| 수신한 소리 | ✅ | 두 채널 모두 -18.0 dB (보낸 톤과 같은 레벨), 손실·보정 패킷 0 |
| 장시간 안정성 | ❌ | 몇 분 단위로만 돌려 봤다 |

## 3. Dante 호환

| 항목 | 상태 | 비고 |
|---|---|---|
| Dante Controller가 Ponte 스트림을 목록에 표시 | ✅ | "AES67eleven SAP test"가 2채널 송신기로 표시됨 |
| Dante Controller에서 PTP 기준 시계 인식 | ❌ | Dante 장비가 없어 "기본 리더 클록 알 수 없음" 상태였다 |
| Dante 장비가 기준 시계일 때 Ponte가 동기화 | ❌ | Ponte는 기준 시계가 될 수 없어서(슬레이브 전용) 이게 가장 먼저 확인할 항목이다 |
| 맥 → Dante 장비로 소리 전달 | ❌ | Dante 장비 없음 |
| Dante 장비 → 맥으로 소리 전달 | ❌ | Dante 장비 없음 |
| Dante 장비가 보내는 SAP 알림을 Ponte가 해석 | ❌ | 형식은 RFC 기준으로 맞췄지만 실제 Dante 패킷으로 시험한 적 없다 |
| 스트림을 끌 때 Dante Controller 목록에서 사라짐 | ❌ | 삭제 패킷 송신은 단위 시험으로만 확인했다 |
| 장시간 클럭 드리프트 (끊김, 틱 노이즈) | ❌ | |

## 4. 시험하지 않은 범위

- 스트림 여러 개를 동시에 알림 (예: 8채널 × 4개 = 32채널)
- 이중화(DUP) 스트림. 코드는 랜 포트별로 따로 알리도록 나누지만, 랜 포트 두 개로 시험하지 않았다
- 다른 SAP 기반 장비 (RAVENNA, Livewire+ 등)
- 맥 두 대 사이 전송, 관리형 스위치(IGMP snooping) 환경
- Docker / Linux 빌드

## 5. 다음 시험 순서 (Dante 장비가 생기면)

1. Dante 장비의 AES67 모드를 켜고 재부팅한다.
2. Ponte가 Dante 장비의 PTP에 동기화되는지 웹 UI에서 확인한다.
3. Dante Controller에서 Ponte 스트림을 Dante 수신 채널에 연결하고 소리를 확인한다.
4. Dante 장비에서 AES67 멀티캐스트 흐름을 만들고, Ponte의 "SAP streams" 카드에서 Connect로 받는다.
5. 30분~1시간 틀어 두고 손실·보정 패킷 수와 소리를 확인한다.

참고: Dante의 AES67 흐름은 실무상 흐름 하나에 8채널을 기준으로 잡는다. 더 많은 채널은 흐름을 여러 개로 나눠 보낸다.
