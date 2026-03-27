# C1 カメラ対応 変更点資料

## 新規作成ファイル

### [c1_camera.hpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp)

ZED SDK の `sl::Camera` を置き換えるカメラ抽象クラス。

| 項目 | 内容 |
|------|------|
| 実装 | OpenCV `cv::VideoCapture` + `CAP_V4L2` |
| フレーム出力 | BGR → BGRA 変換済み `cv::Mat` |
| 主なメソッド | [open(device_path, w, h, fps)](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#145-171) / [grabFrame(bgra_out)](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#172-204) / [close()](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#205-210) |

---

### [main_c1_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_c1_tcp.cpp)

[main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) ベースで ZED SDK 依存を完全除去した C1 カメラ専用エントリポイント。

| 変更箇所 | ZED（元） | C1（新） |
|----------|-----------|---------|
| インクルード | `sl/Camera.hpp` | [c1_camera.hpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp) |
| カメラ初期化 | `sl::Camera zed; zed.open(init_params)` | `C1Camera cam; cam.open(device_path, w, h, fps)` |
| フレーム取得 | `zed.grab() + retrieveImage + slMat2cvMat` | `cam.grabFrame(bgra_frame)` |
| 種別チェック | `camera != "ZED"` | `camera != "C1"` |
| デフォルト解像度 | 2560x720（S.B.S） | 1280x720 |
| デフォルト FPS | 60 | 30 |
| [updateZedConfiguration()](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#598-619) | あり（sl::RESOLUTION マッピング） | 削除 |
| [slMat2cvMat()](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#391-428) | あり | 削除 |

**追加 CLI オプション（ZED 版にはなかったもの）：**

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `--device PATH` | V4L2 デバイスパス | `/dev/video0` |
| `--width W` | キャプチャ幅 | 1280 |
| `--height H` | キャプチャ高さ | 720 |
| `--fps FPS` | フレームレート | 30 |
| `--bitrate BPS` | エンコードビットレート | 4000000 |
| `--hevc` | H.265 エンコーダを使用 | H.264 |

---

## 変更ファイル

### [Makefile](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/Makefile)

```diff
+C1APP := OrinVideoSender_C1
+C1_SRCS := main_c1_tcp.cpp
+C1_OBJS := $(C1_SRCS:.cpp=.o)
+C1_LDFLAGS := <opencv + gstreamer + ssl + pthread>

+c1: $(C1APP)
+$(C1APP): $(C1_OBJS)
+    $(CXX) -o $@ $(C1_OBJS) $(C1_LDFLAGS)

 clean:
-    rm -rf $(APP) $(OBJS)
+    rm -rf $(APP) $(OBJS) $(C1APP) $(C1_OBJS)

-.PHONY: all debug clean install
+.PHONY: all c1 debug clean install
```

> [!NOTE]
> [main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) は変更していないため、[make](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_c1_tcp.cpp#236-240)（ZED ビルド）は引き続き動作する。

---

## ビルド・実行方法

### ビルド（Linux / Jetson Orin）

```bash
cd XRoboToolkit-Orin-Video-Sender
make c1
```

### 実行例

**直接送信モード（C1 カメラ -> 受信側 PC）:**

```bash
./OrinVideoSender_C1 \
  --send \
  --server 192.168.1.10 \
  --port 12345 \
  --device /dev/video0 \
  --width 1280 --height 720 --fps 30
```

**コントロール受信待ちモード（XR ヘッドセットからの OPEN_CAMERA コマンド待ち）:**

```bash
./OrinVideoSender_C1 --listen 0.0.0.0:9000
```

> [!IMPORTANT]
> `--listen` モードで `OPEN_CAMERA` コマンドを送信する際、`camera` フィールドは **`"C1"`** にしてください（`"ZED"` は拒否されます）。

---

## アーキテクチャ（変更後）

```
C1 Camera (/dev/video0)
    | cv::VideoCapture (V4L2)
    v
C1Camera::grabFrame()  ->  cv::Mat (BGRA)
    |
    v
GstAppSrc (appsrc)
    | videoconvert -> nvvidconv -> NV12(NVMM)
    v
nvv4l2h264enc / nvv4l2h265enc  (Jetson HW encoder)
    |
    v
GstAppSink -> on_new_sample()
    |
    v
TCP (4-byte length header + H.264/H.265 NAL)
    |
    v
受信側 (XR Headset / PC)
```

---

## デュアルカメラ（ステレオ）対応 追記

### 変更ファイル

#### [c1_camera.hpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp) — [C1StereoCamera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#140-225) クラスを追加

左右カメラを各 [C1Camera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#25-125) で取得し、`cv::hconcat` で SBS (Side-by-Side) 合成して BGRA フレームを返すクラス。

| メソッド | 説明 |
|----------|------|
| [open(left, right, w, h, fps)](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#145-171) | 左右 V4L2 デバイスを開く |
| [grabFrame(sbs_out)](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#172-204) | 左右フレームを横並び合成して返す |
| [sbsWidth()](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#213-215) | SBS 出力幅（単眼幅 × 2） |

#### [main_c1_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_c1_tcp.cpp) — ステレオ対応を追加

| 変更点 | 内容 |
|--------|------|
| グローバル変数 | `c1_device_left`, `c1_device_right`, `stereo_mode` を追加 |
| CLI オプション | `--stereo`, `--device-left PATH`, `--device-right PATH` を追加 |
| [streamingThreadFunction](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_c1_tcp.cpp#269-270) | `stereo_mode` で [C1StereoCamera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#140-225) / [C1Camera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/c1_camera.hpp#25-125) を分岐 |
| SBS 解像度 | 各カメラ 1280x720 → 送信フレームは **2560x720** |

**追加 CLI オプション:**

| オプション | 説明 | デフォルト |
|-----------|------|-----------|
| `--stereo` | ステレオ（2 台）モードを有効化 | 無効（単眼） |
| `--device-left PATH` | 左カメラ V4L2 パス | `/dev/video0` |
| `--device-right PATH` | 右カメラ V4L2 パス | `/dev/video1` |

### ステレオアーキテクチャ

```
Left  C1 Camera (/dev/video0)          Right C1 Camera (/dev/video1)
    | cv::VideoCapture                      | cv::VideoCapture
    v                                       v
C1Camera::grabFrame()               C1Camera::grabFrame()
    |                                       |
    +------------- cv::hconcat ------------+
                        |
                        v
             SBS フレーム: 2560x720 BGRA
                        |
                        v
             GstAppSrc -> nvv4l2h264enc
                        |
                        v
             TCP -> 受信側 (XR Headset)
```

### 実行例（ステレオモード）

```bash
./OrinVideoSender_C1 \
  --stereo \
  --device-left /dev/video0 \
  --device-right /dev/video1 \
  --send --server 192.168.1.10 --port 12345 \
  --width 1280 --height 720 --fps 30
```

> [!NOTE]
> 左右カメラのデバイス番号は `ls /dev/video*` で確認し、必要に応じて `--device-left` / `--device-right` に指定してください。

