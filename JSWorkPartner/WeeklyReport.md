# 주간업무 보고서 (Qt 앱)

## 사용 흐름

1. 메인 화면의 **이번 주 요약 → 업무 보고서**를 연다. 기존 **목록 보기**는 SQLite 퀘스트 요약으로 유지된다.
2. **보고서 설정...**에서 Redmine 주소, API key, workspace를 등록한다. 기본은 HTTPS이며 HTTP 서버는 평문 전송 안내를 확인하고 **HTTP 평문 전송 허용**을 선택해야 저장·수집할 수 있다. 메인 **Settings**에서도 같은 설정을 열 수 있다. 보고서 설정은 별도로 저장된다.
3. 기본 금주/차주는 PC 현지 날짜의 월요일~일요일이다. 보고서 제목 날짜와 이름도 편집할 수 있다. 이름을 비워 두면 수집 시 현재 API 계정 이름을 사용한다.
4. **이력·이슈 수집 → 초안 만들기**를 누른다. 수집 중에는 취소할 수 있다. 오류/부분 실패는 하단 상태와 **수집 결과 / 오류** 탭에 표시된다.
5. **초안 편집**에서 섹션과 작업 요약을 수정하고, 차주 후보를 삭제/추가한다. 행을 선택하면 아래에 VCS 원문·폴더·리비전·일시 또는 Redmine 후보 근거가 표시된다. 분석/회의 등은 **수동 행 추가**로 보완한다.
6. **2열 미리보기**를 확인한 뒤 **HTML 복사** 또는 **HTML 파일 저장**으로 내보낸다. 클립보드에는 HTML과 일반 텍스트가 함께 들어간다. 붙여넣는 앱의 HTML 지원에 따라 모양이 달라질 수 있다.

초안은 대화상자의 메모리에만 유지된다. 다시 수집하면 수동 편집/추가도 교체하므로 교체 여부를 확인한다. 영구 초안 저장/불러오기는 이번 버전에 포함하지 않는다. 편집한 기간이 실제 수집 기간과 달라지면 미리보기와 HTML에 원래 수집 기간을 표시한다.

## Workspace 설정

| 필드 | 의미 |
| --- | --- |
| 보고서 섹션 | 보고서에서 사용할 프로젝트 제목. 여러 폴더에 같은 이름을 사용하면 합쳐진다. |
| VCS | Git 또는 SVN |
| Working copy 경로 | 기존 로컬 working copy의 절대 폴더 경로 |
| 작성자 | Git author 이름 또는 이메일의 정확한 값. 이메일만 대소문자를 무시한다. SVN은 정확한 작성자 계정 ID. |
| Redmine 프로젝트 ID/식별자 | API에서 프로젝트를 조회해 보고서 섹션에 연결한다. 같은 프로젝트에는 같은 섹션명을 지정한다. 비우면 그 폴더는 VCS 수집만 한다. |

API key, 이름, 작성자 계정은 코드에 고정하지 않는다. 프로젝트 연결을 하나 이상 지정하면 차주 후보는 성공적으로 확인된 연결 프로젝트에 한정된다. 연결을 하나도 지정하지 않으면 현재 담당자의 전체 열린 이슈를 Redmine 프로젝트명 아래 제공한다. VCS 메시지의 이슈 번호는 현재 담당 이슈와 일치하면 해당 이슈의 실제 프로젝트에 연결한다.

## 수집과 초안 규칙

