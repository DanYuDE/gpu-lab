// includes
#include <stdio.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <cstddef>
#include <vector>

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
using namespace cl;

bool runCpu = true;
bool runGpu = true;
bool displayGpu = true;
bool writeImages = false;

// change here

int cpuFunction ( const Mat& src );
Mat GaussianFilter ( const Mat& src );
Mat NonMaximumSuppression ( const Mat& magnitude, const Mat& blurred, const Mat& angle );
void DoubleThresholding ( const Mat& magnitude, Mat& strong, Mat& weak );
Mat Hysteresis ( Mat& strong, const Mat& weak );
cl::Image2D createImage2DFromMat ( const cl::Context& context, const cv::Mat& mat, bool readOnly );


int main ( int argc, char* argv[] ) {

    cout << "Beginning of the project!" << endl;

    // GPU setup ------------------------------------------
    // Create Context
    cl::Context context ( CL_DEVICE_TYPE_GPU );
    // Device list
    int deviceNr = argc < 2 ? 1 : atoi ( argv[1] );
    cout << "Using device " << deviceNr << " / "
         << context.getInfo<CL_CONTEXT_DEVICES>().size() << std::endl;
    ASSERT ( deviceNr > 0 );
    ASSERT ( ( std::size_t ) deviceNr <= context.getInfo<CL_CONTEXT_DEVICES>().size() );
    cl::Device device = context.getInfo<CL_CONTEXT_DEVICES>() [deviceNr - 1];
    std::vector<cl::Device> devices;
    devices.push_back ( device );
    OpenCL::printDeviceInfo ( std::cout, device );

    // Create a command queue
    cl::CommandQueue queue ( context, device, CL_QUEUE_PROFILING_ENABLE );


    // Load the source code
    extern unsigned char canny_cl[];
    extern unsigned int canny_cl_len;
    cl::Program program ( context,
                          std::string ( ( const char* ) canny_cl,
                                        canny_cl_len ) );

    OpenCL::buildProgram ( program, devices );

    // ----------------------------------------------------

    // Declare some value for GPU -------------------------
    std::size_t wgSizeX =
        16;  // Number of work items per work group in X direction
    std::size_t wgSizeY = 16;
    std::size_t countX =
        wgSizeX *
        40;  // Overall number of work items in X direction = Number of elements in X direction
    std::size_t countY = wgSizeY * 30;
    //countX *= 3; countY *= 3;
    std::size_t count = countX * countY;       // Overall number of elements
    std::size_t size = count * sizeof ( float ); // Size of data in bytes

    // Allocate space for output data from CPU and GPU on the host
    std::vector<float> h_input ( count );
    std::vector<float> h_outputCpu ( count );
    std::vector<float> h_outputGpu ( count );

    // vector<float> h_intermediate_sbl (count);
    // vector<float> h_intermediate_nms (count);
    // vector<float> h_intermediate_strong (count);
    // vector<float> h_intermediate_weak (count);


    // Allocate space for input and output data on the device
    cl::Buffer d_input ( context, CL_MEM_READ_WRITE, size );
    cl::Image2D d_output ( context, CL_MEM_READ_WRITE,
                           cl::ImageFormat ( CL_R, CL_FLOAT ), countX, countY );
    // ----------------------------------------------------

    // load image
    string imgName;
    if ( argc > 1 )
        imgName = argv[1];
    else
        imgName = "test1.png";

    string imgPath = "../" + imgName;

    Mat img = imread ( imgPath, IMREAD_GRAYSCALE );
    // Ensure the image is of type CV_32F
    if ( img.type() != CV_32F ) {
        img.convertTo ( img, CV_32F );
        }
    imshow("Convert Img", img);
    Mat blurred;
    blur ( img, blurred, Size ( 3,3 ) );

    int rows = blurred.rows;
    int cols = blurred.cols;
    std::vector<float> imgVector;
    imgVector.assign ( ( float* ) blurred.datastart, ( float* ) blurred.dataend );
    cout << rows << " " << cols << endl;

    for ( std::size_t j = 0; j < countY; j++ ) {
        for ( std::size_t i = 0; i < countX; i++ ) {
            h_input[i + countX * j] = imgVector[ ( i % cols ) + cols * ( j % rows )];
            }
        }
    // for ( size_t k = 0; k < h_input.size(); k++ )
    //     cout << h_input[k] << " ";
    if ( blurred.empty() ) {
        std::cout << imgName + " is not a valid image." << std::endl;
        return 0;
        }

    // imshow ( "img", img );
    Core::TimeSpan cpuStart = Core::getCurrentTime();
    cpuFunction ( blurred );
    Core::TimeSpan cpuEnd = Core::getCurrentTime();


    cout << "CPU implementation successfully!" << endl;

    cl::Image2D image2D = createImage2DFromMat ( context, blurred, true );

    cout << "Convert Mat to image2D successfully" << endl;

    cl::size_t<3> origin;
    origin[0] = 0;
    origin[1] = 0;
    origin[2] = 0;
    cl::size_t<3> region;
    region[0] = countX;
    region[1] = countY;
    region[2] = 1;

    // GPU ----------------------------------------------------
    memset ( h_outputGpu.data(), 255, size );
    queue.enqueueWriteImage ( d_output, true, origin, region,
                              countX * sizeof ( float ), 0, h_outputGpu.data() );

    cl::Event copy1;
    cl::Image2D image;
    image = cl::Image2D ( context, CL_MEM_READ_WRITE,
                          cl::ImageFormat ( CL_R, CL_FLOAT ), countX, countY );

    queue.enqueueWriteImage ( image, true, origin, region,
                              countX * sizeof ( float ), 0, imgVector.data(), NULL,
                              &copy1 );

    Mat h_intermediate_sbl (countY, countX, CV_32F);
    Mat h_intermediate_nms (countY, countX, CV_32F);
    Mat h_intermediate_strong (countY, countX, CV_32F);
    Mat h_intermediate_weak (countY, countX, CV_32F);

    // Create a kernel object
    string kernel0 = "Sobel";
    string kernel1 = "NonMaximumSuppression";
    string kernel2 = "DoubleThresholding";
    string kernel3 = "Hysteresis";

    cl::Kernel sblKernel ( program, kernel0.c_str() );
    cl::Kernel nmsKernel ( program, kernel1.c_str() );
    cl::Kernel dtKernel ( program, kernel2.c_str() );
    cl::Kernel hKernel ( program, kernel3.c_str() );

    cl::ImageFormat format ( CL_R, CL_FLOAT );
    cl::Image2D bufferSBLtoNMS ( context, CL_MEM_READ_WRITE, format, countX, countY ); //Output for NMS
    cl::Image2D bufferNMStoDT ( context, CL_MEM_READ_WRITE, format, countX, countY ); // Output of the NonMaximumSuppression kernel and input to the DoubleThresholding

    cl::Image2D bufferDT_strong_toH ( context, CL_MEM_READ_WRITE, format, countX, countY ); // Output of the DoubleThresholding kernel -- strong, and input to the Hysteresis
    cl::Image2D bufferDT_weak_toH ( context, CL_MEM_READ_WRITE, format, countX, countY ); // Output of the DoubleThresholding kernel -- weak, and input to the Hysteresis
    // Launch kernel on the device
    cl::Event eventSBL, eventNMS, eventDT, eventH;

    // set SBL Kernel arguments
    sblKernel.setArg<cl::Image2D> ( 0, image2D );
    sblKernel.setArg<cl::Image2D> ( 1, bufferSBLtoNMS );

    // Set NMS Kernel arguments
    nmsKernel.setArg<cl::Image2D> ( 0, bufferSBLtoNMS );
    nmsKernel.setArg<cl::Image2D> ( 1, bufferNMStoDT ); // Output used as input for the next kernel

    // Set DT Kernel arguments
    float magMax = 0.2f;
    float magMin = 0.1f;
    dtKernel.setArg<cl::Image2D> ( 0, bufferNMStoDT ); // Output from the previous kernel
    dtKernel.setArg<cl::Image2D> ( 1, bufferDT_strong_toH ); // Output strong used as input for the next kernel
    dtKernel.setArg<cl::Image2D> ( 2, bufferDT_weak_toH ); // Output weak used as input for the next kernel
    dtKernel.setArg<cl_float> ( 3, magMax );
    dtKernel.setArg<cl_float> ( 4, magMin );

    // Set H Kernel arguments
    hKernel.setArg<cl::Image2D> ( 0, bufferDT_strong_toH );
    hKernel.setArg<cl::Image2D> ( 1, bufferDT_weak_toH );
    hKernel.setArg<cl::Image2D> ( 2, d_output ); // Output from the previous kernel

    // Sobel Kernel ------------------------------------------------------------------------
    queue.enqueueNDRangeKernel ( sblKernel, cl::NullRange,
                                 cl::NDRange ( countX, countY ),
                                 cl::NDRange ( wgSizeX, wgSizeY ), NULL, &eventSBL );
    eventSBL.wait();

    // store Sobel output image back to host
    queue.enqueueReadImage ( bufferSBLtoNMS, true, origin, region,
                             countX * sizeof ( float ), 0, h_intermediate_sbl.data, NULL);
    double minVal, maxVal;
    cv::minMaxLoc(h_intermediate_sbl, &minVal, &maxVal);
    std::cout << "Sobel Min: " << minVal << ", Max: " << maxVal << std::endl;
    cv::Mat displayImg;
    cv::normalize(h_intermediate_sbl, displayImg, 0, 255, cv::NORM_MINMAX);
    displayImg.convertTo(displayImg, CV_8U);

        // Step 3: Display the image using cv::imshow
    cv::imshow("Sobel Intermediate", displayImg);
    cv::waitKey(0); // Wait for a key press

    // Non-Maximum Suppression Kernel -------------------------------------------------------
    queue.enqueueNDRangeKernel ( nmsKernel, cl::NullRange,
                                 cl::NDRange ( countX, countY ),
                                 cl::NDRange ( wgSizeX, wgSizeY ), NULL, &eventNMS );
    eventNMS.wait(); // Wait for the NMS kernel to complete

    // store Non-Maximum Suppression output image back to host
    queue.enqueueReadImage ( bufferNMStoDT, true, origin, region,
                             countX * sizeof ( float ), 0, h_intermediate_nms.data, NULL);
    cv::Mat displayImg1;
    cv::normalize(h_intermediate_nms, displayImg1, 0, 255, cv::NORM_MINMAX);
    displayImg1.convertTo(displayImg1, CV_8U);

        // Step 3: Display the image using cv::imshow
    cv::imshow("NMS Intermediate", displayImg1);
    cv::waitKey(0); // Wait for a key press

    // Double Thresholding Kernel -----------------------------------------------------------
    queue.enqueueNDRangeKernel ( dtKernel, cl::NullRange,
                                 cl::NDRange ( countX, countY ),
                                 cl::NDRange ( wgSizeX, wgSizeY ), NULL, &eventDT );
    eventDT.wait(); // Wait for the Double Thresholding kernel to complete

    // store Strong Image output image back to host
    queue.enqueueReadImage ( bufferDT_strong_toH, true, origin, region,
                             countX * sizeof ( float ), 0, h_intermediate_strong.data, NULL);
    cv::Mat displayImg2;
    cv::normalize(h_intermediate_strong, displayImg2, 0, 255, cv::NORM_MINMAX);
    displayImg2.convertTo(displayImg2, CV_8U);

        // Step 3: Display the image using cv::imshow
    cv::imshow("strong Intermediate", displayImg2);
    cv::waitKey(0); // Wait for a key press

    // store Weak Image output image back to host
    queue.enqueueReadImage ( bufferDT_weak_toH, true, origin, region,
                             countX * sizeof ( float ), 0, h_intermediate_weak.data, NULL);
    cv::Mat displayImg3;
    cv::normalize(h_intermediate_weak, displayImg3, 0, 255, cv::NORM_MINMAX);
    displayImg3.convertTo(displayImg3, CV_8U);

        // Step 3: Display the image using cv::imshow
    cv::imshow("Weak Intermediate", displayImg3);
    cv::waitKey(0); // Wait for a key press

    // Hysteresis Kernel --------------------------------------------------------------------
    queue.enqueueNDRangeKernel ( hKernel, cl::NullRange,
                                 cl::NDRange ( countX, countY ),
                                 cl::NDRange ( wgSizeX, wgSizeY ), NULL, &eventH );

    eventH.wait(); // Wait for the Hysteresis kernel to complete

    // Copy output data back to host
    cl::Event copy2;
    queue.enqueueReadImage ( d_output, true, origin, region,
                             countX * sizeof ( float ), 0, h_outputGpu.data(), NULL, &copy2 );

    // --------------------------------------------------------

    // Print performance data
    Core::TimeSpan cpuTime = cpuEnd - cpuStart;
    Core::TimeSpan gpuTime = OpenCL::getElapsedTime ( eventNMS ) + OpenCL::getElapsedTime ( eventDT ) + OpenCL::getElapsedTime ( eventH );
    Core::TimeSpan copyTime = OpenCL::getElapsedTime ( copy1 ) + OpenCL::getElapsedTime ( copy2 );
    Core::TimeSpan overallGpuTime = gpuTime + copyTime;

    cout << "CPU Time: " << cpuTime.toString() << ", "
         << ( count / cpuTime.getSeconds() / 1e6 ) << " MPixel/s"
         << endl;
    cout << "Memory copy Time: " << copyTime.toString() << endl;
    cout << "GPU Time w/o memory copy: " << gpuTime.toString()
         << " (speedup = " << ( cpuTime.getSeconds() / gpuTime.getSeconds() )
         << ", " << ( count / gpuTime.getSeconds() / 1e6 ) << " MPixel/s)"
         << endl;
    cout << "GPU Time with memory copy: " << overallGpuTime.toString()
         << " (speedup = "
         << ( cpuTime.getSeconds() / overallGpuTime.getSeconds() ) << ", "
         << ( count / overallGpuTime.getSeconds() / 1e6 ) << " MPixel/s)"
         << endl;
    return 0;
    }


