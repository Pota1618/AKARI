# AGENTS.md

このファイルは AKARI リポジトリ全体に適用します。下位に `AGENTS.md` または `AGENTS.override.md` がある場合は、
そのディレクトリ配下で下位の指示を優先します。

## Project Overview / Current State

AKARI は **Animation Kernel for Algorithmic Rendering & Illustration** の略で、Vulkan を基盤にした、
数学・アルゴリズム解説向けのインタラクティブなアニメーションエンジンを目指します。

現在は M3 まで実装され、dimension-neutral な scene/session/snapshot、`Canvas2D`、stable `NodeId`、型付き property、
非破壊 take、semantic drag recording、120 Hz fixed-step simulation、Unit Circle／Spring Mass preview、offscreen PNG、
画像回帰、CPU-only CI が利用できます。3D、serialization、simulation checkpoint、動画出力、自動再ビルド、状態復元は
未実装です。存在しないコマンドを利用可能と説明・実行せず、追加時は本ファイルと利用者向け文書も更新してください。

## Product Contract

- レンダリングコアと第一級のシーン記述 API は C++23 で実装します。
- GPU バックエンドは Vulkan とします。拡張境界は保ちますが、初期実装で複数 GPU API は不要です。
- Windows を最初の正式対象とします。ただし、Win32 固有処理を `scene`、`timeline`、数学処理、または
  バックエンド非依存の描画データへ漏らしてはいけません。Linux/macOS への移植を妨げない境界を保ってください。
- ライブプレビューは、再生、一時停止、任意時刻へのシーク、カメラ操作、マウス入力、キー入力を扱います。
- 将来は外部 preview host が増分ビルド後に process を再起動します。初期版で DLL hot reload や安定 ABI は要求しません。
- 再起動時は可能な限り scene identifier、timeline time、再生状態、記録済み入力を復元します。対象シーンや
  復元対象がなくなった場合は、理由を明示する診断を出し、安全にシーン先頭へ戻してください。
- 同じ scene evaluation からライブ表示、PNG 静止画、フレーム連番、FFmpeg 動画を生成します。preview と
  offline render の見た目や時間意味論が別実装へ分岐してはいけません。

## Public Interface Direction

- 公開 scene model は Vulkan、ウィンドウシステム、FFmpeg の型に依存させないでください。
- scene は任意の timeline time を直接評価できるものとします。直前フレームを順番に実行しないと正しい状態に
  到達できる設計を、公開 API の前提にしてはいけません。
- 入力は timestamp、対象、payload、順序を持つ記録・再生可能な event として扱います。対話入力で変化する値も、
  offline render で再現できる明示的な state に反映してください。
- 公開 API は小さく保ち、所有権、寿命、単位、失敗条件を明記してください。
- 公開 API の追加・変更には、最小のサンプル、挙動を固定するテスト、利用者向け説明を同じ変更に含めます。
- クラス名、座標系、ABI、例外方針など未決の契約を推測で固定しないでください。後戻りしにくい公開契約を
  導入する前に、選択肢、理由、互換性への影響を設計文書または ADR として明文化してください。

## Architecture Guardrails

依存方向を一方向に保ち、少なくとも次の責務を分離してください。ディレクトリ名は設計時に決めます。

- **scene / timeline**: authored objects、animation、state dependency、時間評価。GPU と OS に依存しない純粋なコア。
- **render data / renderer interface**: 評価済みシーンを、明示的所有権の描画データへ変換する境界。
- **Vulkan backend**: device、queue、resource、pipeline、synchronization、presentation の実装。
- **platform / input**: window、clock source、filesystem watching、mouse/keyboard event の OS アダプター。
- **preview orchestration**: build、process restart、状態保存・復元、対話的な playback control。
- **export / encoding**: offscreen frame、PNG、image sequence、FFmpeg との連携。scene の時間意味論を持たない層。

以下は全層に適用する不変条件です。

- preview と offline render は、同じ scene/timeline evaluator と renderer を使用します。異なるのは scheduler、
  render target、出力先だけにしてください。
- wall-clock time を scene evaluation の暗黙入力にしないでください。時刻、delta、frame index、frame rate、seed は
  evaluation context の明示的な値として渡します。
- 同じ scene、時刻、seed、入力履歴、render settings から同じ結果を得られることを優先します。並列化やキャッシュで
  観測可能な結果を変えてはいけません。
- seek は前方・後方・ランダム順のいずれでも正しく動作させます。キャッシュは最適化であり、正しさの前提ではありません。
- OS イベントを scene object へ直接配信せず、platform-neutral event へ正規化してから扱います。
- 長時間処理、ファイル I/O、shader compilation、動画 encode、GPU wait を対話フレームの主経路で同期実行しないでください。

## Vulkan and Performance Rules

- Vulkan resource は RAII で所有し、device/allocator と子 resource の破棄順序を型または明示的な owner で表現します。
  raw handle を所有権の説明なしに長期保存しないでください。
- in-flight resource の破棄・再利用は fence、timeline semaphore、または同等の明示的な完了条件に基づけます。
  `deviceWaitIdle` を通常フレームの寿命管理手段として使わないでください。