- Git은 해당 폴더의 현재 `HEAD`에서 접근 가능한 커밋을 읽는다. fetch/pull이나 다른 브랜치 조회는 하지 않는다. 기간은 author date가 아닌 **committer date**, 작성자는 **author**로 판단한다. **Git 2.37 이상**의 `--since-as-filter`를 사용해 부모보다 자식의 committer 시각이 이전인 이력도 빠뜨리지 않는다. 구버전에서 부정확한 `--since`로 전환하지 않고 명령 실패를 표시한다.
- SVN은 working copy 경로의 로그를 기간으로 조회하고 작성자와 현지 날짜를 다시 확인한다. SVN 인증은 미리 구성된 인증 캐시/기존 클라이언트 설정을 사용하며 대화식 입력은 하지 않는다.
- 금주 행은 Git/SVN 이력이 있어야 생성된다. `#번호`가 현재 API 사용자 담당 이슈에 연결되면 이슈 제목·번호로 합치고 현재 상태를 편집 화면에 표시한다. 같은 이슈의 여러 커밋/리비전은 한 행의 근거로 모인다. 이슈 제목은 완료 여부를 추론한 문장이 아니라 편집할 초안이다.
- 연결되지 않은 메시지는 workspace 섹션 아래에 보존하고 같은 메시지를 합친다. 여러 번호 중 일부만 연결되면 원문 행도 남겨 연결되지 않은 작업을 보존한다. 개행 등 공백만 정리하며 원문은 근거에 남긴다.
- Redmine에서는 `users/current`로 현재 사용자를 먼저 확인하고, 그 **숫자 사용자 ID**를 `assigned_to_id`에 전달해 전체 상태 이슈와 열린 이슈를 각각 페이지 단위 조회한다. `me`는 소속 그룹의 담당 이슈까지 포함할 수 있어 개인 보고서에 사용하지 않는다. 응답의 담당자 ID도 현재 사용자와 비교한다. 열린 이슈는 차주 **계획 후보**이며 다음 주 마감일로 제한하거나 자동 확정하지 않는다.
- Redmine 실패는 조회 실패로 표시하며 VCS 이력은 유지한다. 진행 중 취소/부분 실패 시 완료된 폴더와 페이지의 결과만 초안에 반영한다. 실패한 폴더의 부분 출력은 사용하지 않는다.
- 외부 도구는 `QProcess`의 프로그램/인자 배열, HTTP는 `QNetworkAccessManager`를 사용한다. 둘 다 이벤트 기반 비동기 실행이다. VCS는 폴더당 60초, API는 요청당 30초, 응답은 16 MiB, 이슈 조회는 목록당 10,000건까지로 제한한다. 제한 도달도 누락으로 표시한다.

## 보안과 설정

- 보고서 설정은 기존 앱의 `QSettings` 안의 `weeklyReport/*`에 저장하고 Redmine 주소는 기존 `redmine/baseUrl`을 공유한다. SQLite, 공유 DB 스키마 및 저장 위치는 변경하지 않는다.
- API key는 Windows DPAPI로 암호화한 바이트만 저장한다 (`weeklyReport/apiKeyDpapi`). 현재 Windows 사용자에 묶이고 저장한 서버 주소와도 연결된다. 서버를 바꾸면 새 key를 입력하거나 기존 key를 삭제해야 한다. 다른 Windows 계정으로 설정을 복사하면 복호화가 실패할 수 있다.
- API key는 URL/쿼리나 오류 로그에 넣지 않고 `X-Redmine-API-Key` 헤더로만 전송한다. HTTPS가 기본이며 **HTTP 평문 전송 허용**은 기본 false로 `weeklyReport/allowHttp`에 저장한다. HTTP에서는 API key와 업무 내용이 암호화되지 않고 전송된다. 허용 옵션만 바꿔도 다른 서버나 HTTP/HTTPS 간 기존 key를 재사용하지 않으며 새 key 입력 또는 기존 key 삭제가 필요하다.
- HTTP/HTTPS 외 scheme, host 없는 주소, 주소에 포함된 사용자명/암호·쿼리·fragment는 거부한다. 포트와 Redmine 하위 경로는 유지한다. 리디렉션을 따라가지 않고 HTTPS 인증서 오류를 무시하지 않는다. 자동 HTTPS→HTTP 전환은 없다. 실패 시 서버 응답 원문/도구 stderr를 출력하지 않는다.
- Redmine 서버의 REST API 활성화, 사용자 이슈/프로젝트 조회 권한, **Git 2.37 이상**의 `git.exe` 및 `svn.exe`의 PATH 등록이 필요하다. HTTPS 조회에는 해당 Qt 5.9.9 배포본이 사용하는 TLS 런타임이 필요하다. 사용할 수 없으면 명시적으로 수집 실패를 표시한다. 빌드의 `windeployqt` 단계는 외부 TLS 라이브러리를 자동 보장하지 않는다.
- Notion 직접 연동은 포함하지 않는다.

