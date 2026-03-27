# C1 カメラへの置換計画

## 変更が必要な既存ファイル
| ファイル | 変更ポイント |
|----------|----------------|
| [main_zed_tcp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp) | - `#include <sl/Camera.hpp>` を削除し、C1 カメラ用ヘッダ（例: `#include "c1_camera.hpp"`）に置換。\n- `sl::Camera` のインスタンス生成・設定部分 (`sl::Camera zed;`、`init_params` 等) を C1 カメラ API に置き換える。\n- [CameraRequestData](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#35-38) の `camera` フィールドで "ZED" から "C1" に変更し、[handleOpenCamera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#475-525) 内のカメラ種別チェックを更新。\n- [buildPipelineString](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#565-597) で解像度やフォーマットが C1 カメラに合わせて調整（例: `width/height` のデフォルトを C1 の仕様に）。\n- [on_new_sample](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio.cpp#156-212) で取得するフレーム変換 ([slMat2cvMat](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#391-428)) を C1 カメラのフレーム取得関数に置換。 |
| [main_zed_asio.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio.cpp) / [main_zed_asio_udp.cpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_asio_udp.cpp) | 同様に `#include <sl/Camera.hpp>` を除去し、C1 カメラ API に差し替える。カメラ初期化ロジックとフレーム取得部分を更新。 |
| [network_helper.hpp](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/network_helper.hpp) | [CameraRequestData](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#35-38) の `camera` フィールドはそのまま使用できるが、コメントに "ZED" → "C1" の説明を追加。 |
| [README.md](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/README.md) (必要に応じて) | 対応カメラ一覧を更新し、ZED の記述を C1 カメラに置換。 |

## 新規作成が推奨されるファイル
| ファイル | 内容 |
|----------|------|
| `c1_camera.hpp` | C1 カメラの初期化・設定・フレーム取得インタフェースを定義。例: `class C1Camera { public: bool open(const Config&); cv::Mat grabFrame(); void close(); };` |
| `c1_camera.cpp` | 上記ヘッダの実装。ZED 用コードを参考にしつつ、C1 カメラ SDK の API を呼び出す。 |
| `c1_camera_demo.cpp` (任意) | C1 カメラだけで動作する最小デモ。テストやデバッグに利用。 |

## 置換作業の流れ (概要)
1. **C1 カメラ SDK をプロジェクトに組み込む** – `CMakeLists.txt`（または Makefile）にインクルードパスとリンクライブラリを追加  
2. **`c1_camera.hpp/.cpp` を実装** – ZED の `sl::Camera` と同等の機能（解像度取得、フレーム取得）を提供。  
3. **既存 ZED コードを置換** – 先述のファイルで `sl::Camera` 関連コードをすべて `C1Camera` に置き換える。  
4.  **`CameraRequestData.camera` のバリデーションを更新** – [handleOpenCamera](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#475-525) で `if (cameraConfig.camera != "C1")` のようにチェック。  
5.   **ビルド・テスト** – `make clean && make` でコンパイルし、`--listen` または `--send` モードで動作確認。\n6. **ドキュメント更新** – [README.md](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/README.md) と [analysis_summary.md](file:///c:/Users/taise/.gemini/antigravity/brain/9e473996-35c5-4e06-9b1a-95c308acf7a6/analysis_summary.md) のカメラ対応表を修正。  
## 注意点
- C1 カメラの解像度やフレームレートの上限が ZED と異なる可能性があるため、[CameraRequestData](file:///c:/Users/taise/OneDrive/%E3%83%89%E3%82%AD%E3%83%A5%E3%83%A1%E3%83%B3%E3%83%88/%E3%83%AD%E3%83%9C%E3%83%83%E3%83%88%E3%83%90%E3%82%A4%E3%83%88/XRoboToolkit-Orin-Video-Sender/main_zed_tcp.cpp#35-38) のデフォルト値を適切に設定すること。\n- GStreamer の `caps` 文字列 (`width=...,height=...,format=BGRA`) は C1 カメラが出力できるフォーマットに合わせて変更する必要がある。\n- エンコード設定 (`nvv4l2h264enc` / `nvv4l2h265enc`) はハードウェアエンコーダが Jetson に搭載されている限り共通で使用できるが、`pipeline_str` の `videoconvert` 前後の変換が正しいかテストすること。  
この資料を元に、対象ファイルを修正・新規作成してください。
