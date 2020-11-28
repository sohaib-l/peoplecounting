// f001_carlosruz_pc_v2.cpp : Ce fichier contient la fonction 'main'. L'exécution du programme commence et se termine à cet endroit.
//

#define _CRT_SECURE_NO_WARNINGS
// 3rdparty dependencies
// GFlags: DEFINE_bool, _int32, _int64, _uint64, _double, _string
#include <gflags/gflags.h>
// Allow Google Flags in Ubuntu 14
#ifndef GFLAGS_GFLAGS_H_
namespace gflags = google;
#endif
// OpenPose dependencies
#include <openpose/core/headers.hpp>
#include <openpose/filestream/headers.hpp>
#include <openpose/gui/headers.hpp>
#include <openpose/pose/headers.hpp>
#include <openpose/utilities/headers.hpp>

#include <iostream>
#include <fstream>
#include <chrono>
#include <direct.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <windows.h>
#include <time.h>

// See all the available parameter options withe the `--help` flag. E.g. `build/examples/openpose/openpose.bin --help`
// Note: This command will show you flags for other unnecessary 3rdparty files. Check only the flags for the OpenPose
// executable. E.g. for `openpose.bin`, look for `Flags from examples/openpose/openpose.cpp:`.
// Debugging/Other
DEFINE_int32(logging_level, 3, "The logging level. Integer in the range [0, 255]. 0 will output any log() message, while"
	" 255 will not output any. Current OpenPose library messages are in the range 0-4: 1 for"
	" low priority messages and 4 for important ones.");
// OpenPose
DEFINE_string(model_pose, "COCO", "Model to be used. E.g. `COCO` (18 keypoints), `MPI` (15 keypoints, ~10% faster), "
	"`MPI_4_layers` (15 keypoints, even faster but less accurate).");
DEFINE_string(model_folder, "../dependencies/models/openpose/", "Folder path (absolute or relative) where the models (pose, face, ...) are located.");
DEFINE_string(net_resolution, "-1x368", "Multiples of 16. If it is increased, the accuracy potentially increases. If it is"
	" decreased, the speed increases. For maximum speed-accuracy balance, it should keep the"
	" closest aspect ratio possible to the images or videos to be processed. Using `-1` in"
	" any of the dimensions, OP will choose the optimal aspect ratio depending on the user's"
	" input value. E.g. the default `-1x368` is equivalent to `656x368` in 16:9 resolutions,"
	" e.g. full HD (1980x1080) and HD (1280x720) resolutions.");
DEFINE_string(output_resolution, "-1x-1", "The image resolution (display and output). Use \"-1x-1\" to force the program to use the"
	" input image resolution.");
DEFINE_int32(num_gpu_start, 0, "GPU device start number.");
DEFINE_double(scale_gap, 0.3, "Scale gap between scales. No effect unless scale_number > 1. Initial scale is always 1."
	" If you want to change the initial scale, you actually want to multiply the"
	" `net_resolution` by your desired initial scale.");
DEFINE_int32(scale_number, 1, "Number of scales to average.");
// OpenPose Rendering
DEFINE_bool(disable_blending, false, "If enabled, it will render the results (keypoint skeletons or heatmaps) on a black"
	" background, instead of being rendered into the original image. Related: `part_to_show`,"
	" `alpha_pose`, and `alpha_pose`.");
DEFINE_double(render_threshold, 0.3, "Only estimated keypoints whose score confidences are higher than this threshold will be"
	" rendered. Generally, a high threshold (> 0.5) will only render very clear body parts;"
	" while small thresholds (~0.1) will also output guessed and occluded keypoints, but also"
	" more false positives (i.e. wrong detections).");
DEFINE_double(alpha_pose, 0.6, "Blending factor (range 0-1) for the body part rendering. 1 will show it completely, 0 will"
	" hide it. Only valid for GPU rendering.");

DEFINE_string(video, "../data/WalkingOfficePeople.mp4", "video path (absolute or relative).");

DEFINE_bool(ip_camera, false, "If enabled, it uses the ip camera. The address needs to be set using the flag --ip_camera_address http://***.***.***.***/mjpg/video.mjpg");

DEFINE_bool(web_cam, false, "If enabled, it uses the webcam.");

DEFINE_string(ip_camera_address, "http://192.168.2.50/mjpg/video.mjpg", "link of the ip camera.");