- Debug build と GPU smoke test では Vulkan validation layer を有効にし、error レベルの validation message を
  テスト失敗として扱います。
- 毎フレームの heap allocation、全量 upload、pipeline 再生成、blocking readback を避けます。ただし、測定前に
  複雑な最適化を導入せず、性能変更には再現可能な計測結果を添えてください。
- GPU 機能がない環境でも scene/timeline/state のテストを実行できるようにし、正しさの大部分を GPU から分離します。
- shader、生成コード、埋め込み binary には生成元と再生成手順を用意し、生成物を手作業で編集しないでください。

## Toolchain and Workflow

標準は C++23、CMake Presets、Visual Studio 2022/MSVC、CMake `FetchContent` とし、Ninja と vcpkg は前提にしません。
compiler-specific extension は隔離し、標準 C++ の代替を説明してください。

標準コマンドは次のとおりです。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

- 作業開始時に `README.md`、本ファイル、対象ディレクトリにある追加の `AGENTS.md`、現在の差分を確認します。
- ユーザーの既存変更を保持し、依頼外の整形、名前変更、生成物更新を混ぜないでください。
- build/download/render output、動画、一時 asset を追跡対象へ追加しないでください。
- dependency は tag/commit で固定し、用途、代替案、license、移植性、binary size/build time の影響を記録します。
- formatter 等は既存設定だけを使い、設定なしで大規模な機械的書き換えを行わないでください。
- 変更はレビュー可能な単位に保ち、振る舞いを変える変更と純粋なリファクタリングを可能な限り分離します。

## Testing and Definition of Done

テストは次の層を維持し、機能とともに必要な層を追加します。

1. **CPU unit tests**: timeline、state dependency、interpolation、event ordering、serialization。GPU と window は不要。
2. **Determinism/property tests**: 前方・後方・ランダム seek、異なる frame rate、同じ seed、入力 replay の一致。
3. **Vulkan smoke tests**: headless/offscreen を優先し、validation error、resource lifetime、resize/recreation を確認。
4. **Image regression tests**: 固定 scene と設定を許容差付きで比較。圧縮画像の byte-for-byte 一致だけに依存しない。
5. **Workflow tests**: source change 後の増分 build、preview restart、state restore、復元不能時の診断と先頭 fallback。
6. **Export tests**: preview と offline の選択時刻の frame 一致、PNG/sequence の個数・寸法・timestamp、FFmpeg failure。

変更を完了とする前に、適用可能な build、test、static analysis、validation を実行し、実行したコマンドと結果を報告します。
GPU、SDK、FFmpeg などがなく実行できない検証は、成功したものとして扱わず、未実行の理由と残るリスクを明記してください。
バグ修正には、可能な限り修正前に失敗し修正後に成功する回帰テストを追加します。

## Interactive Preview Debugging with Computer Use

Computer Use は常時必須ではなく、CPU/GPU test 後に drag、focus、keyboard、resize、最小化復帰、mode/take 表示など、
実ウィンドウでしか確認しにくい preview の受け入れを再現する場合に使います。

- `computer-use` skill と指定された文書を先に読み、Windows操作は`node_repl`の`@oai/sky`だけで行います。独自helper、
  PowerShell UI Automation、terminal UIを混ぜません。
- build済み`.exe`を明示pathから起動し、返されたprocess/windowから対象を一つに絞ります。観測後は一操作だけ行って再観測し、
  古い座標、screenshot id、element indexを再利用しません。候補が複数なら操作を止めます。
- Spring Massではplay/pause、Edit/Record、drag、take、resize、最小化復帰、終了を確認し、起動したpreviewを閉じます。
- CTest、determinism test、Vulkan validation、画像回帰の代替にしません。detachしたGUIのUI smokeとvalidation結果を分けます。
- capture待機による`steady_clock`の飛びや、拡張キーだけが注入されない場合があります。手動操作とcontroller testで比較します。
- 利用不能なら手動項目を提示して未検証と報告します。runtime pathの`EPERM lstat`はsandbox/elevationを確認します。
  `sandbox = "unelevated"`は動作実績がありますが、設定変更はユーザーの許可なく行いません。

## Code Review Rules

レビューでは特に次を指摘してください。

- wall clock、unordered iteration、未固定 seed、race、frame accumulation による非決定性。
- preview と offline render で重複または矛盾する scene evaluation、animation、rendering logic。
- scene/timeline 層への Vulkan、FFmpeg、Win32 型や副作用の漏出。
- Vulkan handle の二重破棄、親より長生きする子 resource、in-flight resource の早期再利用、同期不足。
- render loop 上の毎フレーム allocation、同期 I/O、無条件 GPU wait、shader/pipeline 再生成。
- seek、resize、device loss、build failure、scene removal、状態復元失敗を silent に無視する処理。
- public API の所有権・単位・失敗条件、サンプル・回帰テストの欠落。
- license・移植性未確認の dependency、参照実装からの出典不明なコード・asset。

## Reference Projects and Licensing

[SwapTube](https://github.com/2swap/swaptube) のstate/build境界と、[ManimGL](https://github.com/3b1b/manim) のライブ制作体験を
参考にします。移植時はlicense、attribution、配布条件を確認し、出典不明のコード・assetを持ち込まないでください。
