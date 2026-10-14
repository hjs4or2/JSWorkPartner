# 바탕화면 로봇 개인비서 (Qt)

앱의 기본 진입 화면은 작은 로봇이다. 로봇에서 대화·업무 추가·업무 목록·주간보고서를 열 수 있다. 기존 보드는 트레이의 `퀘스트 보드 표시`로 연다. 이전 보드 표시 설정이 켜져 있어도 시작 때 자동 표시하지 않는다. 트레이에는 로봇 표시, AI 대화 열기, 숨기기, 기존 업무창, 설정, 완전 종료가 있다. 트레이가 없는 환경에서는 업무창도 열어 복귀 경로를 유지한다. 업무창에서 `Ctrl+Shift+Space`로 로봇과 대화를 다시 연다.

디자인·크기·표시·항상 위 설정은 로봇 우클릭의 `로봇 설정...`이나 기존 앱 `Settings...`의 `로봇 설정 열기...`에서 연 전용 창에서 바꾼다. 크기는 25/50/75/100/125/150%에서 선택한다. 기본 50%는 72×75이며 100%는 원본 크기 144×150이다. 저장하면 즉시 적용되고 취소하거나 닫으면 변경하지 않는다. 숨김으로 저장한 로봇은 다음 실행에도 숨겨지며 트레이의 로봇 표시나 `Ctrl+Shift+Space`로 다시 열 수 있다.

디자인은 **민트봇(기본)**, **우주봇**, **고양이봇**, **미니드론** 네 가지다. 민트봇은 잎 안테나와 분리된 둥근 머리·몸, 우주봇은 원형 헬멧과 옆 포드, 고양이봇은 귀·수염·꼬리, 미니드론은 로터와 마름모 몸으로 구분한다. 외부 이미지 없이 Qt 벡터로 그리며, 네 디자인 모두 같은 144×150 캔버스에서 크기·눈 깜박임·떠 있는 움직임·AI busy 표시·업무 수 뱃지를 공유한다. 기존 동작대로 AI busy 중에는 업무 수 뱃지 대신 진행 표시를 보여준다.

설정창의 디자인과 크기 선택은 창 안의 미리보기만 바꾼다. **저장** 한 번으로 디자인·크기·표시·항상 위를 함께 저장하고 실제 로봇에 적용한다. **취소 또는 X**는 실제 로봇과 저장값을 바꾸지 않는다. 미리보기와 실제 로봇은 같은 그리기 구현을 사용한다. 선택은 `desktopPet/designId`에 `mint-bot`, `space-bot`, `cat-bot`, `mini-drone` 중 하나로 저장한다. 누락·손상·알 수 없는 ID는 민트봇으로 해석하며 시작 때 저장값도 정규화한다. 재실행하면 저장한 디자인을 복원한다.

설정창의 Codex 로그인·CLI·모델·연결 상태는 읽기 전용이다. `연결 전`은 실제 로그인 확인 전 상태이며, 첫 대화를 보내면 로그인과 연결 상태를 확인한다.

로봇과 대화창의 닫기·숨기기는 AI 취소와 별개다. AI 응답을 멈추려면 대화창의 취소 버튼을 누른다. 취소 요청 응답만으로 작업 완료를 판단하지 않고 `turn/completed`를 기다린다. 트레이 종료는 앱이 시작한 Codex/MCP 자식 프로세스만 종료한다.

## AI 연결과 데이터

별도 API 키 없이 기존 **Codex ChatGPT 로그인**을 재사용한다. CLI가 없거나 로그인 방식·제공자·주소·카탈로그·도구 차단 상태가 검증 조건과 다르면 설명을 표시하고 AI 요청을 시작하지 않는다. 사용자 지정 제공자/endpoint와 API 키 방식으로 조용히 전환하지 않는다. 업무 추가·목록·보고서 버튼은 기존 앱 기능을 사용한다.

