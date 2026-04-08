#pragma once

#include <opencv2/opencv.hpp>

class C1StereoCamera {
public:
    struct Config {
        int cam1_index = 0;
        int cam2_index = 2; // Default to 0 and 2 for Jetson setup
        int width = 1280;   // Capture width per camera (full width will be 2x)
        int height = 720;   // Capture height
        int fps = 30;       // Capture FPS
    };

    C1StereoCamera();
    ~C1StereoCamera();

    bool open(const Config& config);
    bool isOpened() const;
    bool grab();
    bool retrieveImage(cv::Mat& stereo_image);
    void close();

    int getWidth() const { return config_.width * 2; }
    int getHeight() const { return config_.height; }
    int getFPS() const { return config_.fps; }

private:
    cv::VideoCapture cap1_;
    cv::VideoCapture cap2_;
    Config config_;
    cv::Mat frame1_;
    cv::Mat frame2_;
};
