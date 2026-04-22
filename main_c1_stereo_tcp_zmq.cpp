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
#include "c1_stereo_camera.hpp"
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <zmq.h>

#include "network_helper.hpp"

// Network Protocol Structures
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
      : width(0), height(0), fps(0), bitrate(0), enableMvHevc(0), renderMode(0),
        port(0) {}
};

struct NetworkDataProtocol {
  std::string command;
  int length;
  std::vector<uint8_t> data;

  NetworkDataProtocol() : length(0) {}
  NetworkDataProtocol(const std::string &cmd, const std::vector<uint8_t> &d)
      : command(cmd), data(d), length(d.size()) {}
};

// Deserialization functions
class CameraRequestDeserializer {
public:
  static CameraRequestData deserialize(const std::vector<uint8_t> &data) {
    if (data.size() < 10) {
      throw std::invalid_argument("Data is too small for valid camera request");
    }

    size_t offset = 0;

    // Check magic bytes (0xCA, 0xFE)
    if (data[offset] != 0xCA || data[offset + 1] != 0xFE) {
      throw std::invalid_argument("Invalid magic bytes");
    }
    offset += 2;

    // Check protocol version
    uint8_t version = data[offset++];
    if (version != 1) {
      throw std::invalid_argument("Unsupported protocol version");
    }

    CameraRequestData result;

    // Read integer fields (7 * 4 bytes)
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

    // Read strings with compact encoding
    result.camera = readCompactString(data, offset);
    result.ip = readCompactString(data, offset);

    return result;
  }

private:
  static int32_t readInt32(const std::vector<uint8_t> &data, size_t offset) {
    if (offset + 4 > data.size()) {
      throw std::out_of_range("Not enough data to read int32");
    }

    // Little-endian format (matching C# BitConverter default)
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
    if (buffer.size() <
        8) { // Minimum: 4 bytes command length + 4 bytes data length
      throw std::invalid_argument("Buffer too small for valid protocol data");
    }

    size_t offset = 0;

    // Read command length
    int32_t commandLength = readInt32(buffer, offset);
    offset += 4;

    if (commandLength < 0 || offset + commandLength > buffer.size()) {
      throw std::invalid_argument("Invalid command length");
    }

    // Read command
    std::string command;
    if (commandLength > 0) {
      command = std::string(reinterpret_cast<const char *>(&buffer[offset]),
                            commandLength);
      // Remove any null terminators
      size_t nullPos = command.find('\0');
      if (nullPos != std::string::npos) {
        command = command.substr(0, nullPos);
      }
    }
    offset += commandLength;

    if (offset + 4 > buffer.size()) {
      throw std::invalid_argument("Buffer too small for data length");
    }

    // Read data length
    int32_t dataLength = readInt32(buffer, offset);
    offset += 4;

    if (dataLength < 0 || offset + dataLength > buffer.size()) {
      throw std::invalid_argument("Invalid data length");
    }

    // Read data
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

    // Little-endian format
    return static_cast<int32_t>((data[offset]) | (data[offset + 1] << 8) |
                                (data[offset + 2] << 16) |
                                (data[offset + 3] << 24));
  }
};

// Global camera configuration
CameraRequestData current_camera_config;
int global_cam1_index = 0;
int global_cam2_index = 2;

// Thread-safe global state
std::atomic<bool> stop_requested{false};
std::atomic<bool> streaming_active{false};
std::atomic<bool> encoding_enabled{false};
std::atomic<bool> send_enabled{false};
std::atomic<bool> preview_enabled{false};
std::atomic<bool> zmq_enabled{false};
std::atomic<bool> zmq_raw_mode{false};  // true: 生画像を送信, false: H.264/H.265を送信

// Thread management
std::unique_ptr<std::thread> listen_thread;
std::unique_ptr<std::thread> streaming_thread;
std::mutex config_mutex;
std::condition_variable streaming_cv;
std::mutex streaming_mutex;

// Network components
std::unique_ptr<TCPClient> sender_ptr;
std::unique_ptr<TCPServer> server_ptr;
std::string send_to_server = "";
int send_to_port = 0;

// ZMQ components
void* zmq_context = nullptr;
void* zmq_publisher = nullptr;
std::string zmq_endpoint = "";
std::mutex zmq_mutex;

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
    // Sleep for 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    retry--;
  }
  return false;
}