**AI 대화는 Codex 서비스로 전송된다. 업무 도구를 요청하면 해당 업무 내용도 대화에 포함될 수 있다.** 일반 대화에 업무 목록이나 개발 저장소 내용을 자동 첨부하지 않는다. 앱은 대화 원문·프로토콜 stdout/stderr·인증 값을 파일이나 설정에 저장하지 않는다. 대화 기록은 앱 메모리에 있으며 새 대화/앱 종료로 사라진다. `ephemeral`과 대화 이력 저장 비활성화를 사용해도 **Codex 자체의 내부 상태·진단 SQLite/파일은 생성될 수 있다.** 완전한 무기록을 보장하지 않는다.

대화 문맥은 앱/개발 저장소와 분리된 앱 소유 임시 디렉터리다. 명시한 기본·개발자 지침을 사용하고 환경·작업 공간·선택 capability roots를 빈 목록으로 시작한다. 업무 제목·내용·URL·도구 결과를 지시나 권한으로 취급하지 않는다.

## 검증한 연결 조건

- Codex CLI `0.161.0`, 실행 파일 SHA-256 `a0f89acca07a511734cac7fddee26b2106fab68918717bc90df16104a0760a30`.
- 같은 실행 파일의 `debug models --bundled` 원본 stdout을 QByteArray 그대로 확인한다. 원본 SHA-256은 `262dc36e0e7beb290aef8196bd59f42acf51647b8f7b68dafa533ae7742019e6`이다. 변환·정렬·속성 수정 없이 앱 임시 문맥의 전용 파일에 `QSaveFile`로 atomic 저장한다. 이 파일만 공식 `model_catalog_json` 설정으로 지정한다.
- 실제 `model/list`에 있는 `gpt-6.1-sol`을 사용하며 초기화/스레드 버전과 실제 모델도 검사한다. 앱 원본 pin의 파일 경로·sessionFlags origin·내용 SHA를 검사하고 매 turn 및 질문 답변 전송 전에 내용을 재확인한다. 임의 inherited 카탈로그 override는 거부한다.
- `agents.enabled=false`, 앱·플러그인·hooks·shell·멀티에이전트·브라우저·컴퓨터·이미지·기억/목표 기능 비활성화, `notify=[]`, read-only/승인 never를 자식 인자로 적용한다. 등록된 MCP는 이름별 inline table로 비활성화한다. 전역 설정/로그인 파일을 변경하거나 인증 값을 추출·복사하지 않는다.
- 등록 MCP discovery에도 `features.apps=false`, `features.plugins=false`, `features.remote_plugin=false`를 적용해 child에서 transport가 없는 앱/플러그인 유래 이름을 추가하지 않는다. 발견된 기존 이름별 비활성 설정과 모든 runtime MCP 상태 검사는 유지한다. 초기화 통신 실패와 성공 응답의 버전 불일치는 원문 오류 없이 구분해 표시한다.
- 승인된 capability 정책은 **업무 네 개 + 파일/네트워크/프로세스가 없는 exec/wait 래퍼 + 사용자 질의**다. 제한 래퍼를 위해 `features.code_mode_host=true`, `features.code_mode.excluded_tool_namespaces=["clock"]`를 사용한다. 모델 카탈로그의 속성을 수정해 기능이 없는 것처럼 꾸미지 않는다.
- 같은 cwd의 설정 계층을 낮은 우선순위부터 병합하고, 비활성 계층은 제외한다. 스레드 feature 상태와 모든 MCP 상태 페이지의 도구/리소스 등록 부재를 검사한다. 비활성 MCP 행 자체는 남을 수 있다. 스레드에서 자동 정규화되는 `unified_exec`/`shell_snapshot` 값만으로 접근 가능하다고 판단하지 않는다. 환경 빈 목록과 실제 handler/registry 차단 재현을 함께 근거로 삼는다.
- MCP `runtimeStatus`는 scope별로 검사한다. thread 없는 조회는 실제 null만, thread 조회는 정확한 문자열 `disabled`만 허용한다. thread null·누락·다른 상태·타입은 거부한다. known-name/disabled 설정과 빈 tools/resources/templates, feature·카탈로그·모델·환경·pagination 검사는 그대로 유지한다.