Mat NonMaximumSuppression ( const Mat& magnitude, const Mat& blurred, const Mat& angle ) {
    Mat result = magnitude.clone();
    int neighbor1X, neighbor1Y, neighbor2X, neighbor2Y;
    float gradientAngle;

    for ( int x = 0; x < blurred.rows; x++ ) {
        for ( int y = 0; y < blurred.cols; y++ ) {
            gradientAngle = angle.at<float> ( x, y );

            // Normalize angle to be in the range [0, 180)
            gradientAngle = fmodf ( fabs ( gradientAngle ), 180.0f );

            // Determine neighbors based on gradient angle
            if ( gradientAngle <= 22.5f || gradientAngle > 157.5f ) {
                neighbor1X = x - 1;
                neighbor1Y = y;
                neighbor2X = x + 1;
                neighbor2Y = y;
                }
            else if ( gradientAngle <= 67.5f ) {
                neighbor1X = x - 1;
                neighbor1Y = y - 1;
                neighbor2X = x + 1;
                neighbor2Y = y + 1;
                }
            else if ( gradientAngle <= 112.5f ) {
                neighbor1X = x;
                neighbor1Y = y - 1;
                neighbor2X = x;
                neighbor2Y = y + 1;
                }
            else {
                neighbor1X = x - 1;
                neighbor1Y = y + 1;
                neighbor2X = x + 1;
                neighbor2Y = y - 1;
                }

            // Check bounds of neighbor1
            if ( neighbor1X >= 0 && neighbor1X < blurred.rows && neighbor1Y >= 0 && neighbor1Y < blurred.cols ) {
                if ( result.at<float> ( x, y ) < result.at<float> ( neighbor1X, neighbor1Y ) ) {
                    result.at<float> ( x, y ) = 0;
                    continue;
                    }
                }

            // Check bounds of neighbor2
            if ( neighbor2X >= 0 && neighbor2X < blurred.rows && neighbor2Y >= 0 && neighbor2Y < blurred.cols ) {
                if ( result.at<float> ( x, y ) < result.at<float> ( neighbor2X, neighbor2Y ) ) {
                    result.at<float> ( x, y ) = 0;
                    continue;
                    }
                }
            }
        }
    return result;
    }

