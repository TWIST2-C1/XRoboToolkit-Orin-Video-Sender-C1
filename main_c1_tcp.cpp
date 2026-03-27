/**
 * main_c1_tcp.cpp
 *
 * C1 カメラ（V4L2/UVC）を使用した動画ストリーミング送信プログラム。
 * 単眼モード（--device）とステレオモード（--stereo）に対応。
 * ステレオ時は左右カメラを SBS（横並び）で合成して送信する。
 *
 * ビルド: make c1
 *
 * 使い方（ステレオ送信）:
 *   ./OrinVideoSender_C1 --stereo \
 *     --device-left /dev/video0 --device-right /dev/video1 \
 *     --send --server 192.168.1.10 --port 12345
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <glib-unix.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <openssl/md5.h>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "c1_camera.hpp"
#include "network_helper.hpp"

// ---------------------------------------------------------------------------
// Network Protocol Structures
// ---------------------------------------------------------------------------

struct CameraRequestData {
  int width;
  int height;
  int fps;
  int bitrate;
  int enableMvHevc;
  int renderMode;
  int port;
  std::string camera;
  std::string ip;

  CameraRequestData()
      : width(0), height(0), fps(0), bitrate(0), enableMvHevc(0),
        renderMode(0), port(0) {}
};

struct NetworkDataProtocol {
  std::string command;
  int length;
  std::vector<uint8_t> data;

  NetworkDataProtocol() : length(0) {}
  NetworkDataProtocol(const std::string &cmd, const std::vector<uint8_t> &d)
      : command(cmd), data(d), length(d.size()) {}
};

// ---------------------------------------------------------------------------
// Deserialization
// ---------------------------------------------------------------------------

class CameraRequestDeserializer {
public:
  static CameraRequestData deserialize(const std::vector<uint8_t> &data) {
    if (data.size() < 10) {
      throw std::invalid_argument("Data is too small for valid camera request");
    }

    size_t offset = 0;

    if (data[offset] != 0xCA || data[offset + 1] != 0xFE) {
      throw std::invalid_argument("Invalid magic bytes");
    }
    offset += 2;

    uint8_t version = data[offset++];
    if (version != 1) {
      throw std::invalid_argument("Unsupported protocol version");
    }

    CameraRequestData result;

    if (offset + 28 > data.size()) {
      throw std::invalid_argument("Data too small for integer fields");
    }

    result.width = readInt32(data, offset);
    result.height = readInt32(data, offset + 4);
    result.fps = readInt32(data, offset + 8);
    result.bitrate = readInt32(data, offset + 12);
    result.enableMvHevc = readInt32(data, offset + 16);
    result.renderMode = readInt32(data, offset + 20);
    result.port = readInt32(data, offset + 24);
    offset += 28;

    result.camera = readCompactString(data, offset);
    result.ip = readCompactString(data, offset);

    return result;
  }

private:
  static int32_t readInt32(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 4 > data.size()) {
      throw std::out_of_range("Not enough data to read int32");
    }
    return static_cast<int32_t>((data[offset]) | (data[offset + 1] << 8) |
                                (data[offset + 2] << 16) |
                                (data[offset + 3] << 24));
  }

  static std::string readCompactString(const std::vector<uint8_t> &data,
                                       size_t &offset) {
    if (offset >= data.size()) {
      throw std::out_of_range("Not enough data to read string length");
    }

    uint8_t length = data[offset++];
    if (length == 0) {
      return std::string();
    }

    if (offset + length > data.size()) {
      throw std::out_of_range("Not enough data to read string content");
    }

    std::string result(reinterpret_cast<const char *>(&data[offset]), length);
    offset += length;
    return result;
  }
};

class NetworkDataProtocolDeserializer {
public:
  static NetworkDataProtocol deserialize(const std::vector<uint8_t> &buffer) {
    if (buffer.size() < 8) {
      throw std::invalid_argument("Buffer too small for valid protocol data");
    }

    size_t offset = 0;

    int32_t commandLength = readInt32(buffer, offset);
    offset += 4;

    if (commandLength < 0 || offset + commandLength > buffer.size()) {
      throw std::invalid_argument("Invalid command length");
    }

    std::string command;
    if (commandLength > 0) {
      command = std::string(reinterpret_cast<const char *>(&buffer[offset]),
                            commandLength);
      size_t nullPos = command.find('\0');
      if (nullPos != std::string::npos) {
        command = command.substr(0, nullPos);
      }
    }
    offset += commandLength;

    if (offset + 4 > buffer.size()) {
      throw std::invalid_argument("Buffer too small for data length");
    }

    int32_t dataLength = readInt32(buffer, offset);
    offset += 4;

    if (dataLength < 0 || offset + dataLength > buffer.size()) {
      throw std::invalid_argument("Invalid data length");
    }

    std::vector<uint8_t> data;
    if (dataLength > 0) {
      data.assign(buffer.begin() + offset,
                  buffer.begin() + offset + dataLength);
    }

    return NetworkDataProtocol(command, data);
  }

private:
  static int32_t readInt32(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 4 > data.size()) {
      throw std::out_of_range("Not enough data to read int32");
    }
    return static_cast<int32_t>((data[offset]) | (data[offset + 1] << 8) |
                                (data[offset + 2] << 16) |
                                (data[offset + 3] << 24));
  }
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

CameraRequestData current_camera_config;

std::atomic<bool> stop_requested{false};
std::atomic<bool> streaming_active{false};
std::atomic<bool> encoding_enabled{false};
std::atomic<bool> send_enabled{false};
std::atomic<bool> preview_enabled{false};

std::unique_ptr<std::thread> listen_thread;
std::unique_ptr<std::thread> streaming_thread;
std::mutex config_mutex;
std::condition_variable streaming_cv;
std::mutex streaming_mutex;

std::unique_ptr<TCPClient> sender_ptr;
std::unique_ptr<TCPServer> server_ptr;
std::string send_to_server = "";
int send_to_port = 0;

// デバイスパス（単眼モード）
std::string c1_device_path = "/dev/video0";
// デバイスパス（ステレオモード）
std::string c1_device_left  = "/dev/video0";
std::string c1_device_right = "/dev/video1";
// true = ステレオ（2台）モード
bool stereo_mode = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

template <typename T, typename... Args>
std::unique_ptr<T> make_unique_helper(Args &&...args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

bool initialize_sender() {
  int retry = 10;
  while (retry > 0 && !sender_ptr && !stop_requested.load()) {
    try {
      sender_ptr = std::unique_ptr<TCPClient>(
          new TCPClient(send_to_server, send_to_port));
      std::cout << "Attempting to connect to " << send_to_server << ":"
                << send_to_port << std::endl;
      sender_ptr->connect();
      return true;
    } catch (const TCPException &e) {
      std::cerr << "Failed to connect to server: " << e.what() << std::endl;
      sender_ptr = nullptr;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
    retry--;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------

void handleOpenCamera(const std::vector<uint8_t> &data);
void handleCloseCamera(const std::vector<uint8_t> &data);
void startStreamingThread();
void stopStreamingThread();
void streamingThreadFunction();
void listenThreadFunction(const std::string &listen_address);

// ---------------------------------------------------------------------------
// GStreamer callback – encoded フレームを TCP で送信
// ---------------------------------------------------------------------------

GstFlowReturn on_new_sample(GstAppSink *sink, gpointer /*user_data*/) {
  GstSample *sample = gst_app_sink_pull_sample(sink);
  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer(sample);
  GstMapInfo map;
  if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    const uint8_t *data = map.data;
    gsize size = map.size;

    if (send_enabled.load() && sender_ptr && sender_ptr->isConnected() &&
        data && size > 0) {
      try {
        std::vector<uint8_t> packet(4 + size);
        packet[0] = (size >> 24) & 0xFF;
        packet[1] = (size >> 16) & 0xFF;
        packet[2] = (size >> 8) & 0xFF;
        packet[3] = (size) & 0xFF;
        std::copy(data, data + size, packet.begin() + 4);

        sender_ptr->sendData(packet);
      } catch (const TCPException &e) {
        std::cerr << "TCP error in on_new_sample: " << e.what() << std::endl;
        streaming_active.store(false);
      } catch (const std::exception &e) {
        std::cerr << "Unexpected error in on_new_sample: " << e.what()
                  << std::endl;
        streaming_active.store(false);
      }
    }

    gst_buffer_unmap(buffer, &map);
  }

  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

