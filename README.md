# JSWorkManager

Windows용 퀘스트 관리 애플리케이션입니다.

## 주요 기능

- 퀘스트 등록, 수정, 완료 및 삭제
- 중요도와 마감일 관리
- 마감 7일 전부터 하루 단위 색상 강조
- 기간을 설정할 수 있는 퀘스트 요약 및 복사
- 크기 조절 가능한 플로팅 퀘스트 보드
- 일반 모드와 다크 모드
- 시스템 트레이 실행 및 설정
- 퀘스트의 `#일감번호`를 Redmine 링크로 표시

## Redmine 연동

1. JSWorkManager의 `Settings...`에서 Redmine 도메인 주소를 입력합니다.
2. 퀘스트 제목이나 상세 설명에 `#3333`처럼 일감 번호를 작성합니다.
3. 퀘스트 상세 화면에서 해당 번호를 클릭하면 Redmine 일감이 열립니다.

## 개발 환경

- C++17
- Qt 5.9.9
- Visual Studio 2022
- SQLite

`JSWorkManager.sln`을 Visual Studio에서 열어 x64 구성으로 빌드할 수 있습니다.
