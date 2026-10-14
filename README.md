# JSWorkPartner

Windows용 퀘스트 관리 애플리케이션입니다.

## 주요 기능

- 퀘스트 등록, 수정, 완료, 진행 중으로 복원 및 삭제
- 기존 퀘스트를 새 진행 퀘스트로 복제
- 중요도와 마감일 관리
- 마감 7일 전부터 하루 단위 색상 강조
- 열 머리글을 이용한 목록 정렬 및 정렬 상태 저장
- 기간을 설정할 수 있는 퀘스트 요약 및 복사
- 선택한 퀘스트 정보를 클립보드로 복사
- SQLite 데이터 파일 위치 열기 및 수동 백업
- 크기 조절 가능한 플로팅 퀘스트 보드
- 일반 모드와 다크 모드
- 시스템 트레이 실행 및 설정
- 퀘스트의 `#일감번호`를 Redmine 링크로 표시

## 주요 단축키

- `Ctrl+Enter`: 새 퀘스트 저장 또는 수정 내용 저장
- `Ctrl+N`: 새 퀘스트 입력 화면으로 이동
- `F2`: 선택한 퀘스트 수정
- `Esc`: 퀘스트 수정 취소
- `Ctrl+C`: 표에서 선택한 퀘스트 정보 복사
- `Delete`: 선택한 퀘스트 삭제 확인

완료 목록에서 퀘스트를 선택하면 상단 버튼 또는 우클릭 메뉴로 다시
진행 중 상태로 복원할 수 있습니다. 데이터 백업은 `Settings...`의
`데이터 저장 위치` 영역에서 만들 수 있습니다.

## Redmine 연동

1. JSWorkPartner의 `Settings...`에서 Redmine 도메인 주소를 입력합니다.
2. 퀘스트 제목이나 상세 설명에 `#3333`처럼 일감 번호를 작성합니다.
3. 퀘스트 상세 화면에서 해당 번호를 클릭하면 Redmine 일감이 열립니다.

## 구현 버전

- `JSWorkPartner`: 기존 C++17 + Qt 애플리케이션
- `JSWorkPartner.CSharp`: 동일한 SQLite DB를 사용하는 C# + WPF 애플리케이션

두 버전은 `JSWORKPARTNER_DATA_DIR` 환경 변수를 우선 사용하고, 값이 없으면
기존 `JSWORKMANAGER_DATA_DIR` 값을 사용합니다. 두 변수 모두 없으면 기존
`%APPDATA%\JSWorkManager\JSWorkManager\work_manager.db` 경로를 그대로 사용해
이름 변경 후에도 기존 데이터를 이어서 사용할 수 있습니다.

C++ 버전은 기존 Qt 설정을 새 제품의 설정에 없는 항목만 가져옵니다.
이전 C++ 버전과의 중복 실행을 막는 Mutex 식별자도 유지합니다.
같은 DB 파일을 사용할 수 있지만, 편집 충돌을 피하려면 한 번에 한 버전에서
작업하는 것을 권장합니다.

## 개발 환경