void DoubleThresholding ( const Mat& magnitude, Mat& strong, Mat& weak ) {
    // apply double thresholding
    float magMax = 0.2, magMin = 0.1;
    float gradientMagnitude;
    for ( int x = 0; x < magnitude.rows; x++ ) {
        for ( int y = 0; y < magnitude.cols; y++ ) {
            gradientMagnitude = magnitude.at<float> ( x, y );

            if ( gradientMagnitude > magMax ) {
                strong.at<float> ( x, y ) = gradientMagnitude;
                }
            else if ( gradientMagnitude <= magMax && gradientMagnitude > magMin ) {
                weak.at<float> ( x, y ) = gradientMagnitude;
                };
            }
        }
    }

Mat Hysteresis ( Mat& strong, const Mat& weak ) {
    // imshow ( "strong_test", strong );
    // imshow ( "weak_test", weak );
    for ( int x = 0; x < strong.rows; x++ ) {
        for ( int y = 0; y < strong.cols; y++ ) {
            if ( weak.at<float> ( x, y ) != 0 ) {
                if ( ( x + 1 < strong.rows && strong.at<float> ( x + 1, y ) != 0 ) ||
                        ( x - 1 >= 0 && strong.at<float> ( x - 1, y ) != 0 ) ||
                        ( y + 1 < strong.cols && strong.at<float> ( x, y + 1 ) ) != 0 ||
                        ( y - 1 >= 0 && strong.at<float> ( x, y - 1 ) != 0 ) ||
                        ( x - 1 >= 0 && y - 1 >= 0 && strong.at<float> ( x - 1, y - 1 ) != 0 ) ||
                        ( x + 1 < strong.rows && y - 1 >= 0 && strong.at<float> ( x + 1, y - 1 ) != 0 ) ||
                        ( x - 1 >= 0 && y + 1 < strong.cols && strong.at<float> ( x - 1, y + 1 ) != 0 ) ||
                        ( x + 1 < strong.rows && y + 1 < strong.cols && strong.at<float> ( x + 1, y + 1 ) != 0 ) ) {
                    strong.at<float> ( x, y ) = weak.at<float> ( x, y );
                    }
                }
            }
        }
    return strong;
    }