이는 검증한 바이너리·원본 카탈로그·설정 경로의 효과 검증이다. 향후 버전, 임의 모델/제공자, 원격 관리 정책, Codex 엔진 자체의 모든 보안 성질까지 보장하지 않는다. 원본 pin에서 관측한 카탈로그 discovery 요청은 없었지만 실제 ChatGPT 장기 세션의 모든 refresh 분기를 검증한 것은 아니다. 업그레이드나 조건 불일치는 재검증이 필요하다.

## 업무 도구와 재시도

앱의 실제 저장소 DB 절대 경로와 기존 self-contained `JSWorkPartner.Mcp.exe`를 비동기 stdio로 연결한다. 새 DB/디렉터리/스키마를 자동 생성하지 않는다. 등록 목록의 `jsworkpartner` 경로는 앱 DB와 일치할 때만 사용한다. 배포 폴더의 기존 MCP 실행 파일 또는 `desktopPet/mcpExecutable` 명시 설정도 확인한다.

허용 도구는 `create_task`, `list_tasks`, `get_task`, `complete_task` 네 개다. 모델에는 requestId 필드를 제공하지 않는다. 컨트롤러가 변경 의도별 UUID를 만들고 동일 입력의 응답 유실 재시도에 재사용한다. 성공은 실제 MCP 결과에만 근거한다. 응답 유실/취소 이후에는 변경 여부를 단정하지 않는다. 이 재시도 메모리는 앱 실행 동안만 유지되므로 앱을 재시작한 뒤 불명확한 변경을 반복하기 전에 업무 목록을 확인해야 한다.

`[Updater]` 등을 포함한 전체 제목을 보존하고 문자열 업무 ID를 사용한다. 완료에는 제목/ID를 표시하는 선택창이 나오며 자동으로 첫 후보를 선택하지 않는다. 새 대화는 새 스레드로 시작한다. 동기 사용자 질문은 원래 요청 ID로 응답하거나 빈 답으로 취소하고, 비동기 질문 답은 진행 중이면 `turn/steer`, 종료 뒤면 환경 빈 목록의 새 `turn/start`로 보낸다.

## 구현 검증 기록

`JSWorkPartner/obj/PetReview`의 임시 fixture는 실제 사용자 DB/설정과 분리되어 있다. Windows/Fusion 실행에서 분할 JSON·한국어 UTF-8, 대화 유지/새 대화, 취소 응답과 최종 완료 구분, 오류/프로세스 종료/늦은 이벤트, 질문의 동기 RPC·진행 중 steer·종료 후 새 turn UI, 타임아웃, 실제 fixture MCP 생성/조회/완료/원자 재시도, UUID 소유권, 금지 도구 거부, 모호한 완료 대상, 저장된 보드 visible=true와 펫 숨김/복귀/자식 종료를 확인했다. 빈 fixture Codex HOME에서는 실제 CLI가 원본 atomic pin 및 설정 검사를 통과하고 로그인 없음으로 종료되는 흐름을 두 번 확인했다. 카탈로그 내용 변조는 거부했다.

격리 효과의 별도 증거는 개발 중 생성한 ignored `JSWorkPartner/obj/PetIsolationReview/Findings.md`와 `CatalogProvenance/Supplement.md`에 있다. 제품은 이 fixture 파일을 런타임에 참조하지 않는다. 실제 로그인 startup·단일 일반 대화 성공 여부는 별도 `PetReview/live` 실행 기록으로만 판정하며 synthetic fixture 결과로 추론 성공을 주장하지 않는다. 장기 refresh·전체 entitlement는 미검증이다.

후속 비동기 검토에서 완료 조회·승인·실제 쓰기 직전에 원 요청의 RPC ID/call ID/thread/turn/세대와 취소 상태를 확인하도록 보강했다. 이전 turn의 취소 오류·timeout은 새 turn에 적용하지 않으며, 취소 제한시간은 delta나 ack로 연장되지 않는 별도 절대 시계로 측정한다. 진행 중 비치명 연결 오류는 알림을 표시하고 같은 AI 메시지의 스트림을 유지한다. 동기 질문의 정상 답변에도 원본 pin SHA 재검사를 추가해 문서와 구현을 맞췄다.