// ---------------------------------------------------------------------------
// Protocol handlers
// ---------------------------------------------------------------------------

void onDataCallback(const std::string &command) {
  std::vector<uint8_t> binaryData(command.begin(), command.end());

  if (binaryData.size() < 4) {
    std::cerr << "Data too small to contain length header" << std::endl;
    return;
  }

  uint32_t bodyLength = (static_cast<uint32_t>(binaryData[0]) << 24) |
                        (static_cast<uint32_t>(binaryData[1]) << 16) |
                        (static_cast<uint32_t>(binaryData[2]) << 8) |
                        static_cast<uint32_t>(binaryData[3]);

  if (4 + bodyLength > binaryData.size()) {
    std::cerr << "Data too small for declared body length." << std::endl;
    return;
  }

  std::vector<uint8_t> protocolData(binaryData.begin() + 4,
                                    binaryData.begin() + 4 + bodyLength);

  try {
    NetworkDataProtocol protocol =
        NetworkDataProtocolDeserializer::deserialize(protocolData);

    std::cout << "Received protocol command: '" << protocol.command
              << "'" << std::endl;

    if (protocol.command == "OPEN_CAMERA") {
      handleOpenCamera(protocol.data);
    } else if (protocol.command == "CLOSE_CAMERA") {
      handleCloseCamera(protocol.data);
    } else {
      std::cout << "Unknown protocol command: " << protocol.command
                << std::endl;
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to parse protocol: " << e.what() << std::endl;
  }
}

void onDisconnectCallback() {
  std::cout << "Client disconnected, stopping streaming" << std::endl;
  stopStreamingThread();
}

void handleOpenCamera(const std::vector<uint8_t> &data) {
  std::cout << "Handling OPEN_CAMERA command" << std::endl;

  try {
    CameraRequestData cameraConfig =
        CameraRequestDeserializer::deserialize(data);

    std::cout << "Camera config - Width: " << cameraConfig.width
              << ", Height: " << cameraConfig.height
              << ", FPS: " << cameraConfig.fps
              << ", Bitrate: " << cameraConfig.bitrate
              << ", IP: " << cameraConfig.ip
              << ", Port: " << cameraConfig.port
              << ", type: " << cameraConfig.camera << std::endl;

    // C1 カメラ種別チェック
    if (cameraConfig.camera != "C1") {
      std::cout << "Unsupported camera type: " << cameraConfig.camera
                << ". Only 'C1' is supported." << std::endl;
      return;
    }

    {
      std::lock_guard<std::mutex> lock(config_mutex);
      current_camera_config = cameraConfig;
    }

    send_to_server = cameraConfig.ip;
    send_to_port = cameraConfig.port;

    std::cout << "Updated sender target to " << send_to_server << ":"
              << send_to_port << std::endl;

    startStreamingThread();

  } catch (const std::exception &e) {
    std::cerr << "Failed to parse camera config: " << e.what() << std::endl;
    if (!send_to_server.empty() && send_to_port > 0) {
      startStreamingThread();
    } else {
      std::cerr << "No valid server configuration available, cannot start streaming"
                << std::endl;
    }
  }
}

void handleCloseCamera(const std::vector<uint8_t> & /*data*/) {
  std::cout << "Handling CLOSE_CAMERA command" << std::endl;
  stopStreamingThread();
}

// ---------------------------------------------------------------------------
// Streaming thread control
// ---------------------------------------------------------------------------

void startStreamingThread() {
  std::lock_guard<std::mutex> lock(streaming_mutex);
  if (streaming_thread && streaming_thread->joinable()) {
    std::cout << "Streaming thread already running" << std::endl;
    return;
  }

  streaming_active.store(true);
  streaming_thread = make_unique_helper<std::thread>(streamingThreadFunction);
  std::cout << "Started streaming thread" << std::endl;
}

void stopStreamingThread() {
  std::lock_guard<std::mutex> lock(streaming_mutex);

  streaming_active.store(false);
  encoding_enabled.store(false);
  send_enabled.store(false);

  if (sender_ptr && sender_ptr->isConnected()) {
    sender_ptr->disconnect();
  }
  sender_ptr = nullptr;

  if (streaming_thread && streaming_thread->joinable()) {
    streaming_cv.notify_all();
    streaming_thread->join();
    streaming_thread = nullptr;
    std::cout << "Stopped streaming thread" << std::endl;
  }
}

// ---------------------------------------------------------------------------
// Listen thread
// ---------------------------------------------------------------------------

void listenThreadFunction(const std::string &listen_address) {
  std::cout << "Listen thread started on " << listen_address << std::endl;

  while (!stop_requested.load()) {
    try {
      server_ptr = make_unique_helper<TCPServer>(listen_address);
      server_ptr->setDataCallback(onDataCallback);
      server_ptr->setDisconnectCallback(onDisconnectCallback);
      server_ptr->start();
      std::cout << "TCPServer is listening on " << listen_address << std::endl;

      while (!stop_requested.load() && server_ptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
      }

      if (server_ptr) {
        server_ptr->stop();
        server_ptr = nullptr;
      }

      if (!stop_requested.load()) {
        std::cout << "Waiting for new connection..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }

    } catch (const std::exception &e) {
      std::cerr << "Listen thread error: " << e.what() << std::endl;
      if (!stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(2));
      }
    }
  }

  std::cout << "Listen thread stopped" << std::endl;
}

// ---------------------------------------------------------------------------
// SIGINT handler
// ---------------------------------------------------------------------------

void handle_sigint(int) {
  std::cout << "\nSIGINT received. Stopping all threads..." << std::endl;
  stop_requested.store(true);

  stopStreamingThread();

  if (server_ptr) {
    server_ptr->stop();
    server_ptr = nullptr;
  }

  streaming_cv.notify_all();
}

// ---------------------------------------------------------------------------
// GStreamer pipeline builder
// ---------------------------------------------------------------------------

/**
 * @brief C1 カメラ用 GStreamer パイプライン文字列を構築する。
 *
 * 入力: appsrc (BGRA) → videoconvert → nvvidconv → Jetson HW エンコーダ
 *        → appsink（TCP 送信）＋任意でプレビュー
 */
std::string buildPipelineString(const CameraRequestData &config,
                                bool preview) {
  // 解像度・フレームレートを確定（0 ならデフォルト値）
  int w = (config.width > 0) ? config.width : 1280;
  int h = (config.height > 0) ? config.height : 720;
  int fps = (config.fps > 0) ? config.fps : 30;
  int bitrate =
      (config.bitrate > 0) ? config.bitrate : 4000000; // 4 Mbps default

  std::string pipeline_str =
      "appsrc name=mysource is-live=true format=time "
      "caps=video/x-raw,format=BGRA,width=" +
      std::to_string(w) + ",height=" + std::to_string(h) +
      ",framerate=" + std::to_string(fps) + "/1 ! ";

  pipeline_str += "videoconvert ! nvvidconv ! "
                  "video/x-raw(memory:NVMM),format=NV12 ! tee name=t ";

  std::string encoder = config.enableMvHevc ? "nvv4l2h265enc" : "nvv4l2h264enc";
  std::string parser = config.enableMvHevc ? "h265parse" : "h264parse";

  pipeline_str +=
      "t. ! queue ! " + encoder + " maxperf-enable=1 insert-sps-pps=true ";
  pipeline_str +=
      "idrinterval=15 bitrate=" + std::to_string(bitrate) + " ! ";
  pipeline_str +=
      parser + " ! appsink name=mysink emit-signals=true sync=false ";

  if (preview) {
    pipeline_str +=
        "t. ! queue ! nvvidconv ! videoconvert ! autovideosink sync=false ";
  }

  return pipeline_str;
}

// ---------------------------------------------------------------------------
// Streaming thread – C1 カメラでフレームを取得し GStreamer に push
// ---------------------------------------------------------------------------

void streamingThreadFunction() {
  std::cout << "Streaming thread started" << std::endl;

  try {
    if (!initialize_sender()) {
      std::cerr << "Failed to initialize sender, streaming thread stopping"
                << std::endl;
      return;
    }

    encoding_enabled.store(true);
    send_enabled.store(true);

    // カメラ設定を安全に取得
    CameraRequestData config;
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      config = current_camera_config;
    }

    int w   = (config.width  > 0) ? config.width  : 1280;
    int h   = (config.height > 0) ? config.height : 720;
    int fps = (config.fps    > 0) ? config.fps    : 30;

    // =========================================================
    // カメラオープン（単眼 or ステレオを自動選択）
    // =========================================================
    std::string pipeline_str;

    if (stereo_mode) {
      // ---  ステレオモード: 左右 2 台から SBS フレームを生成  ---
      C1StereoCamera stereo;
      if (!stereo.open(c1_device_left, c1_device_right, w, h, fps)) {
        std::cerr << "Failed to open stereo cameras" << std::endl;
        return;
      }

      // 実際取得できた解像度で SBS パイプラインを設定
      config.width  = stereo.sbsWidth();  // 例: 2560
      config.height = stereo.height();    // 例: 720
      config.fps    = stereo.fps();

      pipeline_str = buildPipelineString(config, preview_enabled.load());
      std::cout << "[Stereo] Pipeline: " << pipeline_str << std::endl;

      GError *error = nullptr;
      GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
      if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << error->message << std::endl;
        g_clear_error(&error);
        stereo.close();
        return;
      }

      GstElement *appsrc  = gst_bin_get_by_name(GST_BIN(pipeline), "mysource");
      GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
      g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample), nullptr);
      gst_element_set_state(pipeline, GST_STATE_PLAYING);

      int frame_id   = 0;
      int target_fps = (config.fps > 0) ? config.fps : 30;
      std::cout << "Starting stereo SBS streaming loop..." << std::endl;

      while (streaming_active.load() && !stop_requested.load()) {
        cv::Mat sbs_frame;
        if (!stereo.grabFrame(sbs_frame)) {
          std::cerr << "Stereo frame grab failed, retrying..." << std::endl;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

        if (encoding_enabled.load()) {
          GstBuffer *buffer = gst_buffer_new_allocate(
              nullptr, sbs_frame.total() * sbs_frame.elemSize(), nullptr);
          GstMapInfo map;
          gst_buffer_map(buffer, &map, GST_MAP_WRITE);
          memcpy(map.data, sbs_frame.data,
                 sbs_frame.total() * sbs_frame.elemSize());
          gst_buffer_unmap(buffer, &map);

          GST_BUFFER_PTS(buffer) =
              gst_util_uint64_scale(frame_id, GST_SECOND, target_fps);
          GST_BUFFER_DURATION(buffer) =
              gst_util_uint64_scale(1, GST_SECOND, target_fps);
          gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
          frame_id++;
        }
      }

      std::cout << "Stereo streaming loop ended, cleaning up..." << std::endl;
      gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(appsrc);
      gst_object_unref(appsink);
      gst_object_unref(pipeline);
      stereo.close();

    } else {
      // ---  単眼モード: 1 台から BGRA フレームを生成  ---
      C1Camera cam;
      if (!cam.open(c1_device_path, w, h, fps)) {
        std::cerr << "Failed to open C1 camera: " << c1_device_path << std::endl;
        return;
      }

      config.width  = cam.width();
      config.height = cam.height();
      config.fps    = cam.fps();

      pipeline_str = buildPipelineString(config, preview_enabled.load());
      std::cout << "[Mono] Pipeline: " << pipeline_str << std::endl;

      GError *error = nullptr;
      GstElement *pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
      if (!pipeline) {
        std::cerr << "Failed to create pipeline: " << error->message << std::endl;
        g_clear_error(&error);
        cam.close();
        return;
      }

      GstElement *appsrc  = gst_bin_get_by_name(GST_BIN(pipeline), "mysource");
      GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
      g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample), nullptr);
      gst_element_set_state(pipeline, GST_STATE_PLAYING);

      int frame_id   = 0;
      int target_fps = (config.fps > 0) ? config.fps : 30;
      std::cout << "Starting mono streaming loop..." << std::endl;

      while (streaming_active.load() && !stop_requested.load()) {
        cv::Mat bgra_frame;
        if (!cam.grabFrame(bgra_frame)) {
          std::cerr << "Frame grab failed, retrying..." << std::endl;
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          continue;
        }

        if (encoding_enabled.load()) {
          GstBuffer *buffer = gst_buffer_new_allocate(
              nullptr, bgra_frame.total() * bgra_frame.elemSize(), nullptr);
          GstMapInfo map;
          gst_buffer_map(buffer, &map, GST_MAP_WRITE);
          memcpy(map.data, bgra_frame.data,
                 bgra_frame.total() * bgra_frame.elemSize());
          gst_buffer_unmap(buffer, &map);

          GST_BUFFER_PTS(buffer) =
              gst_util_uint64_scale(frame_id, GST_SECOND, target_fps);
          GST_BUFFER_DURATION(buffer) =
              gst_util_uint64_scale(1, GST_SECOND, target_fps);
          gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
          frame_id++;
        }
      }

      std::cout << "Mono streaming loop ended, cleaning up..." << std::endl;
      gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(appsrc);
      gst_object_unref(appsink);
      gst_object_unref(pipeline);
      cam.close();
    }

  } catch (const std::exception &e) {
    std::cerr << "Streaming thread error: " << e.what() << std::endl;
  }

  std::cout << "Streaming thread finished" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);
  signal(SIGINT, handle_sigint);

  bool preview_enabled_local = false;
  bool listen_enabled = false;
  bool send_enabled_mode = false;
  std::string listen_address = "";

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--preview") {
      preview_enabled_local = true;
    } else if (arg == "--listen" && i + 1 < argc) {
      listen_enabled = true;
      listen_address = argv[++i];
    } else if (arg == "--send") {
      send_enabled_mode = true;
    } else if (arg == "--server" && i + 1 < argc) {
      send_to_server = argv[++i];
    } else if (arg == "--port" && i + 1 < argc) {
      send_to_port = std::stoi(argv[++i]);
    } else if (arg == "--device" && i + 1 < argc) {
      // 単眼デバイスパス
      c1_device_path = argv[++i];
    } else if (arg == "--device-left" && i + 1 < argc) {
      // ステレオ左カメラ
      c1_device_left = argv[++i];
    } else if (arg == "--device-right" && i + 1 < argc) {
      // ステレオ右カメラ
      c1_device_right = argv[++i];
    } else if (arg == "--stereo") {
      // ステレオ（2台）モードを有効化
      stereo_mode = true;
    } else if (arg == "--width" && i + 1 < argc) {
      current_camera_config.width = std::stoi(argv[++i]);
    } else if (arg == "--height" && i + 1 < argc) {
      current_camera_config.height = std::stoi(argv[++i]);
    } else if (arg == "--fps" && i + 1 < argc) {
      current_camera_config.fps = std::stoi(argv[++i]);
    } else if (arg == "--bitrate" && i + 1 < argc) {
      current_camera_config.bitrate = std::stoi(argv[++i]);
    } else if (arg == "--hevc") {
      current_camera_config.enableMvHevc = 1;
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --preview           Enable video preview\n";
      std::cout << "  --listen ADDR       Listen for control commands (IP:PORT)\n";
      std::cout << "  --send              Send video stream directly\n";
      std::cout << "  --server IP         Receiver IP address\n";
      std::cout << "  --port PORT         Receiver port\n";
      std::cout << "  --device PATH       V4L2 デバイスパス（単眼）(default: /dev/video0)\n";
      std::cout << "  --stereo            ステレオ（2台）モードを有効化\n";
      std::cout << "  --device-left PATH  ステレオ時の左カメラ (default: /dev/video0)\n";
      std::cout << "  --device-right PATH ステレオ時の右カメラ (default: /dev/video1)\n";
      std::cout << "  --width W           Capture width  (default: 1280)\n";
      std::cout << "  --height H          Capture height (default: 720)\n";
      std::cout << "  --fps FPS           Capture FPS    (default: 30)\n";
      std::cout << "  --bitrate BPS       Encode bitrate (default: 4000000)\n";
      std::cout << "  --hevc              Use H.265 encoder (default: H.264)\n";
      std::cout << "  --help              Show this help message\n";
      return 0;
    }
  }

  if (!listen_enabled && !send_enabled_mode) {
    std::cerr << "Error: Either --listen or --send option is required"
              << std::endl;
    std::cerr << "Use --help to see usage options" << std::endl;
    return -1;
  }

  if (send_enabled_mode && (send_to_server.empty() || send_to_port == 0)) {
    std::cerr << "Error: --send mode requires both --server and --port options"
              << std::endl;
    return -1;
  }

  preview_enabled.store(preview_enabled_local);

  // C1 カメラ識別子を設定（ステレオ時も同じ "C1"）
  current_camera_config.camera = "C1";
  if (stereo_mode) {
    std::cout << "[Stereo] Left: " << c1_device_left
              << "  Right: " << c1_device_right << std::endl;
  }

  if (send_enabled_mode) {
    std::cout << "Starting direct C1 video streaming to " << send_to_server
              << ":" << send_to_port << "..." << std::endl;

    {
      std::lock_guard<std::mutex> lock(config_mutex);
      // CLI で指定されなかった項目にデフォルト値を設定
      if (current_camera_config.width == 0)
        current_camera_config.width = 1280;
      if (current_camera_config.height == 0)
        current_camera_config.height = 720;
      if (current_camera_config.fps == 0)
        current_camera_config.fps = 30;
      if (current_camera_config.bitrate == 0)
        current_camera_config.bitrate = 4000000;
      current_camera_config.ip = send_to_server;
      current_camera_config.port = send_to_port;
    }

    startStreamingThread();

    std::cout << "Streaming started. Press Ctrl+C to stop." << std::endl;
    while (!stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

  } else if (listen_enabled) {
    std::cout << "Starting C1 video streaming server on " << listen_address
              << "..." << std::endl;

    listen_thread =
        make_unique_helper<std::thread>(listenThreadFunction, listen_address);

    std::cout << "Server started. Press Ctrl+C to stop." << std::endl;
    while (!stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (listen_thread && listen_thread->joinable()) {
      listen_thread->join();
    }
  }

  std::cout << "Shutting down..." << std::endl;
  stopStreamingThread();
  std::cout << "All threads stopped. Exiting." << std::endl;
  return 0;
}
