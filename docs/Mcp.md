# JSWorkPartner 로컬 업무 MCP

독립 C# stdio 서버가 기존 앱 SQLite DB에 업무를 추가·조회·완료한다. Qt GUI 실행/단일 실행 mutex와 무관하다. Redmine이나 주간보고서 API를 호출하거나 수정하지 않는다. `NativeSqlite`, `TaskRepository`, `TaskItem`, `TaskService` 소스를 Compile Link하며 GUI와 같은 업무 삽입·순번·날짜 저장 규칙을 사용한다.

## 실행 파일과 빌드

프로젝트는 `net8.0`, `win-x64`, self-contained다. `ModelContextProtocol`/Core **2.2.0**과 .NET runtime **8.0.31**을 고정하며 `packages.lock.json`에 전이 의존성 버전과 해시를 기록한다. SDK는 저장소 `global.json`의 9.0.302/latestPatch를 따르며 이 환경에서는 9.0.318을 사용했다.

실행 경로:

```text
E:\_Sub\JS_WorkPartner\JSWorkPartner.Mcp\bin\Release\net8.0\win-x64\publish\JSWorkPartner.Mcp.exe
```

같은 `publish` 디렉터리의 `JSWorkPartner.Mcp.dll`, `.deps.json`, `.runtimeconfig.json`, `ModelContextProtocol.dll`, `ModelContextProtocol.Core.dll`, `Microsoft.Extensions.*.dll` 및 .NET runtime DLL을 **폴더 전체로** 보관해야 한다. Windows 기본 `winsqlite3.dll`을 사용한다. 실행 시 `dotnet run/build`나 Qt 앱은 필요하지 않다. 게시 산출물·캐시·fixture는 프로젝트 `.gitignore`에 한정하여 제외한다.

저장소 루트의 PowerShell에서 빌드한다. 환경변수는 이 빌드 프로세스에만 설정한다.

```powershell
$env:DOTNET_CLI_HOME = "$PWD\JSWorkPartner.Mcp\.dotnet"
$env:NUGET_PACKAGES = "$PWD\JSWorkPartner.Mcp\.nuget\packages"
$env:NUGET_HTTP_CACHE_PATH = "$PWD\JSWorkPartner.Mcp\.nuget\http"
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_GENERATE_ASPNET_CERTIFICATE = 'false'
dotnet restore JSWorkPartner.Mcp/JSWorkPartner.Mcp.csproj --locked-mode --source https://api.nuget.org/v3/index.json
dotnet publish JSWorkPartner.Mcp/JSWorkPartner.Mcp.csproj -c Release --no-restore
```

서버 실행에는 **이미 존재하는 절대 DB 경로**를 반드시 지정한다.

```powershell
& .\JSWorkPartner.Mcp\bin\Release\net8.0\win-x64\publish\JSWorkPartner.Mcp.exe --database 'D:\ExistingData\work_manager.db'
```

서버는 `SQLITE_OPEN_READWRITE`만 사용하고 `CREATE` 플래그를 사용하지 않는다. 없는 DB/폴더, 손상 DB, 구형/다른 스키마를 거부한다. `Initialize()`나 자동 마이그레이션은 호출하지 않는다. 현재 `tasks` 컬럼/타입/필수값/PK 및 순번 UNIQUE를 검증한다. 시작·조회는 DB를 수정하지 않고, 첫 유효한 변경 요청의 트랜잭션 안에서만 `mcp_requests`를 추가한다. 구형 스키마는 앱에서 먼저 업그레이드해야 한다.

## Codex 등록

앱 DB 경로 선택 우선순위는 `JSWORKPARTNER_DATA_DIR` > `JSWORKMANAGER_DATA_DIR` > `%APPDATA%/JSWorkManager/JSWorkManager/work_manager.db`다. 환경변수는 DB 파일이 들어 있는 디렉터리를 뜻한다. **MCP 등록 스크립트는 이 기본값을 추측하지 않으며 `-Database`가 필수다.** 실행할 앱과 동일한 DB인지 먼저 확인한다.