bool initialize_zmq() {
  if (zmq_endpoint.empty()) {
    return false;
  }

  try {
    std::lock_guard<std::mutex> lock(zmq_mutex);

    // Create ZMQ context
    zmq_context = zmq_ctx_new();
    if (!zmq_context) {
      std::cerr << "Failed to create ZMQ context" << std::endl;
      return false;
    }

    // Create publisher socket
    zmq_publisher = zmq_socket(zmq_context, ZMQ_PUB);
    if (!zmq_publisher) {
      std::cerr << "Failed to create ZMQ publisher socket" << std::endl;
      zmq_ctx_destroy(zmq_context);
      zmq_context = nullptr;
      return false;
    }

    // Set socket options for better performance
    int hwm = 10;  // High water mark - drop old messages if queue is full
    zmq_setsockopt(zmq_publisher, ZMQ_SNDHWM, &hwm, sizeof(hwm));

    int linger = 0;  // Don't wait for pending messages on close
    zmq_setsockopt(zmq_publisher, ZMQ_LINGER, &linger, sizeof(linger));

    // Bind to endpoint
    if (zmq_bind(zmq_publisher, zmq_endpoint.c_str()) != 0) {
      std::cerr << "Failed to bind ZMQ publisher to " << zmq_endpoint << std::endl;
      zmq_close(zmq_publisher);
      zmq_ctx_destroy(zmq_context);
      zmq_publisher = nullptr;
      zmq_context = nullptr;
      return false;
    }

    std::cout << "ZMQ publisher bound to " << zmq_endpoint << std::endl;
    return true;

  } catch (const std::exception& e) {
    std::cerr << "Exception during ZMQ initialization: " << e.what() << std::endl;
    return false;
  }
}

void cleanup_zmq() {
  std::lock_guard<std::mutex> lock(zmq_mutex);

  if (zmq_publisher) {
    zmq_close(zmq_publisher);
    zmq_publisher = nullptr;
  }

  if (zmq_context) {
    zmq_ctx_destroy(zmq_context);
    zmq_context = nullptr;
  }

  std::cout << "ZMQ cleaned up" << std::endl;
}

