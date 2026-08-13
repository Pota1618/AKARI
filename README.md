# AKARI
AKARI は **A**nimation **K**ernel for **A**lgorithmic **R**endering & **I**llustration の略です。Vulkan をベースにした数学アニメーション作成ライブラリです。

## 開発環境

現在は Windows、Visual Studio 2022、CMake、Vulkan SDK 1.3 以上を使用します。GLFW 3.4、GLM 1.0.3、
Vulkan Memory Allocator 3.3.0、stb_image_write 1.16 は
CMake の `FetchContent` が初回 configure 時に取得するため、Ninja、vcpkg、手動インストールは不要です。
Vulkan SDK は [LunarG](https://vulkan.lunarg.com/) から別途インストールし、`VULKAN_SDK` を設定してください。

### Configure

初回、または CMake の構成を変更したときに実行します。

```powershell
cmake --preset windows-debug
```

初回 configure にはインターネット接続が必要です。取得した依存と生成された Visual Studio project は
`build/windows-debug/` 以下に保存されます。

### Build

ソースを変更したら実行します。変更された部分だけが再ビルドされます。

```powershell
cmake --build --preset windows-debug
```

利用者向けの実行ファイルは次の場所に生成されます。

```text
build/windows-debug/Debug/akari_preview.exe
build/windows-debug/Debug/akari_capture.exe
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

## Source layout

- `include/akari/`: engineとapplicationが共有するC++ interface
- `src/core/`、`src/image/`、`src/vulkan/`: engine libraryの実装
- `src/preview/`、`src/capture/`: 利用者向けapplication entry point
- `tests/`: unit test、dependency smoke test、Vulkan smoke test、およびtest target用CMake定義

テスト専用コードは`tests/`に限定し、production libraryやapplicationのsource listへ含めません。通常の成果物だけを
構成したい場合は、configure時に`-DBUILD_TESTING=OFF`を指定できます。

## M2: Vulkan Backend Foundation & Offscreen Capture

Vulkan backendはdevice/context、VMA資源管理、frame scheduling、scene draw pass、swapchain/offscreen targetに分離されています。
previewとoffscreen captureは同じ`SceneFrame2D`、camera計算、shader、draw passを使用します。geometryはframeごとのstaging bufferから
device-local bufferへ転送され、容量不足時には安全に自動拡張されます。

ウィンドウを作成せず、デモシーンの任意時刻をPNGへ保存できます。

```powershell
./build/windows-debug/Debug/akari_capture.exe --output demo.png --time 1.57079632679
```

利用可能な引数は次のとおりです。

```text
akari_capture --output <path.png>
              [--time <seconds>]
              [--width <pixels>]
              [--height <pixels>]
```

既定値は`time=0`、`width=800`、`height=600`です。timeは`[0, 2π]`へclampされます。出力は左上原点の
unpremultiplied sRGB RGBA8 PNGです。PNG出力は単一frameの同期処理であり、frame連番と動画encodingは未実装です。

VMAはbuffer/image allocationとpersistent mappingのために採用しています。個別`VkDeviceMemory` allocationや自前suballocatorより
実績のある移植可能な資源管理を優先したものです。stb_image_writeはPNG encoderだけを小さく隔離するために採用しています。
Windows固有のWICは移植性がなく、より大きな画像処理libraryはM2の用途に過剰なため採用していません。
VMAはMIT、stb_image_writeはpublic domainまたはMITのdual licenseです。固定したrevisionは`CMakeLists.txt`を参照してください。

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