```powershell
# 기본 동작은 dry-run. 파일 존재와 동일 이름 등록 여부만 확인하고 정확한 명령을 표시한다.
.\scripts\Register-JSWorkPartnerMcp.ps1 -Database 'D:\ExistingData\work_manager.db'
.\scripts\Register-JSWorkPartnerMcp.ps1 -Database 'D:\ExistingData\work_manager.db' -DryRun

# 다른 환경에서 신규 등록할 때만 연결 담당자가 명시적으로 실행한다.
.\scripts\Register-JSWorkPartnerMcp.ps1 -Database 'D:\ExistingData\work_manager.db' -Apply
```

등록 스크립트는 인자 배열로 `codex mcp add jsworkpartner -- <publish exe> --database <absolute DB>`를 실행한다. `codex mcp list --json`으로 같은 이름이 존재하면 덮어쓰지 않고 실패한다. 기존 설정을 삭제하지 않는다. 등록 후 새 Codex 세션에서 도구 연결을 확인한다.

최종 메인 등록은 권한 승인된 실행으로 완료했다. 메인은 다음 인자로 스크립트를 실행하여 exit 0과 `Added global MCP server jsworkpartner`를 확인했다.

```powershell
.\scripts\Register-JSWorkPartnerMcp.ps1 -Database 'C:\Users\ekank\AppData\Roaming\JSWorkManager\JSWorkManager\work_manager.db' -Apply
```

이는 기존 사용자 설정의 Codex 전역 MCP 등록이다. 등록 과정은 DB 파일 존재·경로 확인과 Codex config 변경만 수행했으며 실제 업무 변경은 하지 않았다. 메인은 `codex mcp list --json`에서 `name=jsworkpartner`, `enabled=true`, stdio 실행 파일과 `--database` 경로 일치를 확인했고 기존 MCP 5개 이름도 유지됨을 확인했다. 코드 구현자의 fixture 검증 중 실사용 DB 미접근·미등록 사실과 별도의 최종 메인 등록을 구분한다.

새 간단 작업/luna 세션(`00ddabb0...`)의 실제 callable tool 메타데이터에서 `mcp__jsworkpartner__create_task`, `mcp__jsworkpartner__list_tasks`, `mcp__jsworkpartner__get_task`, `mcp__jsworkpartner__complete_task` 4개가 발견됐고 startup 오류는 노출되지 않았다고 보고됐다. 이는 메타데이터 발견 확인이며 업무 도구 호출이나 실사용 데이터 조회·변경 검증은 아니다.

## 사용 순서

1. 기존 `Output/Release/JSWorkPartner.exe` 앱을 사용자가 완전히 종료하고 `E:\_Sub\JS_WorkPartner\JSWorkPartner\obj\McpBuild\bin\JSWorkPartner.exe`를 실행한다.
2. 현재 에이전트에 MCP 도구를 반영하려면 reload하거나 새 세션을 연다. 이 환경의 CLI help에서 `paseo agent reload <id>`를 확인했지만 실제 메인 reload는 실행하지 않았다.
3. 새 도구가 반영된 세션에서 `[Updater] 테스트 업무 추가해줘`처럼 요청한다. 이 예시 업무는 실사용 DB에 생성하지 않았다. 로컬 완료 업무는 새 Qt 앱의 주간보고서에 반영된다.

## 도구와 대화 예시

| 도구 | 인자 | 동작 |
| --- | --- | --- |
| `create_task` | `requestId`, `title`, `details?`, `importance?=1`, `deadline?`, `externalUrl?` | 로컬 업무 추가, 전체 제목 원문 보존 |
| `list_tasks` | `status?=pending`, `limit?=50`, `cursor?`, `search?` | pending/completed/all, 최대 100개, ID 오름차순 페이지 |
| `get_task` | `id` | 업무 하나 조회 |
| `complete_task` | `requestId`, `id` | 완료 처리, 이미 완료면 기존 완료시각 유지 |

업무 ID·순번·cursor는 Int64 범위의 십진 **문자열**이다. `list_tasks` 응답의 `nextCursor`를 같은 status/search와 함께 다음 요청의 cursor로 보낸다. 조회 시점의 DB 상태를 반영하므로 페이지 사이 상태 변경까지 고정하는 스냅샷은 아니다. search는 제목의 리터럴 부분 문자열이며 SQLite의 기본 ASCII 대소문자 비교를 사용한다. `%`와 `_`도 와일드카드가 아니다.

