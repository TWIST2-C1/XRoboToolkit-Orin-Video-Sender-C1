# C1 カメラ対応 実装計画

## 背景・目的

現在 [main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) は ZED ステレオカメラ SDK (`sl::Camera`) に強く依存しており、C1 カメラでは動作しない。  
C1 カメラは Linux 上では **V4L2 (UVC) デバイス** (`/dev/video*`) として認識されるため、ZED SDK を排除し OpenCV + GStreamer の `v4l2src` に置き換える。  
Linux 専用とし、Windows ビルドはエラーが出ても構わない。

## 変更方針

| 方式 | 内容 |
|------|------|
| 新しいエントリポイントを作成 | `main_c1_tcp.cpp` を新規作成し、既存の [main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) は変更しない |
| カメラ抽象 ヘッダを作成 | `c1_camera.hpp` で C1 カメラの初期化・フレーム取得 I/F を定義 |
| GStreamer パイプライン変更 | `appsrc` + `sl::Camera` → `v4l2src` 直接入力 へ切り替え |
| Makefile 更新 | `main_c1_tcp.cpp` をビルドする `C1` ターゲットを追加 |

> [!NOTE]
> [main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) は手を加えないので ZED ビルドはそのまま維持される。

---

## 提案する変更ファイル

### 1. カメラ抽象レイヤー

#### [NEW] c1_camera.hpp

`C1Camera` クラスを定義する。内部では OpenCV の `cv::VideoCapture` を使って `/dev/video*` を開き、フレームを `cv::Mat` (BGRA) として返す。

主なインタフェース:
```cpp
class C1Camera {
public:
    bool open(int device_index, int width, int height, int fps);
    bool grabFrame(cv::Mat& bgra_out);  // BGR → BGRA 変換込み
    void close();
    int width() const;
    int height() const;
};
```

---

### 2. メインエントリポイント

#### [NEW] main_c1_tcp.cpp

[main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) を雛形として、以下の差分を適用する。

| 変更箇所 | ZED (元) | C1 (新) |
|----------|----------|---------|
| インクルード | `<sl/Camera.hpp>` | `"c1_camera.hpp"` |
| [handleOpenCamera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#244-246) のバリデーション | `camera != "ZED"` | `camera != "C1"` |
| `current_camera_config.camera` デフォルト値 | `"ZED"` | `"C1"` |
| デフォルト解像度 | 2560×720 (S.B.S) | 1280×720 |
| [streamingThreadFunction](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#249-250) の初期化 | `sl::Camera zed; zed.open(init_params)` | `C1Camera cam; cam.open(device, w, h, fps)` |
| フレーム取得ループ | `zed.grab() + retrieveImage + slMat2cvMat` | `cam.grabFrame(cv_image)` |
| GStreamer pipeline | `appsrc → videoconvert → nvvidconv ...` | 同パイプラインを継続使用（フォーマット: BGRA） |
| [updateZedConfiguration](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#598-619) | sl::RESOLUTION マッピング | 削除（不要） |
| C1 デバイス指定 | なし | CLI オプション `--device` (`/dev/video0` デフォルト) |

---

### 3. ビルド設定

#### [MODIFY] Makefile

```diff
 # C1
+C1_SRCS := main_c1_tcp.cpp
+C1_OBJS := $(C1_SRCS:.cpp=.o)
+C1APP := OrinVideoSender_C1
+
+c1: $(C1APP)
+
+$(C1APP): $(C1_OBJS)
+	@echo "Linking: $@"
+	$(CXX) -o $@ $(C1_OBJS) $(C1_LDFLAGS)
```

`C1_LDFLAGS` は `LDFLAGS` から `-lsl_zed` を取り除き、`-lopencv_videoio` を確保したもの。

---

## 検証計画

### コンパイル確認（Linux 環境のみ）

```bash
cd /path/to/XRoboToolkit-Orin-Video-Sender
make c1
```

エラーなくビルドが通ることを確認。

### 手動動作確認（Jetson Orin + C1 カメラ接続済み環境）

1. C1 カメラを Jetson に接続し、`/dev/video0` として認識されていることを確認:
   ```bash
   ls /dev/video*
   ```
2. 受信側（XR Headset or PC）を別途起動しておく。
3. `--send` モード（直接送信）で起動:
   ```bash
   ./OrinVideoSender_C1 --send --server <受信側IP> --port 12345 --device /dev/video0 --preview
   ```
4. プレビューウィンドウに映像が표示され、受信側でデコード映像が流れることを確認。
5. `--listen` モード（コントロール受信）で起動:
   ```bash
   ./OrinVideoSender_C1 --listen 0.0.0.0:9000
   ```
   その後 `OPEN_CAMERA` コマンドを送り、ストリーミングが開始されることを確認。