// Template helper for C++11 make_unique replacement
template <typename T, typename... Args>
std::unique_ptr<T> make_unique_helper(Args &&...args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

// Forward declarations
void handleOpenCamera(const std::vector<uint8_t> &data);
void handleCloseCamera(const std::vector<uint8_t> &data);
void startStreamingThread();
void stopStreamingThread();
void streamingThreadFunction();
void listenThreadFunction(const std::string &listen_address);

void onDataCallback(const std::string &command) {
  // Convert string to binary data for protocol parsing
  std::vector<uint8_t> binaryData(command.begin(), command.end());

  for (size_t i = 0; i < std::min(binaryData.size(), size_t(32)); ++i) {
    std::cout << std::hex << std::setfill('0') << std::setw(2)
              << static_cast<unsigned int>(binaryData[i]) << " ";
  }
  std::cout << std::dec << std::endl;

  // First, extract the actual protocol data from the 4-byte wrapper
  if (binaryData.size() < 4) {
    std::cerr << "Data too small to contain length header" << std::endl;
    return;
  }

  // Read the 4-byte header (big-endian format) to get the actual data length
  uint32_t bodyLength = (static_cast<uint32_t>(binaryData[0]) << 24) |
                        (static_cast<uint32_t>(binaryData[1]) << 16) |
                        (static_cast<uint32_t>(binaryData[2]) << 8) |
                        static_cast<uint32_t>(binaryData[3]);

  if (4 + bodyLength > binaryData.size()) {
    std::cerr << "Data too small for declared body length. Expected: "
              << (4 + bodyLength) << ", got: " << binaryData.size()
              << std::endl;
    return;
  }

  // Extract the actual protocol data (skip the 4-byte header)
  std::vector<uint8_t> protocolData(binaryData.begin() + 4,
                                    binaryData.begin() + 4 + bodyLength);

  for (size_t i = 0; i < std::min(protocolData.size(), size_t(32)); ++i) {
    std::cout << std::hex << std::setfill('0') << std::setw(2)
              << static_cast<unsigned int>(protocolData[i]) << " ";
  }
  std::cout << std::dec << std::endl;

  // Now try to parse as NetworkDataProtocol format (with length prefixes)
  try {
    // Debug: Print first few fields of the protocol data
    if (protocolData.size() >= 8) {
      int32_t cmdLen = (protocolData[0]) | (protocolData[1] << 8) |
                       (protocolData[2] << 16) | (protocolData[3] << 24);
      int32_t dataLenPos = 4 + cmdLen;
      std::cout << "Command length: " << cmdLen << std::endl;
      if (static_cast<size_t>(dataLenPos + 4) <= protocolData.size()) {
        int32_t dataLen = (protocolData[dataLenPos]) |
                          (protocolData[dataLenPos + 1] << 8) |
                          (protocolData[dataLenPos + 2] << 16) |
                          (protocolData[dataLenPos + 3] << 24);
        std::cout << "Data length: " << dataLen << std::endl;
      }
    }

    NetworkDataProtocol protocol =
        NetworkDataProtocolDeserializer::deserialize(protocolData);

    std::cout << "Received protocol command: '" << protocol.command
              << "' (length: " << protocol.command.length() << ")" << std::endl;

    // Handle the protocol commands
    if (protocol.command == "OPEN_CAMERA") {
      handleOpenCamera(protocol.data);
    } else if (protocol.command == "CLOSE_CAMERA") {
      handleCloseCamera(protocol.data);
    } else {
      std::cout << "Unknown protocol command: " << protocol.command
                << std::endl;
    }
    return;
  } catch (const std::exception &e) {
    // If NetworkDataProtocol parsing fails, try simple command format
    std::cout << "Failed to parse as NetworkDataProtocol: " << e.what()
              << std::endl;
  }
}

void onDisconnectCallback() {
  std::cout << "Client disconnected, stopping streaming" << std::endl;
  stopStreamingThread();
}

void listenThreadFunction(const std::string &listen_address) {
  std::cout << "Listen thread started on " << listen_address << std::endl;

  while (!stop_requested.load()) {
    try {
      // Initialize TCPServer
      server_ptr = make_unique_helper<TCPServer>(listen_address);
      server_ptr->setDataCallback(onDataCallback);
      server_ptr->setDisconnectCallback(onDisconnectCallback);
      server_ptr->start();
      std::cout << "TCPServer is listening on " << listen_address << std::endl;

      // Wait for server to stop (client disconnect or error)
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

void handle_sigint(int) {
  std::cout << "\nSIGINT received. Stopping all threads..." << std::endl;
  stop_requested.store(true);

  // Stop streaming first
  stopStreamingThread();

  // Stop server
  if (server_ptr) {
    server_ptr->stop();
    server_ptr = nullptr;
  }

  // Cleanup ZMQ
  cleanup_zmq();

  // Wake up any waiting threads
  streaming_cv.notify_all();
}

GstFlowReturn on_new_sample(GstAppSink *sink, gpointer user_data) {
  GstSample *sample = gst_app_sink_pull_sample(sink);
  if (!sample)
    return GST_FLOW_ERROR;

  GstBuffer *buffer = gst_sample_get_buffer(sample);
  GstMapInfo map;
  if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
    const uint8_t *data = map.data;
    gsize size = map.size;

    // TCP送信 (既存ロジック)
    if (send_enabled.load() && sender_ptr && sender_ptr->isConnected() &&
        data && size > 0) {
      try {
        std::vector<uint8_t> packet(4 + size);
        packet[0] = (size >> 24) & 0xFF;
        packet[1] = (size >> 16) & 0xFF;
        packet[2] = (size >> 8) & 0xFF;
        packet[3] = (size)&0xFF;
        std::copy(data, data + size, packet.begin() + 4);

        sender_ptr->sendData(packet);
      } catch (const TCPException &e) {
        std::cerr << "TCP error in on_new_sample: " << e.what() << std::endl;
        // Don't quit the whole program, just stop streaming
        streaming_active.store(false);
      } catch (const std::exception &e) {
        std::cerr << "Unexpected error in on_new_sample: " << e.what()
                  << std::endl;
        streaming_active.store(false);
      }
    }

    // ZMQ送信 - エンコード済みストリーム (zmq_raw_mode でない場合)
    if (zmq_enabled.load() && !zmq_raw_mode.load() && zmq_publisher && data && size > 0) {
      try {
        std::lock_guard<std::mutex> lock(zmq_mutex);

        // 長さヘッダ付きメッセージを作成
        std::vector<uint8_t> zmq_packet(4 + size);
        zmq_packet[0] = (size >> 24) & 0xFF;
        zmq_packet[1] = (size >> 16) & 0xFF;
        zmq_packet[2] = (size >> 8) & 0xFF;
        zmq_packet[3] = (size) & 0xFF;
        std::copy(data, data + size, zmq_packet.begin() + 4);

        // ZMQで送信 (ノンブロッキング)
        int rc = zmq_send(zmq_publisher, zmq_packet.data(), zmq_packet.size(), ZMQ_DONTWAIT);
        if (rc == -1) {
          if (zmq_errno() != EAGAIN) {  // EAGAINはキューが満杯、許容範囲
            std::cerr << "ZMQ send error: " << zmq_strerror(zmq_errno()) << std::endl;
          }
        }
      } catch (const std::exception &e) {
        std::cerr << "Unexpected error in ZMQ send: " << e.what() << std::endl;
      }
    }

    gst_buffer_unmap(buffer, &map);
  }

  if (buffer) {
    GstClockTime timestamp = GST_BUFFER_PTS(buffer);
    // std::cout << "Encoded frame at timestamp: "
    //           << GST_TIME_AS_MSECONDS(timestamp) << " ms" << std::endl;
    (void)timestamp; // suppress unused warning
  }

  gst_sample_unref(sample);
  return GST_FLOW_OK;
}

// Handler function implementations
void handleOpenCamera(const std::vector<uint8_t> &data) {
  std::cout << "Handling OPEN_CAMERA command" << std::endl;

  try {
    // Parse the camera configuration data
    CameraRequestData cameraConfig =
        CameraRequestDeserializer::deserialize(data);
    // 追加：PICOからの片目サイズ(1280)を、ステレオ両目サイズ(2560)に2倍にする！
    cameraConfig.width = cameraConfig.width * 2;
    //cameraConfig.bitrate = 8000000;

    std::cout << "Camera config - Width: " << cameraConfig.width
              << ", Height: " << cameraConfig.height
              << ", FPS: " << cameraConfig.fps
              << ", Bitrate: " << cameraConfig.bitrate
              << ", IP: " << cameraConfig.ip << ", Port: " << cameraConfig.port
              << ", type: " << cameraConfig.camera << std::endl;
    // Do nothing if the type is not "C1"
    if (cameraConfig.camera != "C1") {
      std::cout << "Unsupported camera type: " << cameraConfig.camera
                << ". Only 'C1' is supported in this program." << std::endl;
      return;
    }

    // Store the camera configuration globally
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      current_camera_config = cameraConfig;
    }

    // Set the global sender connection parameters from config
    send_to_server = cameraConfig.ip;
    send_to_port = cameraConfig.port;

    std::cout << "Updated sender target to " << send_to_server << ":"
              << send_to_port << std::endl;

    // Start the streaming thread which will use the updated config
    startStreamingThread();

  } catch (const std::exception &e) {
    std::cerr << "Failed to parse camera config: " << e.what() << std::endl;
    // Start with default configuration only if we have valid defaults
    if (!send_to_server.empty() && send_to_port > 0) {
      startStreamingThread();
    } else if (zmq_enabled.load()) {
      // ZMQのみでも開始可能
      startStreamingThread();
    } else {
      std::cerr
          << "No valid server configuration available, cannot start streaming"
          << std::endl;
    }
  }
}

void handleCloseCamera(const std::vector<uint8_t> &data) {
  std::cout << "Handling CLOSE_CAMERA command" << std::endl;
  stopStreamingThread();
}

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
  zmq_enabled.store(false);

  // Disconnect sender if connected
  if (sender_ptr && sender_ptr->isConnected()) {
    sender_ptr->disconnect();
  }
  sender_ptr = nullptr;

  // Wait for streaming thread to finish
  if (streaming_thread && streaming_thread->joinable()) {
    streaming_cv.notify_all();
    streaming_thread->join();
    streaming_thread = nullptr;
    std::cout << "Stopped streaming thread" << std::endl;
  }
}

// Pipeline configuration functions
std::string buildPipelineString(const CameraRequestData &config,
                                bool preview_enabled) {
  std::string pipeline_str = "appsrc name=mysource is-live=true format=time ";

  // Use configuration parameters for caps
  pipeline_str +=
      "caps=video/x-raw,format=BGRA,width=" + std::to_string(config.width) +
      ",height=" + std::to_string(config.height) +
      ",framerate=" + std::to_string(config.fps) + "/1 ! ";

  pipeline_str += "videoconvert ! nvvidconv ! "
                  "video/x-raw(memory:NVMM),format=NV12 ! tee name=t ";

  // Configure encoder based on settings
  std::string encoder = config.enableMvHevc ? "nvv4l2h265enc" : "nvv4l2h264enc";
  std::string parser = config.enableMvHevc ? "h265parse" : "h264parse";

  pipeline_str +=
      "t. ! queue ! " + encoder + " maxperf-enable=1 insert-sps-pps=true ";
  pipeline_str +=
      "idrinterval=15 bitrate=" + std::to_string(config.bitrate) + " ! ";
  pipeline_str +=
      parser + " ! appsink name=mysink emit-signals=true sync=false ";

  if (preview_enabled) {
    pipeline_str +=
        "t. ! queue ! nvvidconv ! videoconvert ! autovideosink sync=false ";
  }

  return pipeline_str;
}

void updateCameraConfiguration(C1StereoCamera &c1, const CameraRequestData &config) {
  std::cout << "Camera would be configured with:" << std::endl;
  std::cout << "  Resolution: " << config.width << "x" << config.height << std::endl;
  std::cout << "  FPS: " << config.fps << std::endl;
  std::cout << "  Bitrate: " << config.bitrate << std::endl;
  std::cout << "  HEVC: " << (config.enableMvHevc ? "enabled" : "disabled") << std::endl;
}

int main(int argc, char *argv[]) {
  gst_init(&argc, &argv);
  signal(SIGINT, handle_sigint);

  // Parse command line arguments
  bool preview_enabled_local = false;
  bool listen_enabled = false;
  bool send_enabled_mode = false;
  bool zmq_enabled_mode = false;
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
    } else if (arg == "--cam1" && i + 1 < argc) {
      global_cam1_index = std::stoi(argv[++i]);
    } else if (arg == "--cam2" && i + 1 < argc) {
      global_cam2_index = std::stoi(argv[++i]);
    } else if (arg == "--zmq" && i + 1 < argc) {
      zmq_enabled_mode = true;
      zmq_endpoint = argv[++i];
    } else if (arg == "--zmq-raw" && i + 1 < argc) {
      zmq_enabled_mode = true;
      zmq_endpoint = argv[++i];
      zmq_raw_mode.store(true);
    } else if (arg == "--help") {
      std::cout << "Usage: " << argv[0] << " [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --preview          Enable video preview\n";
      std::cout << "  --listen ADDR      Listen to control commands on address "
                   "(IP:PORT)\n";
      std::cout << "  --send             Send video stream directly to server\n";
      std::cout << "  --server IP        Server IP address\n";
      std::cout << "  --port PORT        Server port\n";
      std::cout << "  --cam1 IDX         Index for left camera (default: 0)\n";
      std::cout << "  --cam2 IDX         Index for right camera (default: 2)\n";
      std::cout << "  --zmq ENDPOINT     ZMQ publish endpoint for H.264/H.265 stream (e.g. tcp://*:5555)\n";
      std::cout << "  --zmq-raw ENDPOINT ZMQ publish endpoint for raw images (e.g. tcp://*:5556)\n";
      std::cout << "  --help             Show this help message\n";
      std::cout << "\nExamples:\n";
      std::cout << "  # TCP + ZMQ (encoded stream)\n";
      std::cout << "  " << argv[0] << " --send --server 192.168.1.100 --port 8080 --zmq tcp://*:5555\n";
      std::cout << "  # Listen mode + ZMQ\n";
      std::cout << "  " << argv[0] << " --listen 0.0.0.0:9090 --zmq tcp://*:5555\n";
      std::cout << "  # ZMQ only (raw images, low latency)\n";
      std::cout << "  " << argv[0] << " --send --zmq-raw tcp://*:5556\n";
      std::cout << "  # ZMQ only (encoded stream)\n";
      std::cout << "  " << argv[0] << " --send --zmq tcp://*:5555\n";
      std::cout << "  # Specify camera devices\n";
      std::cout << "  " << argv[0] << " --send --zmq tcp://*:5555 --cam1 0 --cam2 2\n";
      return 0;
    }
  }

  if (!listen_enabled && !send_enabled_mode) {
    std::cerr << "Error: Either --listen or --send option is required"
              << std::endl;
    std::cerr << "Use --help to see usage options" << std::endl;
    return -1;
  }

  if (send_enabled_mode && (send_to_server.empty() || send_to_port == 0) && !zmq_enabled_mode) {
    std::cerr << "Error: --send mode requires either TCP (--server and --port) or ZMQ (--zmq/--zmq-raw) options"
              << std::endl;
    std::cerr << "Use --help to see usage options" << std::endl;
    return -1;
  }

  // Initialize ZMQ if requested
  if (zmq_enabled_mode) {
    if (initialize_zmq()) {
      zmq_enabled.store(true);
      std::cout << "ZMQ publisher initialized on " << zmq_endpoint << std::endl;
      if (zmq_raw_mode.load()) {
        std::cout << "ZMQ mode: raw images (BGRA, no encoding)" << std::endl;
      } else {
        std::cout << "ZMQ mode: encoded H.264/H.265 stream" << std::endl;
      }
    } else {
      std::cerr << "Failed to initialize ZMQ, continuing without ZMQ support" << std::endl;
    }
  }

  if (send_enabled_mode) {
    std::cout << "Starting direct video streaming";
    if (!send_to_server.empty() && send_to_port > 0) {
      std::cout << " to " << send_to_server << ":" << send_to_port;
    }
    if (zmq_enabled.load()) {
      std::cout << " and ZMQ " << zmq_endpoint;
    }
    std::cout << "..." << std::endl;

    // Set global preview flag
    preview_enabled.store(preview_enabled_local);

    // Set up default camera configuration for direct streaming
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      current_camera_config.width = 2560;
      current_camera_config.height = 720;
      current_camera_config.fps = 30;
      current_camera_config.bitrate = 8000000;
      current_camera_config.enableMvHevc = 1;
      current_camera_config.renderMode = 0;
      current_camera_config.camera = "C1";
      current_camera_config.ip = send_to_server;
      current_camera_config.port = send_to_port;
    }

    // Start streaming directly
    startStreamingThread();

    // Main thread waits for termination signal
    std::cout << "Streaming started. Press Ctrl+C to stop." << std::endl;

    while (!stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  } else if (listen_enabled) {
    std::cout << "Starting threaded video streaming server...";
    if (zmq_enabled.load()) {
      std::cout << " with ZMQ support on " << zmq_endpoint;
    }
    std::cout << std::endl;

    // Set global preview flag
    preview_enabled.store(preview_enabled_local);

    // Start listening thread
    listen_thread =
        make_unique_helper<std::thread>(listenThreadFunction, listen_address);

    // Main thread waits for termination signal
    std::cout << "Server started. Press Ctrl+C to stop." << std::endl;

    while (!stop_requested.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Stop listening thread
    if (listen_thread && listen_thread->joinable()) {
      listen_thread->join();
    }
  }

  std::cout << "Shutting down..." << std::endl;

  // Stop streaming thread first
  stopStreamingThread();

  // Cleanup ZMQ
  cleanup_zmq();

  std::cout << "All threads stopped. Exiting." << std::endl;
  return 0;
}