DEFINE_int32(refresh, 300, "Number of frames on which counting is made.");


using namespace std;
using namespace std::chrono;
using namespace cv;
int framerate;
bool ip_camera;
bool web_cam;
bool quit;

cv::Mat inputImage;
cv::Mat outputImage;

op::Array<float> poseKeypoints;

struct tm * now;
high_resolution_clock::time_point dur;
int countppl;

void save(string filename);

string getFileName(const string& s);
int framen;

int main(int argc, char *argv[])
{
	// Parsing command line flags
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	quit = false;
	ip_camera = false;
	web_cam = false;
	framerate = 0;
	inputImage = cv::Mat();
	outputImage = cv::Mat();

	countppl = 0;
	framen = 0;

	time_t t = time(0);   // get time now
	now = localtime(&t);
	dur = high_resolution_clock::now();

	cv::VideoCapture cap;

	if (ip_camera) {

		const std::string videoStreamAddress = FLAGS_ip_camera_address;

		if (!cap.open(videoStreamAddress)) {

			std::cout << "Error opening video stream" << std::endl;
			quit = true;
		}
	}
	else if (web_cam) {

		cap = cv::VideoCapture(0);
		if (!cap.isOpened()) {
			cout << "Error reading webcam!" << endl;
			quit = true;
		}
	}
	else {
		cap = cv::VideoCapture(FLAGS_video);
		if (!cap.isOpened()) {
			cout << "video not found!!" << endl;
			quit = true;
		}
	}

	op::log("Starting People Counting demo...", op::Priority::High);

	// ------------------------- INITIALIZATION -------------------------
		// Step 1 - Set logging level
		// - 0 will output all the logging messages
		// - 255 will output nothing
	op::check(0 <= FLAGS_logging_level && FLAGS_logging_level <= 255, "Wrong logging_level value.",
		__LINE__, __FUNCTION__, __FILE__);
	op::ConfigureLog::setPriorityThreshold((op::Priority)FLAGS_logging_level);
	op::log("", op::Priority::Low, __LINE__, __FUNCTION__, __FILE__);

	// Step 2 - Read Google flags (user defined configuration)
	// outputSize
	const auto outputSize = op::flagsToPoint(FLAGS_output_resolution, "-1x-1");
	// netInputSize
	const auto netInputSize = op::flagsToPoint(FLAGS_net_resolution, "-1x368");
	// poseModel
	const auto poseModel = op::flagsToPoseModel(FLAGS_model_pose);
	// Check no contradictory flags enabled
	if (FLAGS_alpha_pose < 0. || FLAGS_alpha_pose > 1.)
		op::error("Alpha value for blending must be in the range [0,1].", __LINE__, __FUNCTION__, __FILE__);
	if (FLAGS_scale_gap <= 0. && FLAGS_scale_number > 1)
		op::error("Incompatible flag configuration: scale_gap must be greater than 0 or scale_number = 1.",
			__LINE__, __FUNCTION__, __FILE__);
	// Logging
	op::log("", op::Priority::Low, __LINE__, __FUNCTION__, __FILE__);
	// Step 3 - Initialize all required classes
	op::ScaleAndSizeExtractor scaleAndSizeExtractor(netInputSize, outputSize, FLAGS_scale_number, FLAGS_scale_gap);
	op::CvMatToOpInput cvMatToOpInput;
	op::CvMatToOpOutput cvMatToOpOutput;
	op::PoseExtractorCaffe poseExtractorCaffe{ poseModel, FLAGS_model_folder, FLAGS_num_gpu_start };
	op::PoseCpuRenderer poseRenderer{ poseModel, (float)FLAGS_render_threshold, !FLAGS_disable_blending,
		(float)FLAGS_alpha_pose };
	op::OpOutputToCvMat opOutputToCvMat;
	// Step 4 - Initialize resources on desired thread (in this case single thread, i.e. we init resources here)
	poseExtractorCaffe.initializationOnThread();
	poseRenderer.initializationOnThread();

	// ------------------------- POSE ESTIMATION AND RENDERING -------------------------
	int nof = 0;
	high_resolution_clock::time_point t1;
	while (!quit)
	{
		cap.read(inputImage);
		if (nof == 0) t1 = high_resolution_clock::now();
		nof++;

		if (inputImage.empty()) {

			quit = true;
			continue;
		} // end of video stream
		framen++;
		const op::Point<int> imageSize{ inputImage.cols, inputImage.rows };
		// Step 2 - Get desired scale sizes
		std::vector<double> scaleInputToNetInputs;
		std::vector<op::Point<int>> netInputSizes;
		double scaleInputToOutput;
		op::Point<int> outputResolution;
		std::tie(scaleInputToNetInputs, netInputSizes, scaleInputToOutput, outputResolution)
			= scaleAndSizeExtractor.extract(imageSize);
		// Step 3 - Format input image to OpenPose input and output formats
		const auto netInputArray = cvMatToOpInput.createArray(inputImage, scaleInputToNetInputs, netInputSizes);
		auto outputArray = cvMatToOpOutput.createArray(inputImage, scaleInputToOutput, outputResolution);
		// Step 4 - Estimate poseKeypoints
		poseExtractorCaffe.forwardPass(netInputArray, imageSize, scaleInputToNetInputs);
		//const auto poseKeypoints = poseExtractorCaffe.getPoseKeypoints();
		poseKeypoints = poseExtractorCaffe.getPoseKeypoints();
		// Step 5 - Render poseKeypoints
		poseRenderer.renderPose(outputArray, poseKeypoints, scaleInputToOutput);
		// Step 6 - OpenPose output format to cv::Mat
		outputImage = opOutputToCvMat.formatToCvMat(outputArray);
		// ------------------------- SHOWING RESULT AND CLOSING -------------------------
			// Measuring framerate
		high_resolution_clock::time_point t2 = high_resolution_clock::now();
		duration<double, std::milli> time_span = t2 - t1;
		if (time_span.count() >= 1000) {

			framerate = nof;
			nof = 0;
		}

		if ((framen - 1) % FLAGS_refresh == 0) {
			countppl += poseKeypoints.getSize(0);

		}

		if (!outputImage.empty()) {

			cv::putText(outputImage, "FPS: " + to_string(framerate), cv::Point(10, 20), cv::FONT_HERSHEY_PLAIN, 1,
				cv::Scalar(255, 255, 255), 1);

			cv::putText(outputImage, "Count: " + to_string(countppl), cv::Point(10, 50), cv::FONT_HERSHEY_DUPLEX, 1,
				cv::Scalar(0, 0, 255), 1);

			cv::putText(outputImage, "NOF: " + to_string(framen), cv::Point(10, 80), cv::FONT_HERSHEY_PLAIN, 1,
				cv::Scalar(255, 0, 0), 1);

			cv::imshow("OpenPose Tracking", outputImage);
			cv::waitKey(1);
		}
	}
	save(FLAGS_video);
}