- C++17
- Qt 5.9.9
- Visual Studio 2022
- SQLite
- .NET 8 SDK 대상 및 WPF (C# 버전)
- Windows 내장 SQLite(`winsqlite3.dll`, C# 버전에서 사용)

C# 버전에는 외부 NuGet 패키지가 필요하지 않습니다. Windows 10/11에 포함된
SQLite 엔진을 사용하므로 별도 라이브러리 설치 없이 빌드할 수 있습니다.

`JSWorkPartner.sln`을 Visual Studio에서 열어 x64 구성으로 빌드할 수 있습니다.

C# 버전만 명령줄에서 빌드하고 실행하려면 다음 명령을 사용합니다.

```powershell
dotnet build .\JSWorkPartner.CSharp\JSWorkPartner.CSharp.csproj
dotnet run --project .\JSWorkPartner.CSharp\JSWorkPartner.CSharp.csproj
```

표시 버전의 마지막 자리는 `Release 1.0.0` 이후 `origin/main`에 반영된
커밋 수로 빌드 시 자동 생성됩니다. 예를 들어 세 커밋이 푸시된 상태에서
빌드하면 버전은 `1.0.0.3`으로 표시됩니다.

## 자동 테스트

솔루션의 `JSWorkPartner.Tests` 프로젝트는 운영 데이터와 분리된 임시 SQLite
DB에서 다음 흐름을 검증합니다.

- 퀘스트 등록과 입력값 정규화
- 중복 제목, 잘못된 중요도 및 외부 링크 거부
- 정렬 후 ID 기반 수정
- 완료 처리와 진행 중 복원
- 삭제
- 이전 버전 DB 컬럼 마이그레이션
- 중단된 제목 마이그레이션 자동 복구
- 손상된 DB 시작 차단

Release x64로 솔루션을 빌드한 뒤 다음 파일을 실행합니다.

```powershell
.\Output\Tests\Release\JSWorkPartner.Tests.exe
```

모든 검증이 성공하면 종료 코드 `0`과
`All task workflow tests passed.`가 출력됩니다.

C# 버전의 동일한 핵심 흐름 테스트는 다음과 같이 실행합니다.

```powershell
dotnet run --project .\JSWorkPartner.CSharp.Tests\JSWorkPartner.CSharp.Tests.csproj
```

성공 시 `All C# task workflow tests passed.`가 출력됩니다.

## 대화로 로컬 업무 관리(MCP)

Qt 앱은 작은 바탕화면 로봇에서 기존 업무 추가·목록·주간보고서와 AI 대화창을 연다. AI는 검증된 Codex CLI와 현재 ChatGPT 로그인을 재사용하며 별도 API 키를 사용하지 않는다. 대화는 Codex 서비스로 전송되고, 요청한 업무 도구의 내용도 대화에 포함될 수 있다. 앱은 표시 대화·작성 중 입력을 DB 폴더의 별도 DPAPI 암호화 스냅샷에 보관하고, 재실행 때 이어 말하기/새 대화를 선택한다. 복원 선택 자체는 AI나 이전 업무 요청을 실행하지 않는다. 본문·프로토콜·인증 값은 로그/QSettings에 기록하지 않으며 Codex 내부 상태·진단 파일은 생성될 수 있다. 연결 조건, 트레이 사용, 격리·재시도 정책과 검증 경계는 [docs/DesktopPet.md](docs/DesktopPet.md)를 참고하세요.

로봇 설정은 로봇 우클릭의 `로봇 설정...` 또는 앱 `Settings...`의 `로봇 설정 열기...`에서 연다. 크기(25/50/75/100/125/150%)·표시·항상 위는 저장 즉시 적용되고 취소·닫기는 변경을 버린다. AI 연결 정보는 읽기 전용이며, 첫 대화를 보내면 로그인과 연결 상태를 확인한다.

`JSWorkPartner.Mcp`는 업무 추가·조회·완료용 독립 stdio 서버입니다. 공식 C# MCP SDK 의존성을 고정하고 기존 저장소 소스를 공유합니다. 게시 실행 파일, 필수 DB 경로 설정, 기본 dry-run 등록 스크립트, 도구 사용법과 격리 검증 기록은 [docs/Mcp.md](docs/Mcp.md)를 참고하세요.

앱 DB 디렉터리는 `JSWORKPARTNER_DATA_DIR` > `JSWORKMANAGER_DATA_DIR` > `%APPDATA%/JSWorkManager/JSWorkManager` 순서로 선택하며 파일명은 `work_manager.db`입니다. MCP 서버와 `scripts/Register-JSWorkPartnerMcp.ps1`에는 기존 DB의 절대 파일 경로를 명시해야 합니다. 등록 스크립트는 `-Database`가 필수이며 `-Apply`를 지정해야 실제 연결을 등록합니다.

바탕화면 로봇과 AI 대화, 외부 업무 변경 감지 및 로컬 완료 업무의 주간보고서 반영을 사용하려면 기존 `Output/Release` 앱을 완전히 종료하고 `JSWorkPartner/obj/PetSettingsBuild/status-fix/bin/JSWorkPartner.exe`를 실행하세요. MCP 등록 후 기존 에이전트는 reload 또는 새 세션이 필요합니다. 도구가 반영된 세션에서 `[Updater] 테스트 업무 추가해줘`처럼 요청할 수 있습니다. 이 예시 업무는 실사용 DB에 생성하지 않았습니다.