void streamingThreadFunction() {
  std::cout << "Streaming thread started" << std::endl;

  try {
    // Initialize sender (optional for TCP)
    bool tcp_initialized = false;
    if (!send_to_server.empty() && send_to_port > 0) {
      tcp_initialized = initialize_sender();
      if (!tcp_initialized) {
        std::cerr << "Failed to initialize TCP sender, continuing with ZMQ only" << std::endl;
      }
    }

    // Enable streaming flags
    encoding_enabled.store(true);
    if (tcp_initialized) {
      send_enabled.store(true);
    }
    if (!zmq_endpoint.empty() && zmq_publisher) {
      zmq_enabled.store(true);
    }

    // Check if at least one output method is available
    if (!send_enabled.load() && !zmq_enabled.load()) {
      std::cerr << "No output method available (neither TCP nor ZMQ), stopping" << std::endl;
      return;
    }

    // Initialize C1 Stereo camera
    C1StereoCamera c1;
    C1StereoCamera::Config init_params;

    init_params.cam1_index = global_cam1_index;
    init_params.cam2_index = global_cam2_index;

    // Get camera configuration safely
    CameraRequestData config;
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      config = current_camera_config;
    }

    // Configure C1 based on received config
    if (config.width > 0 && config.height > 0) {
      init_params.width = config.width / 2; // config.width is the total SBS width, per camera is half
      init_params.height = config.height;
      if (config.fps > 0) {
        init_params.fps = config.fps;
      } else {
        init_params.fps = 60; // default
      }
    } else {
      // Use default values if no config
      init_params.width = 1280;
      init_params.height = 720;
      init_params.fps = 60;
    }

    if (!c1.open(init_params)) {
      std::cerr << "Failed to open C1 stereo cameras in streaming thread" << std::endl;
      return;
    }

    // -------------------------------------------------------------------
    // 修正①: カメラが実際にネゴシエートした解像度・FPS で config を上書き。
    // c1_stereo_camera.cpp の open() が config_.width/height/fps を
    // 実際の値に書き戻しているため、getWidth()/getHeight()/getFPS() は
    // 「実際の SBS 幅」「実際の高さ」「実際の FPS」を返す。
    // これを使わないと GStreamer の caps とフレームサイズが食い違い画像化けする。
    // -------------------------------------------------------------------
    {
      int actual_sbs_w = c1.getWidth();   // cam_width * 2
      int actual_h     = c1.getHeight();
      int actual_fps   = c1.getFPS();
      if (actual_sbs_w > 0 && actual_h > 0) {
        std::cout << "Actual camera resolution (SBS): " << actual_sbs_w
                  << "x" << actual_h << "@" << actual_fps << std::endl;
        //config.width  = actual_sbs_w;
        //config.height = actual_h;
        //if (actual_fps > 0) config.fps = actual_fps;
      } else {
        std::cerr << "WARNING: Could not read actual camera resolution, "
                     "using requested values (" << config.width << "x"
                  << config.height << ")" << std::endl;
      }
    }

    // Build GStreamer pipeline (needed for TCP and ZMQ encoded mode)
    // For zmq-raw mode only, we still need the pipeline if TCP is also active
    bool need_pipeline = send_enabled.load() || (zmq_enabled.load() && !zmq_raw_mode.load());

    GstElement *pipeline = nullptr;
    GstElement *appsrc = nullptr;
    GstElement *appsink = nullptr;

    if (need_pipeline) {
      std::string pipeline_str;
      if (config.width > 0 && config.height > 0) {
        pipeline_str = buildPipelineString(config, preview_enabled.load());
        std::cout << "Pipeline from command: " << pipeline_str << std::endl;
      } else {
        // Default pipeline
        if (preview_enabled.load()) {
          pipeline_str =
              "appsrc name=mysource is-live=true format=time "
              "caps=video/x-raw,format=BGRA,width=2560,height=720,framerate=60/1 "
              "! "
              "videoconvert ! nvvidconv ! video/x-raw(memory:NVMM),format=NV12 ! "
              "tee name=t "
              "t. ! queue ! nvv4l2h264enc maxperf-enable=1 insert-sps-pps=true "
              "idrinterval=15 bitrate=4000000 ! h264parse ! appsink name=mysink "
              "emit-signals=true sync=false "
              "t. ! queue ! nvvidconv ! videoconvert ! autovideosink sync=false ";
        } else {
          pipeline_str =
              "appsrc name=mysource is-live=true format=time "
              "caps=video/x-raw,format=BGRA,width=2560,height=720,framerate=60/1 "
              "! "
              "videoconvert ! nvvidconv ! video/x-raw(memory:NVMM),format=NV12 ! "
              "tee name=t "
              "t. ! queue ! nvv4l2h264enc maxperf-enable=1 insert-sps-pps=true "
              "idrinterval=15 bitrate=4000000 ! h264parse ! appsink name=mysink "
              "emit-signals=true sync=false ";
        }
      }

      // Launch pipeline
      GError *error = nullptr;
      pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
      if (!pipeline) {
        std::cerr << "Failed to create pipeline in streaming thread: "
                  << error->message << std::endl;
        g_clear_error(&error);
        c1.close();
        return;
      }

      // Bind appsrc/appsink
      appsrc = gst_bin_get_by_name(GST_BIN(pipeline), "mysource");
      appsink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");

      g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample), nullptr);
      gst_element_set_state(pipeline, GST_STATE_PLAYING);
    } else {
      std::cout << "ZMQ raw mode only - skipping GStreamer pipeline" << std::endl;
    }

    cv::Mat cv_image;
    int frame_id = 0;

    std::cout << "Starting streaming loop..." << std::endl;
    while (streaming_active.load() && !stop_requested.load()) {
      if (c1.grab()) {
        c1.retrieveImage(cv_image);

        // ZMQ生画像送信 (zmq_raw_mode の場合)
        if (zmq_enabled.load() && zmq_raw_mode.load() && zmq_publisher &&
            cv_image.data && cv_image.total() > 0) {
          try {
            std::lock_guard<std::mutex> lock(zmq_mutex);

            // メッセージ形式: [4バイト幅][4バイト高さ][4バイトチャンネル数][画像データ]
            size_t data_size = cv_image.total() * cv_image.elemSize();
            std::vector<uint8_t> zmq_packet(12 + data_size);

            // 画像情報を書き込み
            int width = cv_image.cols;
            int height = cv_image.rows;
            int channels = cv_image.channels();

            std::memcpy(&zmq_packet[0], &width, 4);
            std::memcpy(&zmq_packet[4], &height, 4);
            std::memcpy(&zmq_packet[8], &channels, 4);

            // 画像データを書き込み
            std::memcpy(&zmq_packet[12], cv_image.data, data_size);

            // ZMQで送信 (ノンブロッキング)
            int rc = zmq_send(zmq_publisher, zmq_packet.data(), zmq_packet.size(), ZMQ_DONTWAIT);
            if (rc == -1) {
              if (zmq_errno() != EAGAIN) {
                std::cerr << "ZMQ send error: " << zmq_strerror(zmq_errno()) << std::endl;
              }
            }
          } catch (const std::exception &e) {
            std::cerr << "Unexpected error in ZMQ raw image send: " << e.what() << std::endl;
          }
        }

        // GStreamerパイプラインへのフレーム送信
        if (need_pipeline && encoding_enabled.load()) {

          GstBuffer *buffer = gst_buffer_new_allocate(
              nullptr, cv_image.total() * cv_image.elemSize(), nullptr);
          GstMapInfo map;
          gst_buffer_map(buffer, &map, GST_MAP_WRITE);
          memcpy(map.data, cv_image.data,
                 cv_image.total() * cv_image.elemSize());
          gst_buffer_unmap(buffer, &map);

          // 修正②: タイムスタンプは実際の FPS を使う（固定 60 は誤り）
          int ts_fps = (config.fps > 0) ? config.fps : 60;
          GST_BUFFER_PTS(buffer) =
              gst_util_uint64_scale(frame_id, GST_SECOND, ts_fps);
          GST_BUFFER_DURATION(buffer) =
              gst_util_uint64_scale(1, GST_SECOND, ts_fps);
          gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

          frame_id++;
        }
      }
    }

    std::cout << "Streaming loop ended, cleaning up..." << std::endl;

    // Clean shutdown
    if (need_pipeline && pipeline) {
      gst_app_src_end_of_stream(GST_APP_SRC(appsrc));
      gst_element_set_state(pipeline, GST_STATE_NULL);
      gst_object_unref(appsrc);
      gst_object_unref(appsink);
      gst_object_unref(pipeline);
    }
    c1.close();

  } catch (const std::exception &e) {
    std::cerr << "Streaming thread error: " << e.what() << std::endl;
  }

  std::cout << "Streaming thread finished" << std::endl;
}