void save(string filename) {
	ostringstream stringStream;
	stringStream << now->tm_year + 1900
		<< now->tm_mon + 1
		<< now->tm_mday
		<< "_"
		<< now->tm_hour
		<< now->tm_min
		<< now->tm_sec;

	high_resolution_clock::time_point dur2 = high_resolution_clock::now();
	duration<double, std::milli> dur_span = dur2 - dur;

	std::string filepath = "../results/" + stringStream.str() + "_" + getFileName(filename) + ".txt";
	cout << "Saving to: " << filepath << endl;

	ofstream out(filepath);
	out << "Results of analysis of the file: " + filename + "\n"
		<< "____________________________________________________\n"
		<< "The program is based on the model: " + FLAGS_model_pose + ". (available models: COCO).\n"
		<< "Length of the video: " + to_string(framen) << " frames.\n"
		<< "Time of analysis: " + to_string(dur_span.count() / 1000) + " seconds.\n"
		<< "Updating the counter every: " + to_string(FLAGS_refresh) + " frames (~" + to_string((int)(FLAGS_refresh / 30)) + "seconds).\n"
		<< "\n"
		<< "Approximate number of detected people: " << to_string(countppl) + "\n";
	out.close();
}

string getFileName(const string& s) {

	char sep = '/';
	char sep2 = '\\';

	size_t i = s.rfind(sep, s.length());
	size_t i2 = s.rfind(sep2, s.length());

	if (i2 < 5000)
		if (i2 > i || i > 5000) i = i2;

	if (i != string::npos) {
		return(s.substr(i + 1, s.length() - i));
	}

	return("");
}