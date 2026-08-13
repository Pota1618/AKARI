# AKARI
AKARI は **A**nimation **K**ernel for **A**lgorithmic **R**endering & **I**llustration の略です。Vulkan をベースにした数学アニメーション作成ライブラリです。

## 開発環境

現在は Windows、Visual Studio 2022、CMake、Vulkan SDK 1.3 以上を使用します。GLFW 3.4 と GLM 1.0.3 は
CMake の `FetchContent` が初回 configure 時に取得するため、Ninja、vcpkg、手動インストールは不要です。
Vulkan SDK は [LunarG](https://vulkan.lunarg.com/) から別途インストールし、`VULKAN_SDK` を設定してください。

### Configure

初回、または CMake の構成を変更したときに実行します。

```powershell
cmake --preset windows-debug
```

初回 configure にはインターネット接続が必要です。GLFW と GLM、および生成された Visual Studio project は
`build/windows-debug/` 以下に保存されます。

### Build

ソースを変更したら実行します。変更された部分だけが再ビルドされます。

```powershell
cmake --build --preset windows-debug
```

主な実行ファイルは次の場所に生成されます。

```text
build/windows-debug/Debug/akari_dependency_smoke.exe
build/windows-debug/Debug/akari_core_tests.exe
build/windows-debug/Debug/akari_vulkan_smoke.exe
build/windows-debug/Debug/akari_preview.exe
```

### Test

ビルド後に全テストを実行します。

```powershell
ctest --preset windows-debug
```

失敗したテストの詳細は preset により自動的に表示されます。通常の開発では、最初に configure を一度行い、
以降は build と test を繰り返してください。

テストには、GPU を必要としない scene/playback の単体テストと、非表示 GLFW window に1 frameを描画する
Vulkan validation smoke test が含まれます。

## M1: Deterministic Vulkan Preview

最初のマイルストーンでは、座標軸、単位円、`p(t) = (cos(t), sin(t))` で円周上を動く点を表示します。
scene は wall clock や Vulkan に依存せず、明示された時刻から毎 frame の geometry を決定的に生成します。

ビルド後、次のコマンドで preview を起動します。

```powershell
./build/windows-debug/Debug/akari_preview.exe
```

### Preview controls

| Key | Action |
| --- | --- |
| `Space` | 再生／一時停止 |
| `Left` / `Right` | 1/60 秒シーク |
| `Shift + Left` / `Shift + Right` | 1 秒シーク |
| `Home` | `t = 0` へ戻る |
| `Esc` | 終了 |

ウィンドウタイトルには現在時刻と再生状態が表示されます。resize、最小化、復帰時にはswapchainを安全に再生成します。
