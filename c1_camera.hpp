/**
 * c1_camera.hpp
 *
 * C1 カメラ（V4L2/UVC デバイス）抽象レイヤー。
 * ZED SDK の sl::Camera を置き換え、OpenCV の VideoCapture を使用する。
 * Linux 専用（Windows では <arpa/inet.h> 等の影響でビルドエラーが出ても可）。
 */

#pragma once

#include <iostream>
#include <opencv2/opencv.hpp>
#include <string>

/**
 * @brief C1 カメラ（V4L2 デバイス）のラッパークラス。
 *
 * 使い方:
 *   C1Camera cam;
 *   cam.open(0, 1280, 720, 30);
 *   cv::Mat bgra;
 *   while (cam.grabFrame(bgra)) { ... }
 *   cam.close();
 */
class C1Camera {
public:
  C1Camera() : width_(0), height_(0), fps_(0) {}
  ~C1Camera() { close(); }

  /**
   * @brief カメラを開く
   * @param device_index  V4L2 デバイス番号（/dev/video<N> の N）
   * @param width         要求解像度（幅）
   * @param height        要求解像度（高さ）
   * @param fps           要求フレームレート
   * @return 成功したら true
   */
  bool open(int device_index, int width, int height, int fps) {
    cap_.open(device_index, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
      std::cerr << "[C1Camera] Failed to open /dev/video" << device_index
                << std::endl;
      return false;
    }

    // 解像度・FPS を設定
    cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap_.set(cv::CAP_PROP_FPS, fps);

    // 実際に取得できた値を記録
    width_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    height_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    fps_ = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));

    std::cout << "[C1Camera] Opened /dev/video" << device_index
              << " at " << width_ << "x" << height_
              << " @" << fps_ << "fps" << std::endl;
    return true;
  }

  /**
   * @brief デバイスパス文字列でカメラを開く（例: "/dev/video0"）
   */
  bool open(const std::string &device_path, int width, int height, int fps) {
    cap_.open(device_path, cv::CAP_V4L2);
    if (!cap_.isOpened()) {
      std::cerr << "[C1Camera] Failed to open " << device_path << std::endl;
      return false;
    }

    cap_.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap_.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap_.set(cv::CAP_PROP_FPS, fps);

    width_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_WIDTH));
    height_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_HEIGHT));
    fps_ = static_cast<int>(cap_.get(cv::CAP_PROP_FPS));

    std::cout << "[C1Camera] Opened " << device_path
              << " at " << width_ << "x" << height_
              << " @" << fps_ << "fps" << std::endl;
    return true;
  }

  /**
   * @brief フレームを取得し BGRA に変換して返す
   * @param bgra_out  出力先 cv::Mat（CV_8UC4 / BGRA）
   * @return 成功したら true
   */
  bool grabFrame(cv::Mat &bgra_out) {
    if (!cap_.isOpened())
      return false;

    cv::Mat frame;
    if (!cap_.read(frame) || frame.empty()) {
      std::cerr << "[C1Camera] Failed to grab frame" << std::endl;
      return false;
    }

    // OpenCV は通常 BGR で取得するので BGRA に変換
    cv::cvtColor(frame, bgra_out, cv::COLOR_BGR2BGRA);
    return true;
  }

  /** @brief カメラを閉じる */
  void close() {
    if (cap_.isOpened()) {
      cap_.release();
      std::cout << "[C1Camera] Camera closed" << std::endl;
    }
  }

  bool isOpened() const { return cap_.isOpened(); }
  int width() const { return width_; }
  int height() const { return height_; }
  int fps() const { return fps_; }

private:
  cv::VideoCapture cap_;
  int width_;
  int height_;
  int fps_;
};

// =============================================================================
/**
 * @brief C1 カメラ 2 台を使ったステレオ（立体）カメラクラス。
 *
 * 左カメラ・右カメラのフレームを取得し、横並び（Side-by-Side, SBS）の
 * BGRA フレームとして返す。出力解像度は (width*2) x height。
 *
 * 使い方:
 *   C1StereoCamera stereo;
 *   stereo.open("/dev/video0", "/dev/video1", 1280, 720, 30);
 *   cv::Mat sbs;  // 2560x720 BGRA
 *   while (stereo.grabFrame(sbs)) { ... }
 *   stereo.close();
 */
class C1StereoCamera {
public:
  C1StereoCamera() {}
  ~C1StereoCamera() { close(); }

  /**
   * @brief 左右カメラを開く
   * @param device_left   左カメラ V4L2 パス（例: "/dev/video0"）
   * @param device_right  右カメラ V4L2 パス（例: "/dev/video1"）
   * @param width         各カメラの要求幅（SBS出力は width*2）
   * @param height        各カメラの要求高さ
   * @param fps           要求フレームレート
   * @return 両カメラとも開けたら true
   */
  bool open(const std::string &device_left, const std::string &device_right,
            int width, int height, int fps) {
    if (!left_.open(device_left, width, height, fps)) {
      std::cerr << "[C1Stereo] Failed to open left camera: " << device_left
                << std::endl;
      return false;
    }
    if (!right_.open(device_right, width, height, fps)) {
      std::cerr << "[C1Stereo] Failed to open right camera: " << device_right
                << std::endl;
      left_.close();
      return false;
    }
    std::cout << "[C1Stereo] Stereo cameras opened. SBS output: "
              << (left_.width() * 2) << "x" << left_.height() << std::endl;
    return true;
  }

  /**
   * @brief 左右フレームを取得し、SBS（横並び）BGRA フレームとして返す
   *
   * 左右カメラを順次読み取り cv::hconcat で合成する。
   * フレームの縦サイズが異なる場合は右カメラのフレームをリサイズして合わせる。
   *
   * @param sbs_out  出力 cv::Mat（width*2 x height, CV_8UC4 BGRA）
   * @return 成功したら true
   */
  bool grabFrame(cv::Mat &sbs_out) {
    cv::Mat left_bgra, right_bgra;

    if (!left_.grabFrame(left_bgra)) {
      std::cerr << "[C1Stereo] Failed to grab left frame" << std::endl;
      return false;
    }
    if (!right_.grabFrame(right_bgra)) {
      std::cerr << "[C1Stereo] Failed to grab right frame" << std::endl;
      return false;
    }

    // 高さが異なる場合は右フレームをリサイズ
    if (left_bgra.rows != right_bgra.rows ||
        left_bgra.cols != right_bgra.cols) {
      cv::resize(right_bgra, right_bgra,
                 cv::Size(left_bgra.cols, left_bgra.rows));
    }

    // 横並び（Left | Right）SBS フレームを生成
    cv::hconcat(left_bgra, right_bgra, sbs_out);
    return true;
  }

  /** @brief 両カメラを閉じる */
  void close() {
    left_.close();
    right_.close();
  }

  bool isOpened() const { return left_.isOpened() && right_.isOpened(); }

  /** SBS 出力の幅（各カメラ幅 x 2） */
  int sbsWidth() const { return left_.width() * 2; }
  /** SBS 出力の高さ（各カメラと同じ） */
  int height() const { return left_.height(); }
  int fps() const { return left_.fps(); }
  /** 単眼あたりの幅 */
  int eyeWidth() const { return left_.width(); }

private:
  C1Camera left_;
  C1Camera right_;
};
