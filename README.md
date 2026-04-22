# XRoboToolkit-Orin-Video-Sender
Video Previewer/Encoder/Sender on Nvidia Jetson Orin Platform

![Screenshot](Docs/screenshot.png)
> Sender (Webcam): `./OrinVideoSender --preview --send --server 192.168.1.176 --port 12345`

> Receiver (Video-Viewer): TCP - 192.168.1.176 - 12345 - 1280x720

## Features

- Support Webcam and ZED cameras
- Preview
- H264 Encoding (via GStreamer)
- TCP/UDP sending w/ and w/o ASIO


## How to

- Setup necessary environment on Orin

- Build
```
# Update `Makefile` to choose the protocol [TCP/UDP], camera type [Webcam/ZED], w/ or w/o ASIO.
# Default: TCP w/o asio.

# install zmq
sudo apt-get install libzmq3-dev

make

./OrinVideoSender --help

# Listen to coming command from VR, 192.168.1.153 is the Orin IP address
# Add `--preview` to show the video on Orin if necessary 
./OrinVideoSender --listen 192.168.1.153:13579

# send the video stream to both VR via TCP and my own ubuntu via ZMQ
./OrinVideoSender --listen 192.168.1.153:13579 --zmq tcp://*:5555

# Direct send the video stream # 192.168.1.176 is the VR headset IP
# Add `--preview` to show the video on Orin if necessary 
./OrinVideoSender --send --server 192.168.1.176 --port 12345
```

## One More Thing 

- For software encoding ffmpeg, please refer to [RobotVisionTest](https://github.com/XR-Robotics/RobotVision-PC/tree/main/VideoTransferPC/RobotVisionTest).

> Note: Hardware ffmpeg encoding is not availalbe yet.

> Note: Jetson Multimedia API is not in use yet.

- For encoded h264 stream receiver, please refer to [VideoPlayer](https://github.com/XR-Robotics/RobotVision-PC/tree/main/VideoTransferPC/VideoPlayer) [TCP Only].

- For a general video player, please refer to [Video-Viewer](https://github.com/XR-Robotics/XRoboToolkit-Native-Video-Viewer) [TCP/UDP].

- The encoded h264 stream can be also played in [Unity-Client](https://github.com/XR-Robotics/XRoboToolkit-Unity-Client) [TCP Only].


# 自分用のまとめ
## GitHub上でプログラムを変更したとき、ローカルに変更を反映させるコマンド
```
git pull origin main
```
## ローカル上でプログラムを変更したとき、GitHubに変更を反映させるコマンド
```
git add .
```
* 意味: 「今回変更・追加したファイルを全部まとめて段ボール箱に入れて！」というコマンドです。
```
git commit -m "カメラの解像度を変更"
```
* 意味: "" の中に、あとで自分が振り返ったときに分かりやすい変更内容を書きます（日本語でOKです）。
```
git push origin main
```
* 意味: 「手元でセーブしたデータを、GitHubにアップロードして！」というコマンドです。

## 利用方法
- つながっているカメラの確認コマンド
```
ls -l /dev/video*

```
```
v4l2-ctl --list-devices
```
### xr_teleoperationを基にしたプログラムで映像を入手してVRで確認するとき
* 準備
video_test.pyのプログラムを開いて、IPアドレスを使用しているPCのアドレスに変更する。（デフォルトでは192.168.??.??のようになっているはず）  
* ターミナル１（映像の配信用）
```
conda activate tv
cd ~/TWIST2
python -m teleimager.image_server
```
* ターミナル２(VRで映像を確認できるようにウェブに映像を配信)
```
conda activate tv
python ~/TWIST2/video_test.py 
```
### XRobotToolkit-Orin-Video-Senderを基にしたプログラムで映像を入手してVRで確認するとき
* 現在準備中
#### PICO4U内のプログラムを変更してC1の項目を作成。
 ```
# pull the file first
adb pull /sdcard/Android/data/com.xrobotoolkit.client/files/video_source.yml
```
```
# edit the video_source.yml
# push the file back
adb push video_source.yml /sdcard/Android/data/com.xrobotoolkit.client/files/video_source.yml
```
#### 3. プログラムの起動方法

##### コマンド待受モード (`--listen`)
受信側（クライアント）からの接続と `OPEN_CAMERA` コマンドを待ってから配信を開始するモードです。（XR アプリ等と連携する場合に標準的です）←**こっちを使っています！**
listenはVR側からの要求（OPEN_CAMERA）待ちという意味。要求を受け取ったら映像の配信を開始することになる。つまりサーバーが映像を配信し始める。
```
./OrinVideoSender --listen 0.0.0.0:13579 --cam1 2 --cam2 0
```
*(※ このモードのときは、後述するクライアント側からのコマンド送信時にステレオの設定が適用されます。)*
```
make clean && make
```

---

#### 4. クライアントからの接続仕様（重要）

`--listen` モードで運用する場合、クライアント（XR ヘッドセット等）から送信する `OPEN_CAMERA` パケットに以下の設定を含める必要があります。

1. **カメラ種別の指定**
   - 従来 `"ZEDMINI"` と指定していた部分を、**`"C1"`** に変更して送信してください。

---

#### 5. 追加のオプション一覧 (C1 専用)

実行時に細かな挙動を制御するためのオプションです。

| オプション | 説明 |
|---|---|
| `--cam1 0` | 左カメラのデバイスパスを指定します。（デフォルト: `/dev/video0`） |
| `--cam2 0` | 右カメラのデバイスパスを指定します。（デフォルト: `/dev/video1`） |
| `--preview` | 映像をデバイスにも出力します |
---

## 参考サイト（元のプログラム）
#### XR-RoboToolkit
- <https://github.com/XR-Robotics/XRoboToolkit-Orin-Video-Sender>
#### TWIST2
- <https://github.com/amazon-far/TWIST2>
#### XR_Teleoperate
- <https://github.com/unitreerobotics/xr_teleoperate>
#### XRoboToolkit_Unity_Client
- <https://github.com/XR-Robotics/XRoboToolkit-Unity-Client>