대화: `"[Updater] 테스트 업무 추가해줘"` → 제목을 `"[Updater] 테스트"`로 해석했다면 해당 문자열 전체를 보존하여 `create_task`를 호출한다. `[Updater]`를 제거하거나 프로젝트 필드로 분리하지 않는다. 사용자 의도가 제목 전체 `"[Updater] 테스트 업무 추가해줘"`인 경우에도 전달한 제목 그대로 저장한다. 이어서 `"Updater 업무 보여줘"`, `"방금 추가한 업무 완료해줘"`에는 조회 결과의 ID를 사용한다.

```json
{"requestId":"84fa523c-b109-40c2-ac05-e74960acda61","title":"[Updater] 테스트","details":"","importance":1}
```

제목은 공백만 있는 값을 거부하고 최대 500 UTF-16 코드 단위다. details는 최대 20,000, URL은 최대 2,048이며 URL은 자격증명·공백 없는 절대 http/https만 허용한다. 중요도는 정수 1..5, deadline은 유효한 `YYYY-MM-DD`다. 알 수 없는 인자, null, 잘못된 타입·ID·UUID, NUL 등 잘못된 제어문자를 거부한다. details의 빈 문자열은 허용한다. MCP 제목·details는 trim하지 않고 GUI의 기존 정규화는 유지한다. 제목 중복은 앱 규칙과 같은 SQLite NOCASE로 거부한다.

변경 의도마다 새로운 nonzero UUID `requestId`를 만든다. **같은 의도 재시도에는 동일 키와 동일 입력을 사용한다.** 생략한 기본값과 명시한 기본값은 동일 입력으로 취급한다. 성공한 최초 결과 JSON을 저장하여 이후 업무가 바뀌어도 같은 키 재시도에는 최초 스냅샷을 반환한다. 같은 키에 다른 입력/operation은 `request_conflict`다. 성공 요청 로그는 자동 삭제하지 않으므로 DB 보관/백업에 포함한다.

업무 변경과 요청 기록은 하나의 `BEGIN IMMEDIATE` 트랜잭션에서 커밋한다. 동시 동일 requestId는 한 번만 삽입하며 실패·취소 시 롤백한다. 잠금 대기는 5초로 제한한다. 커밋 뒤 응답이 유실되거나 취소 결과가 불명확하면 같은 키로 재시도한다. tool 오류는 `isError`와 `structuredContent.error.code/message`로 반환한다. SQL·경로·업무내용은 diagnostics에 출력하지 않으며 stdout은 SDK JSON-RPC 전용이다. EOF와 Ctrl+C는 종료 경로다.

## Qt 앱 연결 범위

외부 DB 변경감지·로컬 완료 업무의 주간보고서 반영과 Qt P2 수정은 완료됐고 제품 소스를 고정했다. Qt Release build는 exit 0, 관련 검증 58 PASS, atomic 검증 21 PASS, 유효 rename/create 경쟁 24회에서 대상 제목 한 행 유지가 확인됐다. 검증은 격리 fixture를 사용했으며 기존 Output과 실사용 앱은 실행·종료·덮어쓰기하지 않았다. 화면 자동반영에는 위 사용 순서대로 새 Qt 앱 실행이 필요하다. MCP 저장/조회 자체는 Qt 프로세스와 독립이다.

최종 고정 파일:

| 파일 | SHA256 |
| --- | --- |
| `E:\_Sub\JS_WorkPartner\JSWorkPartner\obj\McpBuild\bin\JSWorkPartner.exe` | `A4C045FF45DDF754858CC1309149878846EDE2A71D16E125D719B3DAB3069F02` |
| `JSWorkPartner.Mcp/bin/Release/net8.0/win-x64/publish/JSWorkPartner.Mcp.dll` | `04059CC967A4E1A0A2A412FF1E79592900FBB984822E018EE761D1240498BA62` |

실제 게시된 C# MCP 서버와 Qt 제품 클래스를 컴파일한 Windows UI harness의 통합 검증은 **60 PASS, exit 0**이다. create 이후 pending 화면 반영은 **362ms**, complete 이후 completed 반영은 **474ms**였다. 전체 `[Updater] 테스트` 제목·문자열 ID와 재시도 스냅샷을 보존했고, 로컬 완료 업무가 주간 요약·통계·HTML 초안의 '로컬 업무' 행에 포함됐다. 실제 MCP가 GUI 사전 중복 조회 후 제목을 선점한 P2 재현도 rename을 거부하며 원본을 보존했다.