int cpuFunction ( const Mat& blurred ) {
    // Apply Gaussian filter
    // Mat blurred = GaussianFilter ( src );
    // imshow ( "blurred", blurred );

    // Compute image gradient
    Mat xComponent, yComponent;
    Sobel ( blurred, xComponent, CV_32F, 1, 0, 3 );
    Sobel ( blurred, yComponent, CV_32F, 0, 1, 3 );

    // Convert to polar coordinates
    Mat magnitude, angle;
    cartToPolar ( xComponent, yComponent, magnitude, angle, true );

    // Normalize values
    normalize ( magnitude, magnitude, 0, 1, NORM_MINMAX );

    imshow("sobel", blurred);
    // Apply non-maximum suppression
    Mat suppressed = NonMaximumSuppression ( magnitude, blurred, angle );
    imshow ( "non-max suppression", suppressed );

    // Apply double thresholding
    Mat strong = Mat::zeros ( magnitude.rows, magnitude.cols, CV_32F );
    Mat weak = Mat::zeros ( magnitude.rows, magnitude.cols, CV_32F );
    DoubleThresholding ( suppressed, strong, weak );
    imshow ( "strong", strong );
    imshow ( "Weak", weak );

    // Apply hysteresis
    Mat finalEdges = Hysteresis ( strong, weak );
    imshow ( "final edge detection", finalEdges );

    // waitKey ( 0 );
    return 0;
    }

// Function to create cl::Image2D from cv::Mat
cl::Image2D createImage2DFromMat ( const cl::Context& context, const cv::Mat& mat, bool readOnly ) {
    cl_int err;
    cl::ImageFormat format;

    // Example format - adjust based on your cv::Mat type
    format = cl::ImageFormat ( CL_RGBA, CL_UNSIGNED_INT8 );

    // Ensure mat data is continuous and in a suitable format
    cv::Mat continuousMat = mat.clone(); // Simplified; consider adjusting format as needed

    cl_mem_flags flags = readOnly ? CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR : CL_MEM_WRITE_ONLY | CL_MEM_COPY_HOST_PTR;

    // Create cl::Image2D
    cl::Image2D image ( context, flags, format, mat.cols, mat.rows, 0, continuousMat.data, &err );
    if ( err != CL_SUCCESS ) {
        throw std::runtime_error ( "Failed to create cl::Image2D from cv::Mat" );
        }

    return image;
    }

