// includes
#include <stdio.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

// OpenCV header
#include <opencv2/opencv.hpp>

// Undefine Status if defined
#ifdef Status
#undef Status
#endif

// OpenGL and X11 headers
//#include <GL/glew.h>
#include <GL/freeglut.h>
#include <GL/glx.h>

#include <CT/DataFiles.hpp>
#include <Core/Assert.hpp>
#include <Core/Image.hpp>
#include <Core/Time.hpp>
#include <OpenCL/Device.hpp>
#include <OpenCL/Event.hpp>
#include <OpenCL/Program.hpp>
#include <OpenCL/cl-patched.hpp>

#include <boost/lexical_cast.hpp>

using namespace std;
using namespace cv;

bool runCpu = true;
bool runGpu = true;
bool displayGpu = true;
bool writeImages = false;

// change here

int cpuFunction(const Mat& src);
Mat applyGaussianFilter(const Mat& src);
Mat applyNonMaximumSuppression(const Mat& magnitude, const Mat& blurred, const Mat& angle);
void applyDoubleThresholding(const Mat& magnitude, Mat& strong, Mat& weak);
Mat applyHysteresis(Mat& strong, const Mat& weak);


int main(int argc, char* argv[]){

  cout << "Beginning of the project!" << endl;
   
  // load image
  String imgName;
  if (argc > 1)
    imgName = argv[1];
  else
    imgName = "test1.png";

  String imgPath = "../" + imgName;

  Mat src = imread(imgPath, IMREAD_GRAYSCALE);

  if (src.empty()) {
    std::cout << imgName + " is not a valid image." << std::endl;
    return 0;
  }

  imshow("src", src);

  cpuFunction(src);

  return 0;
}


Mat applyGaussianFilter(const Mat& src) {
  Mat blurred;
  blur(src, blurred, Size(3,3));
  return blurred;
}

Mat applyNonMaximumSuppression(const Mat& magnitude, const Mat& blurred, const Mat& angle) {
  Mat result = magnitude.clone();
  int neighbor1X, neighbor1Y, neighbor2X, neighbor2Y;
  float gradientAngle;

  for (int x = 0; x < blurred.rows; x++) {
    for (int y = 0; y < blurred.cols; y++) {
      gradientAngle = angle.at<float>(x, y); 

      // Normalize angle to be in the range [0, 180)
      gradientAngle = fmodf(fabs(gradientAngle), 180.0f);

      // Determine neighbors based on gradient angle
      if (gradientAngle <= 22.5f || gradientAngle > 157.5f) {
        neighbor1X = x - 1;
        neighbor1Y = y;
        neighbor2X = x + 1;
        neighbor2Y = y;
      } else if (gradientAngle <= 67.5f) {
        neighbor1X = x - 1;
        neighbor1Y = y - 1;
        neighbor2X = x + 1;
        neighbor2Y = y + 1;
      } else if (gradientAngle <= 112.5f) {
        neighbor1X = x;
        neighbor1Y = y - 1;
        neighbor2X = x;
        neighbor2Y = y + 1;
      } else { 
        neighbor1X = x - 1;
        neighbor1Y = y + 1;
        neighbor2X = x + 1;
        neighbor2Y = y - 1;
      }

      // Check bounds of neighbor1
      if (neighbor1X >= 0 && neighbor1X < blurred.rows && neighbor1Y >= 0 && neighbor1Y < blurred.cols) {
        if (result.at<float>(x, y) < result.at<float>(neighbor1X, neighbor1Y)) {
          result.at<float>(x, y) = 0;
          continue;
        }
      }

      // Check bounds of neighbor2
      if (neighbor2X >= 0 && neighbor2X < blurred.rows && neighbor2Y >= 0 && neighbor2Y < blurred.cols) {
        if (result.at<float>(x, y) < result.at<float>(neighbor2X, neighbor2Y)) {
          result.at<float>(x, y) = 0;
          continue;
        }
      }
    }
  }
  return result;
}

void applyDoubleThresholding(const Mat& magnitude, Mat& strong, Mat& weak){
  // apply double thresholding
  float magMax = 0.2, magMin = 0.1;
  float gradientMagnitude;
  for (int x = 0; x < magnitude.rows; x++) { 
    for (int y = 0; y < magnitude.cols; y++) { 
      gradientMagnitude = magnitude.at<float>(x, y);

      if (gradientMagnitude > magMax){
        strong.at<float>(x, y) = gradientMagnitude;
      }    
      else if (gradientMagnitude <= magMax && gradientMagnitude > magMin){
        weak.at<float>(x, y) = gradientMagnitude;
      };   
    }
  }
}

Mat applyHysteresis( Mat& strong, const Mat& weak) {
  imshow("strong_test", strong);
  imshow("weak_test", weak);
  for (int x = 0; x < strong.rows; x++) {
    for (int y = 0; y < strong.cols; y++) {
      if (weak.at<float>(x, y) != 0) {
        if (x + 1 < strong.rows && strong.at<float>(x + 1, y) != 0 ||
          x - 1 >= 0 && strong.at<float>(x - 1, y) != 0 ||
          y + 1 < strong.cols && strong.at<float>(x, y + 1) != 0 ||
          y - 1 >= 0 && strong.at<float>(x, y - 1) != 0 ||
          x - 1 >= 0 && y - 1 >= 0 && strong.at<float>(x - 1, y - 1) != 0 ||
          x + 1 < strong.rows && y - 1 >= 0 && strong.at<float>(x + 1, y - 1) != 0 ||
          x - 1 >= 0 && y + 1 < strong.cols && strong.at<float>(x - 1, y + 1) != 0 ||
          x + 1 < strong.rows && y + 1 < strong.cols && strong.at<float>(x + 1, y + 1) != 0) {
          strong.at<float>(x, y) = weak.at<float>(x, y);
        }
      }
    }
  }
  return strong;
}

int cpuFunction(const Mat& src) {
  // Apply Gaussian filter
  Mat blurred = applyGaussianFilter(src);
  imshow("blurred", blurred);

  // Compute image gradient
  Mat xComponent, yComponent;
  Sobel(blurred, xComponent, CV_32F, 1, 0, 3);
  Sobel(blurred, yComponent, CV_32F, 0, 1, 3);

  // Convert to polar coordinates
  Mat magnitude, angle;
  cartToPolar(xComponent, yComponent, magnitude, angle, true);

  // Normalize values
  normalize(magnitude, magnitude, 0, 1, NORM_MINMAX);

  // Apply non-maximum suppression
  Mat suppressed = applyNonMaximumSuppression(magnitude, blurred, angle);
  imshow("non-max suppression", suppressed);

  // Apply double thresholding
  Mat strong = Mat::zeros(magnitude.rows, magnitude.cols, CV_32F);
  Mat weak = Mat::zeros(magnitude.rows, magnitude.cols, CV_32F);
  applyDoubleThresholding(suppressed, strong, weak);
  imshow("strong", strong);
  imshow("Weak", weak);

  // Apply hysteresis
  Mat finalEdges = applyHysteresis(strong, weak);
  imshow("final edge detection", finalEdges);

  waitKey(0);
  return 0;
}