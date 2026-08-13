# AKARI
AKARI は **A**nimation **K**ernel for **A**lgorithmic **R**endering & **I**llustration の略です。Vulkan をベースにした数学アニメーション作成ライブラリです。

## 開発環境

現在は Windows、Visual Studio 2022、CMake を使用します。GLFW 3.4 と GLM 1.0.3 は CMake の
`FetchContent` が初回 configure 時に取得するため、Ninja、vcpkg、手動インストールは不要です。

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

現在の検証用実行ファイルは次の場所に生成されます。

```text
build/windows-debug/Debug/akari_dependency_smoke.exe
```

### Test

ビルド後に全テストを実行します。

```powershell
ctest --preset windows-debug
```

失敗したテストの詳細は preset により自動的に表示されます。通常の開発では、最初に configure を一度行い、
以降は build と test を繰り返してください。