수정 전 스냅샷의 별도 race fixture에서는 13개 안전 조건 중 10개가 실패했고, 수정 후 동일 입력은 **13 PASS / 0 FAIL**이었다. 정상 대화에서 업무 도구 호출 없음, 정상 취소·질문 답변, 살아 있는 요청의 실제 fixture MCP 추가/조회/완료는 좁은 smoke **20 PASS / 0 FAIL**로 확인했다. 기존 기본 fixture의 121 PASS 기록과 새 race/smoke 기록은 별도로 보존했다. 실제 계정 추론 없이 synthetic backend와 fixture DB만 사용했다.

Release x64 출력 경로: `JSWorkPartner/obj/PetSettingsBuild/status-fix/bin/JSWorkPartner.exe`. 새 소스의 커밋·푸시는 수행하지 않았다.

후속 MCP scope 수정은 좁은 fake fixture **42 PASS / 0 FAIL**로 확인했다. global null/thread disabled만 통과하고 반대 scope·누락·다른 상태/타입, unknown server·비어 있지 않은 inventory·enabled 설정·잘못된 feature·pagination 오류는 거부했다. 실제 로그인 metadata-only 검증은 **35 PASS / 0 FAIL**, turn/start와 추론은 0회였다.

최신 GUI subsystem 공개 제품 하네스에서 startup은 native **28 PASS / 0 FAIL**, runner **17 PASS / 0 FAIL**이었다. 기존 ChatGPT 로그인, 실제 `gpt-6.1-sol`, 원본 pin, 분리된 fixture DB/설정으로 일반 문장 한 번을 전송한 결과 native **31 PASS / 0 FAIL**, runner **19 PASS / 0 FAIL**, 모두 exit 0이었다. delta 4개·응답 길이 8자·성공 완료 1회를 확인했고 본문은 저장하지 않았다. 업무 호출/업무 변경은 0건이며 관측된 Codex/MCP와 모든 자손 conhost의 EOF·controller 소멸 후 deadline 및 PID+생성시각 postexit 종료를 확인했다. turn/start 진행은 공개 busy/model 상태, 실제 `turnFinished(completed)`와 제품 제어 흐름에 근거하며 wire payload를 수집한 것은 아니다.

근거는 ignored `PetReview/ThreadStatusFix`와 `PetReview/live/v4`의 run별 불변 기록이다. 이후 별도 `PetReview/live-task`에서 같은 실제 대화의 사용자 3턴으로 추가→목록/상세→완료를 확인했다. 모델의 dynamic 요청은 create/list/get/complete 각 1회였고, 문자열 ID `1`·전체 제목 `[Updater] 테스트`가 보존됐다. 생성/완료의 실제 MCP 결과 receipt와 DB 상태, 자동 Qt 목록·주간요약·주간보고서 병합이 일치했다. 완료창은 살아 있는 원 요청의 정확한 fixture ID·제목을 확인한 경우에만 승인했다. native **32 PASS / 0 FAIL**, runner **18 PASS / 0 FAIL**, 모두 exit 0이며 실제 사용자 DB/설정은 사용하지 않았다.

이 3턴은 내부 모델 추론 호출 3회라는 뜻이 아니다. 내부 호출 횟수와 목록/상세의 개별 성공 응답 횟수는 공개 API에서 직접 관측하지 않았으며, 생성·완료 성공은 각 1회 확인했다. 소유 PID+생성시각으로 Codex/MCP 및 모든 관측 자손의 deadline/postexit 잔존 0을 확인했다. code-mode host 한 개는 EOF 요청 뒤 exit 1이었고 다른 관측 자식은 exit 0이었다. 모든 자손의 정상 exit 0으로 일반화하지 않는다. 응답 원문은 저장하지 않았다.

실제 모델 질문·취소·장기 대화/refresh·전체 entitlement와 실제 사용자 DB는 미검증이다. 이전 첫 live cleanup 실패는 PID 근거가 없어 미확정으로 보존했다. exe 재빌드 없이 검증 문장만 추가했으며 바이너리 빌드 입력 49개와 이후 문서 포함 manifest 시점을 구분해 기록한다.