이는 **제품 클래스 UI harness 통합 검증**이며 main exe 수동 클릭 검증은 아니다. 보고서 초안은 실제 Dialog에 완료 모델 공급 함수를 harness에서 주입해 생성했고, 실제 업무보고서 버튼의 공급 함수 wiring은 별도 관련 검증 58 PASS에서 확인했다. VCS/Redmine 설정이나 외부 네트워크를 사용하지 않았고 기존 TLS/native clipboard 제한은 이번 검증 범위 밖이다.

직접 읽은 근거는 `JSWorkPartner/obj/McpReview/post-review/final-summary.json`과 `JSWorkPartner/obj/McpReview/integration/ReviewStatus.md`다. 메인은 최종 Qt 29개 소스, 서버 17개 소스, 게시 파일 201개, 기존 HTTP 변경 4개의 해시 불일치 0 및 `diff --check` exit 0을 재확인했다. 이번 구현과 기존 HTTP 변경은 `dev`에서 미커밋·미푸시 상태다.

## 격리 검증 기록

코드 구현자와 격리 검증 작업은 실사용 DB·QSettings·API key·클립보드에 접근하지 않았다. 최종 메인의 사용자 설정 등록은 위 등록 절의 별도 작업이다. `verification/verify.py`가 프로젝트 내부 `verification/artifacts/<UUID>`에 기존 Task 스키마 fixture를 생성한다.

```powershell
python JSWorkPartner.Mcp/verification/verify.py
# 기존 C# 테스트 Program.cs 원문을 Compile Link하는 작은 회귀 harness
dotnet run --project JSWorkPartner.Mcp/verification/regression/Regression.csproj -c Release
python JSWorkPartner.Mcp/verification/codex_handshake.py --codex '<installed native codex.exe>'
```

실제 결과(2026-10-07):

- SDK 9.0.318에서 locked restore와 self-contained publish 성공.
- 제목 정확왕복, create/list/get/complete, 재시작 유지, 최초 결과 재시도, 입력충돌, 완료시각 보존, 4개 프로세스 동일 키 경쟁 통과.
- SQL injection·리터럴 검색·페이지제한·입력오류, SQLite busy timeout, 요청기록 실패 시 업무 rollback 통과.
- 없는/손상/구형/다른 DB 거부, 새파일/상위폴더 미생성, 잘못된 DB 내용 미변경 통과.
- 실제 stdio `initialize`(2025-11-25)와 `server/discover`(2026-07-28), 양쪽 tools/list/call 및 EOF/정상 stdout 검증 통과. 최신 lifecycle은 SDK가 처리한다.
- 기존 C# 업무 흐름 테스트 원문을 링크한 회귀 harness 통과. 최초 기존 WPF 테스트 출력 경로 변경 시 생성 소스 중복 오류가 있었으며 기존 테스트/UI를 수정하지 않고 격리 harness로 검증했다.
- 설치된 Codex CLI 0.159.3 app-server: fixture 내부에 격리한 child CODEX_HOME/config만 사용하여 유효 MCP 목록이 fixture 하나임을 확인한 뒤 4개 도구 발견 성공. 추론/실사용 등록 없음. 기록: `verification/artifacts/codex-bf4ca67e-5e9b-462c-b024-2baf1ce4e390/result.json`.
- 등록 스크립트 mock CLI 검증 통과: 기본/명시 dry-run, 공백·작은따옴표 경로 인자배열 전달, 기존 동일 이름/상대/없는 경로 거부. `-Apply` 테스트도 mock만 사용하여 전역 설정을 쓰지 않았다. 실제 dry-run은 연결 담당 메인이 별도로 확인했다.
- 초기 discovery 검증 요청에서 필수 `_meta`가 누락되어 `-32602`가 발생했고 공식 형식으로 고친 뒤 통과했다. 수제 protocol 처리는 추가하지 않았다.
- 최초 .NET 복원 로그에 아래 메시지가 출력됐다. cert-store 사전 baseline이 없어 새 인증서 설치 또는 신뢰 설정 변경 여부는 확정하지 않았다. 이후 실행과 위 빌드 예시는 `DOTNET_GENERATE_ASPNET_CERTIFICATE=false`를 유지한다. 인증서 삭제·추가 설치·신뢰 설정 변경 명령은 실행하지 않았다.

