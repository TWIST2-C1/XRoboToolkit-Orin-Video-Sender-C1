#include "c1_stereo_camera.hpp"
#include <iostream>

C1StereoCamera::C1StereoCamera() {}

C1StereoCamera::~C1StereoCamera() {
    close();
}

bool C1StereoCamera::open(const Config& config) {
    config_ = config;

    // Use V4L2 for Jetson/Linux if possible, otherwise rely on default backend
    int backend = cv::CAP_V4L2;

    std::cout << "Opening C1 Camera 1 on device " << config_.cam1_index << "..." << std::endl;
    cap1_.open(config_.cam1_index, backend);
    std::cout << "Opening C1 Camera 2 on device " << config_.cam2_index << "..." << std::endl;
    cap2_.open(config_.cam2_index, backend);

    if (!cap1_.isOpened() || !cap2_.isOpened()) {
        std::cerr << "Failed to open one or both C1 cameras. "
                  << "dev" << config_.cam1_index << " opened=" << cap1_.isOpened() << ", "
                  << "dev" << config_.cam2_index << " opened=" << cap2_.isOpened() << std::endl;
        close();
        return false;
    }

    // Set properties for Camera 1
    cap1_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    cap1_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    cap1_.set(cv::CAP_PROP_FPS, config_.fps);
    // Use MJPG/YUYV if necessary, by default OpenCV will negotiate.
    cap1_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    // Set properties for Camera 2
    cap2_.set(cv::CAP_PROP_FRAME_WIDTH, config_.width);
    cap2_.set(cv::CAP_PROP_FRAME_HEIGHT, config_.height);
    cap2_.set(cv::CAP_PROP_FPS, config_.fps);
    cap2_.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));

    // Check negotiated resolution for Cam1
    int w1 = (int)cap1_.get(cv::CAP_PROP_FRAME_WIDTH);
    int h1 = (int)cap1_.get(cv::CAP_PROP_FRAME_HEIGHT);
    int fps1 = (int)cap1_.get(cv::CAP_PROP_FPS);
    std::cout << "Negotiated Cam1: " << w1 << "x" << h1 << "@" << fps1 << std::endl;

    // Check negotiated resolution for Cam2
    int w2 = (int)cap2_.get(cv::CAP_PROP_FRAME_WIDTH);
    int h2 = (int)cap2_.get(cv::CAP_PROP_FRAME_HEIGHT);
    int fps2 = (int)cap2_.get(cv::CAP_PROP_FPS);
    std::cout << "Negotiated Cam2: " << w2 << "x" << h2 << "@" << fps2 << std::endl;

    // Warn if the two cameras disagree on resolution
    if (w1 != w2 || h1 != h2) {
        std::cerr << "[C1StereoCamera] WARNING: Camera resolutions differ! "
                  << "Cam1=" << w1 << "x" << h1 << " Cam2=" << w2 << "x" << h2
                  << ". Using Cam1 values. Right frame will be resized in retrieveImage()." << std::endl;
    }

    // Store the ACTUAL negotiated values back into config_
    // so callers (e.g. GStreamer pipeline builder) can query the true resolution.
    //config_.width  = (w1 > 0) ? w1 : config_.width;
    //config_.height = (h1 > 0) ? h1 : config_.height;
    //config_.fps    = (fps1 > 0) ? fps1 : config_.fps;

    return true;
}

bool C1StereoCamera::isOpened() const {
    return cap1_.isOpened() && cap2_.isOpened();
}

bool C1StereoCamera::grab() {
    if (!isOpened()) return false;

    // Grab both cameras as quickly as possible consecutively to minimize sync error
    bool ret1 = cap1_.grab();
    bool ret2 = cap2_.grab();

    if (ret1 && ret2) {
        // Retrieve decodes the frame
        cap1_.retrieve(frame1_);
        cap2_.retrieve(frame2_);
        return true;
    }
    return false;
}

bool C1StereoCamera::retrieveImage(cv::Mat& stereo_image) {
    if (frame1_.empty() || frame2_.empty()) {
        return false;
    }

    // Usually cv::VideoCapture returns BGR, we convert to BGRA to match GStreamer pipeline
    cv::Mat bgra1, bgra2;
    cv::cvtColor(frame1_, bgra1, cv::COLOR_BGR2BGRA);
    cv::cvtColor(frame2_, bgra2, cv::COLOR_BGR2BGRA);

    // Concatenate horizontally (Cam 1 left, Cam 2 right)
    cv::hconcat(bgra1, bgra2, stereo_image);
    // 🌟🌟 追加：GStreamerに流し込む前に、パイプラインのサイズ（VRの要求サイズ）にピッタリ合わせる！
    cv::resize(stereo_image, stereo_image, cv::Size(config_.width * 2, config_.height));
    return true;
}

void C1StereoCamera::close() {
    if (cap1_.isOpened()) cap1_.release();
    if (cap2_.isOpened()) cap2_.release();
}
