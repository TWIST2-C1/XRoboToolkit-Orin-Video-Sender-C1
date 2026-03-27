# XRoboToolkit-Orin-Video-Sender リポジトリ概要

## 目的
Nvidia Jetson Orin 上で動作する **ビデオプレビュー / エンコード / 送信** ツールです。主に **ZED カメラ** と **Webcam** を対象に、映像を H.264 (または HEVC) でエンコードし、以下の通信手段で外部デバイスへ送信できます。

## 対応カメラデバイス
| カメラ種別 | 対応状況 |
|------------|----------|
| **ZED カメラ** (stereolabs) | 完全対応。`sl::Camera` API を使用し、解像度・FPS・ビットレート等を動的に設定可能。 |
| **Webcam** (USB カメラ) | `opencv2/opencv.hpp` 経由で取得可能。[main_zed_asio.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio.cpp) などのサンプルで実装されていますが、主に ZED 用に最適化されています。 |

## 主な通信方式
| ファイル | プロトコル | 説明 |
|----------|-----------|------|
| [main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) | **TCP** (カスタムバイナリプロトコル) | クライアント/サーバ方式。[NetworkDataProtocol](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#46-48) で **コマンド長 + データ長 + データ** の構造を持ち、`OPEN_CAMERA` / `CLOSE_CAMERA` などの制御コマンドを受信。データは 4 バイトヘッダでサイズを示した後、エンコード済み H.264 フレームを送信。 |
| [main_zed_asio.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio.cpp) | **TCP (ASIO ライブラリ)** | 非同期 I/O を利用した実装。[network_asio.hpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/network_asio.hpp) がラッパーとして機能し、`TCPClient` / `TCPServer` クラスで接続管理。 |
| [main_zed_asio_udp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio_udp.cpp) | **UDP** | 同様のコマンド構造を UDP パケットで送信。信頼性は低いが遅延が少ないシナリオ向け。 |
| [main_zed_tcp_zmq.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp_zmq.cpp) + [zmq_receiver.py](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/zmq_receiver.py) | **ZeroMQ (ZMQ)** | [main_zed_tcp_zmq.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp_zmq.cpp) が ZMQ PUB ソケットで映像データを配信し、[zmq_receiver.py](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/zmq_receiver.py) が SUB で受信。マルチキャスト的に複数クライアントへ配信可能。 |
| [network_helper.hpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/network_helper.hpp) | **ヘルパー関数** | バイト列の **リトルエンディアン整数** 変換、**コンパクト文字列** デシリアライズ、プロトコルデコードを提供。 |

## コマンド・プロトコル詳細
- **ヘッダ**: 4 バイト (ビッグエンディアン) がペイロード長を示す。
- **ペイロード**: [NetworkDataProtocol](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#46-48) 形式 → `int32 commandLength` + `command` (文字列) + `int32 dataLength` + `data` (バイナリ)。
- **主要コマンド**:
  - `OPEN_CAMERA` : カメラ設定 ([CameraRequestData](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#35-38)) をバイナリで送信。設定項目は `width, height, fps, bitrate, enableMvHevc, renderMode, port, camera, ip`。
  - `CLOSE_CAMERA` : ストリーミング停止。
  - その他カスタムコマンドは [onDataCallback](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#252-330) でデバッグ出力されます。

## ストリーミングフロー
1. **サーバ側** (`--listen` オプション) が `TCPServer` を起動し、制御コマンドを待機。
2. クライアントが `OPEN_CAMERA` コマンドでカメラ設定を送信。
3. [handleOpenCamera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#475-525) が設定をグローバル `current_camera_config` に保存し、[startStreamingThread](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#247-248) を起動。
4. **ストリーミングスレッド** が ZED カメラを初期化し、[buildPipelineString](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#565-597) で GStreamer パイプラインを生成。
   - `appsrc` → `videoconvert` → `nvvidconv` → `nvv4l2h264enc` (または `nvv4l2h265enc`) → `appsink`。
   - `preview_enabled` が true の場合、`autovideosink` でローカルプレビューを表示。
5. エンコードされたフレームは [on_new_sample](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio.cpp#156-212) で取得し、サイズヘッダ付きパケットに変換して `TCPClient::sendData` で送信。
6. 受信側は同様のヘッダを解析し、デコードまたは保存に利用。

## 主要ビルド・実行オプション (README 参照)
- `--preview` : Jetson 上で映像プレビューを有効化。
- `--send` : エンコードした映像を直接サーバ (`--server IP --port PORT`) に送信。
- `--listen ADDRESS` : 指定アドレス (`IP:PORT`) で制御コマンド受信サーバを起動。
- `--server` / `--port` : 送信先サーバ情報。
- `--help` : 使用方法を表示。

## 参考リンク (README に記載)
- **VideoPlayer**: `TCP` 受信側のサンプルプレイヤー (GitHub XR-Robotics/RobotVision-PC)。
- **Video-Viewer**: 汎用 TCP/UDP ビデオビューア。
- **Unity-Client**: Unity での TCP 受信実装例。
- **RobotVisionTest**: ソフトウェアエンコード (ffmpeg) のサンプル。

## まとめ
- **通信**: TCP / UDP / ASIO / ZeroMQ の 4 つの手段を提供し、カスタムバイナリプロトコルでカメラ制御と映像転送を実現。
- **カメラ**: 主に ZED カメラに最適化されているが、Webcam でも動作可能。解像度・FPS・ビットレートは動的に変更でき、HEVC への切替もサポート。
- **エンコード**: GStreamer + Nvidia ハードウェアエンコーダ (`nvv4l2h264enc` / `nvv4l2h265enc`) を使用し、`insert-sps-pps` と `idrinterval` によるストリーミング開始の安定化を実装。
- **拡張性**: プロトコルはシンプルな長さ付き文字列 + バイナリ構造なので、追加コマンドや新しいデバイスを容易に組み込める。

この資料は、リポジトリが提供する **通信方式** と **カメラデバイス** に焦点を当てた概要です。必要に応じて、各ファイルの詳細実装やテスト手順を別途ドキュメント化できます。