```text
ASP.NET Core HTTPS 개발 인증서를 설치했습니다.
인증서를 신뢰하려면 'dotnet dev-certs https --trust'를 실행하세요.
HTTPS에 관한 자세한 정보: https://aka.ms/dotnet-https
```

최종 fixture 검증 **76개 통과**: 동시 같은 키/다른 입력의 승자 1건·충돌 1건, busy 대기 중 취소와 동일 키 재시도, 완료 UPDATE 뒤 요청 기록 실패 rollback도 포함한다. 결과는 `verification/artifacts/b4f87e2b-d9f4-4a83-8a1a-fc39b5604c81/results.json`, 게시 폴더 파일별 경로/크기/SHA256은 `verification/artifacts/publish-manifest.json`에 기록한다. 이는 서버 단독 fixture 결과이며 후속 Qt 통합과 최종 등록 결과는 위에 별도로 기록했다. main exe 수동 클릭과 실사용 DB 업무 조회·변경은 수행하지 않았다.

### 독립 검토 후 C# Update 경쟁 수정

`TaskService.Update`의 사전 중복 검사 뒤 MCP가 같은 제목을 등록할 수 있는 경쟁을 수정했다. `TaskRepository.UpdateTask`도 `BEGIN IMMEDIATE` 안에서 자기 ID를 제외한 최종 중복 검사와 UPDATE를 함께 수행한다. 서비스 사전 검사·GUI 정규화·기존 한국어 오류 메시지는 유지한다. 중복·UPDATE 오류는 롤백하며 busy timeout은 기존 5초다.

새 source-linked harness 실행:

```powershell
$fixture = "$PWD\JSWorkPartner.Mcp\verification\artifacts\atomic-update-$([guid]::NewGuid())"
dotnet run --project JSWorkPartner.Mcp/verification/atomic-update/AtomicUpdate.csproj -c Release -- $fixture
```

2026-10-07 새 fixture 검증 **103개 통과**: 사전 조회 후 MCP 제목 선점→rename 거부, create/rename 동시 경쟁 30회에서 제목 하나만 저장, 자기 제목·변경 없는 제목의 정상 수정, GUI 정규화, NOCASE 중복 오류 표현, UPDATE 실패 rollback, busy 실패 원본 유지/잠금 해제 후 정상 수정, 없는 ID 오류 계약을 확인했다. 결과: `verification/artifacts/atomic-update-f50c79b9-cf4f-4eff-a8de-4bb717094312/results.json`. 이전 76 PASS fixture는 보존했다.

수정 후 locked restore와 self-contained 재게시 성공. 새 서버 프로세스에서 기존 stdio 재현도 **76개 재통과**했다(`verification/artifacts/2c666ef5-f7c9-4588-9c57-b00949e8a8f7/results.json`). 기존 C# 테스트 원문 source-linked 회귀도 통과했다(`atomic-update-f50c79b9-cf4f-4eff-a8de-4bb717094312/regression.txt`). 최종 게시 파일 manifest와 sourcehash는 `verification/artifacts/publish-manifest.json`, `verification/artifacts/sourcehash.json` 및 해당 atomic-update fixture 하위에 기록한다. 이 수정 단계의 .NET 실행은 인증서 자동생성 비활성화와 workspace 캐시 격리를 유지했다. C# 구현자는 실제 사용자 DB·설정·프로세스와 전역 MCP 등록에 접근하지 않았다. 이후 메인의 최종 등록은 위 등록 절과 같다. 최종 문서 정리에서는 root sourcehash의 문서 항목만 갱신하고 이전 fixture snapshot과 검증·게시 결과는 보존한다.

공식 근거: [C# SDK](https://github.com/modelcontextprotocol/csharp-sdk), [NuGet 2.2.0/net8 의존성](https://www.nuget.org/packages/ModelContextProtocol/2.2.0), [C# SDK lifecycle 설명](https://csharp.sdk.modelcontextprotocol.io/v2/concepts/stateless/stateless.html), [Codex app-server 공식 문서](https://learn.chatgpt.com/docs/app-server). 실행 API는 복원된 2.2.0 패키지 XML 문서와 실제 빌드·stdio 호출로 확인했다.