API 규칙: [Redmine Issues REST API](https://www.redmine.org/projects/redmine/wiki/Rest_Issues), [Redmine Users REST API](https://www.redmine.org/projects/redmine/wiki/Rest_Users).

## 기존 MVP 검증 기록 (HTTP 지원 전)

Qt 5.9.9 / Visual Studio 2022 / C++17 Release x64 제품 빌드는 exit 0으로 성공했다. 기존 Output을 보존하고 obj/WeeklyReportBuild/bin에 출력했다. 정확한 명령·로그·종료 코드는 obj/WeeklyReportReview/fixed-fusion에 보관했다. 마지막 성공 빌드와 현재 제품 소스/프로젝트 25개 SHA256을 대조했다. 상세 결과는 obj/WeeklyReportReview/ReviewStatus.md에 있다.

실제 제품 클래스를 호출해 숫자 Redmine 사용자 ID 요청 및 개인 이슈 수집(프로토콜 fixture), Git 작성자/날짜 경계·committer 날짜 역전 부모 수집, SVN 작성자/날짜 경계·비동기 명령·XML 파싱, malformed 응답 판정을 통과했다. 실서버/개인 key는 미사용이다.

Windows UI는 17개 assertion 중 15 PASS / 2 FAIL, exit 1이었다. 이슈·미연결/혼합 번호 원문 묶음과 근거 보존, HTML escape/링크/2열/중복 제거, 일반 편집, 날짜 provenance, 격리 QSettings/DPAPI와 서버 불일치 거절, queued start/cancel 단일 완료 PASS다. 재수집 질문의 실제 No 클릭 후 편집/수동 초안 보존도 PASS다.

닫기 modal은 실제 완료 callback이 질문 중 collector_=nullptr로 바뀐 뒤 Yes 두 번(닫기 질문 및 dirty 초안 경고)을 클릭해 안전하게 닫혔다. exit 0 / FAILURES=0으로 guard 동적 검증 PASS다. 초기 offscreen 접근 위반은 Qt5.9.9 Windows QMessageBox의 null platformNativeInterface 역참조로 검증 플랫폼 문제다. 수정 전 제품 race의 Windows 동적 재현은 미수행이다. 상세 링크 두 갱신 경로는 정적 확인+Release 빌드로 검증했다.

클립보드 assertion 두 실패(HTML/plain clipboard, focused editor export buffer)는 보존한다. 첫 버튼 callback 한 번 실행, 버튼 활성, 복사 슬롯 진입과 reportHtml 합성 문자열을 확인했다. focus commit 뒤 활성 편집 버퍼는 row와 reportHtml에 반영됐다. 이벤트 처리·포커스·활성화 뒤에도 Qt clipboard 형식과 본문이 비었고 순수 Qt control도 동일했다. 쓰기 없는 native probe는 OpenClipboard=false, GetLastError=5 (Access denied), COM apartment HRESULT=0이었다. 현재 권한의 네이티브 접근 제한으로 종단 검증이 차단됐으며 제품 복사 자체의 성공·실패를 단정하지 않는다. 읽기 probe exit 0은 전체 UI 통과가 아니다. HTML 본문 생성 검증과 native clipboard 종단 검증을 분리하며 정상 데스크톱에서 HTML/plain 전달의 추가 확인이 필요하다.

제품 산출 폴더 TLS probe는 supportsSsl=false, build=OpenSSL 1.0.2t, runtime 빈 문자열이다. 현재 Redmine HTTPS 수집은 차단된다. 조사한 로컬 제품/Qt/도구/Windows/기존 Output 범위에서 호환 libeay32.dll/ssleay32.dll을 찾지 못했다. 해당 Qt 배포본과 맞는 TLS 환경 준비·재확인이 필요하다. DLL 다운로드·설치·배포, 권한 모드·Windows 정책·사용자/시스템 PATH 변경은 하지 않았다.

전체 통과가 아니다. 실서버/인증·권한/HTTPS 종단, 실제 HTML 파일 저장 대화상자, 실제 TaskQueueWidget 상세 링크 UI는 미검증이다. 원본 SQLite·실사용 QSettings·WPF·기존 Output은 변경하지 않았고 기존 테스트 프로젝트도 실행하지 않았다. 이 기록은 기존 MVP 검증 당시 결과이며 이후 `cd7503b`로 커밋했다.

## HTTP 명시 허용 후속 검증

설정에서 **HTTP 평문 전송 허용**을 선택하면 HTTP 서버를 저장하고 수집할 수 있다. HTTPS는 기본 허용이며 HTTP는 옵션이 false일 때 저장·수집을 차단한다. HTTPS에서만 TLS 런타임을 검사하므로 TLS가 없는 환경에서도 명시적으로 허용한 HTTP 수집은 진행한다. HTTP는 API key와 업무 내용의 평문 전송 위험이 있으므로 HTTPS를 우선 사용한다.

이번 변경은 `WeeklyReportSettings.cpp/.h`, `WeeklyReportCollector.cpp`, 이 문서에 한정된다. 실제 제품 클래스를 컴파일해 호출하는 harness·가짜 key·응답·설정과 로그는 `obj/WeeklyReportHttpReview`에 격리했다. 실제 설정 대화상자에서 HTTP 기본 저장 거부, 체크박스와 인라인 안내, 명시 허용 저장/로드, HTTPS 기본 허용, scheme 변경 시 기존 key 재사용 거부를 확인했다. URL의 잘못된 scheme·사용자정보·쿼리·fragment·host 누락·잘못된 포트 등을 거부하고 정상 포트/하위 경로는 보존했다. DPAPI key 복호화와 다른 host/scheme 거절, TLS 없는 HTTP Collector 경로 및 HTTP 이슈 HTML 링크도 확인했다.

최종 관련 harness는 exit 0 / FAILURES=0, timeout 없음이다. 제품 DLL 환경에서 `supportsSsl=false`인 상태로 `127.0.0.1`의 임시 HTTP fixture에 실제 사용자/이슈 요청 3건을 보내고 계획 후보를 수집했다. 키는 헤더에만 전달하고 요청 URL에 포함하지 않는 것을 확인했다. mock request에서 숫자 담당자와 수동 리디렉션 정책도 확인했다. HTTPS는 TLS 런타임 실패로 요청을 보내지 않았으며 HTTP fallback은 없었다. 초기 harness의 생성 헤더 include 누락과 자동응답 타이머 timeout은 테스트 환경을 보완한 뒤 재실행했고 초기 로그도 보존했다.

새 제품 Release x64 빌드는 exit 0이며 실행 파일은 `obj/WeeklyReportHttpBuild/bin/JSWorkPartner.exe`다. 명령·빌드 로그·종료 코드·harness stdout/stderr·소스/실행 파일 해시는 `obj/WeeklyReportHttpReview/results`에 있다. 실행 중인 기존 앱을 종료하거나 기존 Output을 교체하지 않았다.

이 결과는 HTTP 관련 최소 검증만의 결과다. 기존 클립보드 종단 검증 차단과 실서버/HTTPS 종단 미검증 한계는 해소됐다고 주장하지 않는다. 실제 사용자 서버·키·QSettings·DB·클립보드는 사용하지 않았고 외부 서버 접속, 임의 설치, 권한/Windows 정책/시스템 PATH 변경은 하지 않았다. 기존 UI/VCS 테스트 전반은 다시 실행하지 않았다. 후속 변경은 dev 작업 트리에 미커밋 상태로 남기며 commit/push하지 않는다.
