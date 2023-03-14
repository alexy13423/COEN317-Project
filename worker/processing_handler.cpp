#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cstring>
#include <chrono>
//#include <ctime>
#include <filesystem>
#include <pqxx/pqxx>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

#include <unistd.h>
#include <sys/poll.h>
#include <sys/wait.h>
#include <time.h>

#include "processing_handler.h"

#include "FTPClient.h"

#define NUM_DLIB_FACIAL_COORDINATES 68

static void draw_point (cv::Mat &, cv::Point2f, cv::Scalar);
static void draw_delaunay (cv::Mat &, cv::Subdiv2D &, cv::Scalar);
void draw_face_mesh (char *, char *, int *, int *);

ProcessHandler::ProcessHandler(std::vector<int> process_thread_fds, int processing_socket_fd) {
    this->process_thread_fds = process_thread_fds;
    this->processing_socket_fd = processing_socket_fd;
	this->client_id = -1;
}

//ffprobe -hide_banner -select_streams v -show_entries stream=width,height,r_frame_rate,duration -of default=noprint_wrappers=1
//ffmpeg -i Big_Buck_Bunny_Trailer_1080p.ogx frame%04d.png
//ffmpeg -hide_banner -r 30 -s 1920x1080 -i frame%04d.png -pix_fmt yuv420p out/out.mp4
void ProcessHandler::processThread() {
	std::cout << "Process handler started.." << std::endl;

    embeddedmz::CFTPClient FTPClient([](const std::string& strLogMsg){ std::cout << strLogMsg << std::endl; });
	FTPClient.InitSession("127.0.0.1", 21, "PuppyApp", "Aailf13423");

	std::string connectionString = "postgresql://postgres:Aailf13423@localhost/puppyswarm";
	pqxx::connection dbConnection{connectionString};

	char buffer[1024] = { 0 };
	int readLength = 0;

	struct pollfd pfds[process_thread_fds.size()];
	int status = 0;

	while(true) {
		for (int i = 0; i < process_thread_fds.size(); i++) {
			pfds[i].fd = process_thread_fds.at(i);
			pfds[i].events = POLLIN;
		}
		std::cout << "Processing thread polling..." << std::endl;
		status = poll(pfds, process_thread_fds.size(), -1);
		if (status < 0) {
			std::cerr << "Error with poll (processing)." << std::endl;
			break;
		}
		if (status == 0) {
			std::cerr << "Poll returned 0 events? (processing)" << std::endl;
			break;
		}
		if (pfds[0].revents == POLLIN) {
			readLength = read(pfds[0].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from main pipe." << std::endl;
			} else {
				std::string pipeValue(buffer);
				std::cout << "Message output: " << pipeValue << std::endl;
				if (pipeValue == "exit") {
					std::cout << "Terminating processing thread." << std::endl;
					break;
				}
			}
			memset(buffer, 0, 1024);
		}
		if (pfds[1].revents == POLLIN) {
			readLength = read(pfds[1].fd, buffer, 1024);
			if (readLength < 0) {
				std::cerr << "Error reading from socket to processing pipe." << std::endl;
			} else {
				std::string pipeValue(buffer);
				std::cout << "Processing handler message from server: " << pipeValue << std::endl;
				memset(buffer, 0, 1024);

				std::vector<std::string> tokens;
				std::stringstream check1(pipeValue);
				check1 << pipeValue;
				std::string intermediate;
				while (getline(check1, intermediate, ' ')) {
					tokens.push_back(intermediate);
				}

				std::stringstream job_accept_stream, job_complete_stream;
				job_accept_stream << "jobaccept " << pipeValue << " " << client_id;
				job_complete_stream << "jobcomplete " << pipeValue << " " << client_id;

				std::string job_accept = job_accept_stream.str();
				std::string job_complete = job_complete_stream.str();

				std::string operation = tokens[0];
				int jobId, frameId;
				if (operation == "clientid") {
					client_id = std::stoi(tokens[1]);
				}

				else if (operation == "preprocessing") {
					jobId = std::stoi(tokens[1]);
					std::string input_filename = tokens[2];

					write(processing_socket_fd, job_accept.data(), job_accept.length());
					handlePreprocessing(&FTPClient, jobId, input_filename, &dbConnection);
					write(processing_socket_fd, job_complete.data(), job_complete.length());
				}
				else if (operation == "coordinate") {
					jobId = std::stoi(tokens[1]);
					frameId = std::stoi(tokens[2]);

					write(processing_socket_fd, job_accept.data(), job_accept.length());
					handleCoordinateDetection(&FTPClient, jobId, frameId,  &dbConnection, client_id);
					write(processing_socket_fd, job_complete.data(), job_complete.length());
				} else if (operation == "drawing") {
					jobId = std::stoi(tokens[1]);
					frameId = std::stoi(tokens[2]);

					write(processing_socket_fd, job_accept.data(), job_accept.length());
					handleDrawing(&FTPClient, jobId, frameId, &dbConnection, client_id);
					write(processing_socket_fd, job_complete.data(), job_complete.length());
				} else if (operation == "postprocessing") {
					jobId = std::stoi(tokens[1]);

					write(processing_socket_fd, job_accept.data(), job_accept.length());
					handlePostprocessing(&FTPClient, jobId, &dbConnection);
					write(processing_socket_fd, job_complete.data(), job_complete.length());
				} else {
					std::string server_error = "error invalidjob";
					write(processing_socket_fd, server_error.data(), server_error.length());
				}

				//int jobId = 1;
				//int frameNumber = 1;
				//std::string input_video_filename = pipeValue;

				//handlePreprocessing(&FTPClient, jobId, input_video_filename, &dbConnection);
				/*
				for (int i = 1; i <= 217; i++) {
					handleCoordinateDetection(&FTPClient, jobId, i, &dbConnection);
					handleDrawing(&FTPClient, jobId, i, &dbConnection);
				}
				*/
				//handlePostprocessing(&FTPClient, jobId, &dbConnection);
			}
		}
	}
}

int handlePreprocessing(embeddedmz::CFTPClient *ftp_client, int jobId, std::string videoName, pqxx::connection *db_connection) {
	
	std::string video_filepath = getVideo(ftp_client, jobId, videoName);
	pqxx::work txn(*db_connection);
	int external_process_fds[2];
	int pipe_status = pipe(external_process_fds);
	int status;
	if (pipe_status < 0) {
		std::cerr << "Process handler pipe failed." << std::endl;
		//return;
		exit(1);
	}

	int fork_status = fork();
	switch (fork_status) {
		case -1: {
			std::cerr << "Processing thread fork error." << std::endl;
			//return;
			exit(1);
		}
		case 0: {
			//child processing here
			dup2(external_process_fds[1], 1);
			close (external_process_fds[0]);
			execl ("/usr/bin/ffprobe", "-hide_banner", "-v", "quiet", "-select_streams", "v", "-show_entries", "stream=width,height,r_frame_rate,duration",
						"-of", "default=noprint_wrappers=1", video_filepath.c_str(), (char *) NULL);
			exit(1); //this should not execute, because exec
		}
		default: {
			waitpid(fork_status, &status, 0);
			close (external_process_fds[1]);
			FILE *external_fd_file = fdopen(external_process_fds[0], "r");
			if (external_fd_file == NULL) {
				std::cerr << "Processing thread external fd fdopen failure." << std::endl;
				//return;
				exit(1);
			}
			char *line = NULL;
			size_t len = 0;
			int bytes_read = getline(&line, &len, external_fd_file);
			int width, height, framerate;
			float duration;
			while (bytes_read > 0) {
				std::string external_string(line);

				std::string::size_type token_boundary = external_string.find("=");
				std::string key = external_string.substr(0, token_boundary);
				std::string value = external_string.substr(token_boundary + 1);
				std::cout << "Key: " << key << " Value: " << value << std::endl;
				bytes_read = getline(&line, &len, external_fd_file);
				if (key == "width") {
					width = std::stoi(value);
				} else if (key == "height") {
					height = std::stoi(value);
				} else if (key == "r_frame_rate") {
					int numerator, denominator;
					std::string::size_type fraction_boundary = value.find("/");
					numerator = std::stoi(value.substr(0, fraction_boundary));
					denominator = std::stoi(value.substr(fraction_boundary + 1));
					framerate = numerator / denominator;
				} else if (key == "duration") {
					duration = std::stof(value);
				}
			}
			txn.exec0(
				"update video set width = " + pqxx::to_string(width) +
				", height = " + pqxx::to_string(height) +
				", framerate = " + pqxx::to_string(framerate) +
				", duration = " + pqxx::to_string(duration) +
				" where videoid = " + pqxx::to_string(jobId));
			//txn.commit();
			std::cout << "Processing of thread output complete!" << std::endl;
		}
	}
	close (external_process_fds[0]);

	//processing video frames now, go again

	std::stringstream unprocessedDirectoryStream;
	unprocessedDirectoryStream << "./workspace/" << jobId << "/unprocessed";
	std::string unprocessedDirectoryString = unprocessedDirectoryStream.str();
	std::filesystem::create_directories(unprocessedDirectoryString);
                
	int second_fork_status = fork();
	switch (second_fork_status) {
		case -1: {
			std::cerr << "Processing thread fork error." << std::endl;
			exit(1);
		}
		case 0: {
			//child processing here
			//don't need output processing here? so ignore all that
			std::stringstream framePath;
			framePath << "./workspace/" << jobId << "/unprocessed/frame_%04d.png";
			std::string framePathString = framePath.str();
			std::cout << "Splitting video file: " << framePathString << std::endl;
			execl ("/usr/bin/ffmpeg", "-hide_banner", "-v", "quiet", "-i", video_filepath.c_str(), framePathString.c_str(), (char *) NULL);
			exit(1); //this should not execute, because exec
		}
		default: {
			waitpid(second_fork_status, &status, 0);
			std::cout << "Video splitting complete!" << std::endl;
		}
	}

	int maxFrame = uploadFrameSet(ftp_client, jobId);
	std::cout << "Max frame detected: " << maxFrame << std::endl;

	for (int i = 1; i <= maxFrame; i++) {
		txn.exec0("insert into frame (videoid, frameid) values (" + pqxx::to_string(jobId) + ", " + pqxx::to_string(i) + ")");
	}

	txn.exec0("update video set framecount = " + pqxx::to_string(maxFrame) + " where videoid = " + pqxx::to_string(jobId));
	txn.commit();
	return 0;
}

int handleCoordinateDetection(embeddedmz::CFTPClient *ftp_client, int jobId, int frameNumber, pqxx::connection *db_connection, int client_id) {
	int job_start_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()
	).count();
	std::cout << "Starting facial detection for frame " << frameNumber << " of job " << jobId << std::endl;
	std::string frame_filepath = getFrame(ftp_client, jobId, frameNumber);

	int external_process_fds[2];
	int pipe_status = pipe(external_process_fds);
	int status;
	if (pipe_status < 0) {
		std::cerr << "Process handler pipe failed." << std::endl;
		//return;
		exit(1);
	}

	int fork_status = fork();
	switch (fork_status) {
		case -1: {
			std::cerr << "Processing thread fork error." << std::endl;
			//return;
			exit(1);
		}
		case 0: {
			std::cout << "Frame to perform detection on: " << frame_filepath << std::endl;
			//child processing here
			dup2(external_process_fds[1], 1);
			close (external_process_fds[0]);
			std::stringstream framePath;
			execl ("./face_landmark_detection", "./face_landmark_detection", frame_filepath.c_str(), (char *) NULL);
			exit(1); //this should not execute, because exec
		}
		default: {
			waitpid(fork_status, &status, 0);
			close (external_process_fds[1]);
			FILE *external_fd_file = fdopen(external_process_fds[0], "r");
			pqxx::work txn(*db_connection);
			if (external_fd_file == NULL) {
				std::cerr << "Processing thread external fd fdopen failure." << std::endl;
				//return;
				exit(1);
			}
			char *line = NULL;
			size_t len = 0;
			int bytes_read = getline(&line, &len, external_fd_file);
			//for facial detection, skip the first line
			bytes_read = getline(&line, &len, external_fd_file);
			int coordinate_index = 1;
			while (bytes_read > 0) {
				std::string external_string(line);
				//std::cout << "Processing thread output line: " << external_string << std::endl;

				std::string::size_type coordinate_boundary = external_string.find(", ");
				std::string firstCoordinate = external_string.substr(0, coordinate_boundary);
				std::string secondCoordinate = external_string.substr(coordinate_boundary + 2);
				int firstCoordinateValue = std::stoi(firstCoordinate);
				int secondCoordinateValue = std::stoi(secondCoordinate);
				//std::cout << "First coordinate: " << firstCoordinateValue << " Second coordinate: " << secondCoordinateValue << std::endl;

				txn.exec0("insert into coordinate values (" + pqxx::to_string(jobId) + ", " + pqxx::to_string(frameNumber)
							+ ", " + pqxx::to_string(coordinate_index) + ", " + pqxx::to_string(firstCoordinateValue) 
							+ ", " + pqxx::to_string(secondCoordinateValue) + ")");

				bytes_read = getline(&line, &len, external_fd_file);
				coordinate_index++;
			}
			int job_end_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
			).count();
			int job_timer = job_end_timestamp - job_start_timestamp;
			std::cout << "Job completion time: " << job_timer << std::endl;
			txn.exec0("update frame set firstjob = " + pqxx::to_string(job_timer) + ", firstjobid = " + pqxx::to_string(client_id) +
						" where videoid = " + pqxx::to_string(jobId) + " and frameid = " + pqxx::to_string(frameNumber));
			txn.commit();
			std::cout << "Processing of thread output complete!" << std::endl;
		}
	}
	return 0;
}

int handleDrawing(embeddedmz::CFTPClient *ftp_client, int jobId, int frameNumber, pqxx::connection *db_connection, int client_id) {
	int job_start_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()
	).count();
	std::cout << "Starting drawing for frame " << frameNumber << " of job " << jobId << std::endl;
	std::string frame_filepath = getFrame(ftp_client, jobId, frameNumber);

	int x_coordinates[NUM_DLIB_FACIAL_COORDINATES], y_coordinates[NUM_DLIB_FACIAL_COORDINATES];

	pqxx::work txn(*db_connection);
	pqxx::result coordinate_query_result = txn.exec("select x,y from coordinate where videoid = " + pqxx::to_string(jobId)
													+ " and frameid = " + pqxx::to_string(frameNumber) + " order by coordinateid");
	int currentIndex = 0;
	for (auto const &row: coordinate_query_result) {
		int firstItem = atoi(row[0].c_str());
		int secondItem = atoi(row[1].c_str());

		//std::cout << "Row " << currentIndex << ": " << firstItem << " " << secondItem << std::endl;
		x_coordinates[currentIndex] = firstItem;
		y_coordinates[currentIndex] = secondItem;
		currentIndex++;
	}

	std::stringstream destinationFilepath;
	destinationFilepath << "./workspace/" << jobId << "/processed";
	std::string destinationDirectory = destinationFilepath.str();
	std::filesystem::create_directories(destinationDirectory);
	destinationFilepath << "/frame_";

	std::string frame = std::to_string(frameNumber);
	int frameNumberLength = frame.size();
	int paddingZeroCount = 4 - frameNumberLength;
	if (paddingZeroCount > 0) {
		frame.insert(0, paddingZeroCount, '0');
	}

	destinationFilepath << frame << ".png";
	std::string destinationFilepathString = destinationFilepath.str();

	draw_face_mesh(frame_filepath.data(), destinationFilepathString.data(), &x_coordinates[0], &y_coordinates[0]);

	uploadFrame(ftp_client, jobId, frameNumber);

	int job_end_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()
	).count();
	int job_timer = job_end_timestamp - job_start_timestamp;
	std::cout << "Job completion time: " << job_timer << std::endl;
	txn.exec0("update frame set secondjob = " + pqxx::to_string(job_timer) + ", secondjobid = " + pqxx::to_string(client_id) +
		" where videoid = " + pqxx::to_string(jobId) + " and frameid = " + pqxx::to_string(frameNumber));
	txn.commit();
	std::cout << "Drawing for frame " << frameNumber << " of job " << jobId << " complete!" << std::endl;
	return 0;
}

// ***********************************************************************************
// * draw_point is copied from the following source code:                          
// *                                                                               
// * https://learnopencv.com/delaunay-triangulation-and-voronoi-diagram-using-opencv-c-python/
// ***********************************************************************************
static void draw_point (cv::Mat &img, cv::Point2f fp, cv::Scalar color)
{
	circle (img, fp, 2, color, cv::FILLED, cv::LINE_8, 0);
}

// ***********************************************************************************
// * draw_delaunay is copied from the following source code:                       
// *                                                                               
// * https://learnopencv.com/delaunay-triangulation-and-voronoi-diagram-using-opencv-c-python/
// ***********************************************************************************
static void draw_delaunay (cv::Mat &img, cv::Subdiv2D &subdiv, cv::Scalar delaunay_color)
{
	std::vector<cv::Vec6f> triangleList;
	subdiv.getTriangleList(triangleList);
	std::vector<cv::Point> pt(3);
	cv::Size size = img.size();
	cv::Rect rect(0,0, size.width, size.height);
	for (size_t i = 0; i < triangleList.size(); i++)
	{
		cv::Vec6f t = triangleList[i];
		pt[0] = cv::Point(cvRound(t[0]), cvRound(t[1]));
		pt[1] = cv::Point(cvRound(t[2]), cvRound(t[3]));
		pt[2] = cv::Point(cvRound(t[4]), cvRound(t[5]));
		if (rect.contains(pt[0]) && rect.contains(pt[1]) && rect.contains(pt[2]))
		{
			line(img, pt[0], pt[1], delaunay_color, 1, cv::LINE_AA, 0);
			line(img, pt[1], pt[2], delaunay_color, 1, cv::LINE_AA, 0);
			line(img, pt[2], pt[0], delaunay_color, 1, cv::LINE_AA, 0);
		}
	}
}

// ***********************************************************************************
// * Draw_face_mesh is a derivative work from the following source code:           
// *                                                                               
// * https://learnopencv.com/delaunay-triangulation-and-voronoi-diagram-using-opencv-c-python/
// ***********************************************************************************
void draw_face_mesh (char *input_filename, char *output_filename, int 
	*x_coordinates, int *y_coordinates)
{
	cv::Scalar delaunay_color (255,255,255), points_color (0, 0, 255);
	int  i;
	cv::Mat img;
	std::vector<cv::Point2f> points;
	if ((input_filename != NULL) && (output_filename != NULL))
	{
		img = cv::imread (input_filename);
		cv::Size size = img.size();
		cv::Rect rect (0, 0, size.width, size.height);
		cv::Subdiv2D subdiv(rect);
		for (i = 0; i < NUM_DLIB_FACIAL_COORDINATES; i++)
		{
			points.push_back (cv::Point2f((float) x_coordinates[i], (float) 
				y_coordinates[i]));
		}
		for (std::vector<cv::Point2f>::iterator it = points.begin(); it != points.end(); it++)
		{
			subdiv.insert(*it);
		}
		draw_delaunay (img, subdiv, delaunay_color);
		for (std::vector<cv::Point2f>::iterator it = points.begin(); it != points.end(); it++)
		{
			draw_point (img, *it, points_color);
		}
		imwrite (output_filename, img); 
	}
}

int handlePostprocessing(embeddedmz::CFTPClient *ftp_client, int jobId, pqxx::connection *db_connection) {
	//ffmpeg -hide_banner -r 30 -s 1920x1080 -i frame%04d.png -pix_fmt yuv420p ./workspace/1/out/out.mp4
	std::cout << "Starting final reassembly for job " << jobId << std::endl;
	pqxx::work txn(*db_connection);
	pqxx::row videoQueryRow = txn.exec1("select framerate,width,height,framecount from video where videoid = " + pqxx::to_string(jobId));
	const char *framerate = videoQueryRow[0].c_str();
	int width = atoi(videoQueryRow[1].c_str());
	int height = atoi(videoQueryRow[2].c_str());
	int frameCount = atoi(videoQueryRow[3].c_str());
	
	getFrameSet(ftp_client, jobId, frameCount);
	
	std::stringstream videoOutputPath;
	videoOutputPath << "./workspace/" << jobId << "/output";
	std::string destinationDirectory = videoOutputPath.str();
	std::filesystem::create_directories(destinationDirectory);
	videoOutputPath << "/output.mp4";
	std::string videoOutputPathString = videoOutputPath.str();
	int status;
	int fork_status = fork();
	switch(fork_status) {
		case -1: {
		}
		case 0: {
			std::cout << "Outputting video to: " << videoOutputPathString << std::endl;
			std::stringstream resolutionStream;
			resolutionStream << width << "x" << height;
			std::string resolutionString = resolutionStream.str();
			std::cout << "Video target framerate: " << framerate << std::endl;
			std::cout << "Video target resolution: " << resolutionString << std::endl;
			std::stringstream frameLocationPath;
			frameLocationPath << "./workspace/" << jobId << "/processed/frame_%04d.png";
			std::string frameLocationPathString = frameLocationPath.str();
			std::cout << "Frame location: " << frameLocationPathString << std::endl;
			execl("/usr/bin/ffmpeg", "-hide_banner", "-r", framerate, "-s", resolutionString.data(),
					 "-v", "quiet", "-i", frameLocationPathString.data(), "-pix_fmt", "yuv420p",
					 videoOutputPathString.data(), (char *) NULL);
			exit(1);
		}
		default: {
			waitpid(fork_status, &status, 0);
			std::cout << "Final video reassembly complete!" << std::endl;
		}
	}
	
	uploadVideo(ftp_client, jobId);
	
	return 0;
}

std::string getFrame(embeddedmz::CFTPClient *ftp_client, int jobId, int frameNumber) {
    std::stringstream filePath;
    filePath << "/" << jobId << "/unprocessed/frame_";
    std::string frame = std::to_string(frameNumber);
    int frameNumberLength = frame.size();
    int paddingZeroCount = 4 - frameNumberLength;
    if (paddingZeroCount > 0) {
        frame.insert(0, paddingZeroCount, '0');
    }
    filePath << frame;
    filePath << ".png";
    std::string filePathString = filePath.str();
    std::cout << "Frame to download: " << filePathString << std::endl;

    std::stringstream destinationFilePath;
    destinationFilePath << "./workspace/" << jobId << "/unprocessed";
    std::string destinationDirectory = destinationFilePath.str();
    destinationFilePath << "/frame_" << frame << ".png";
    std::string destinationFilePathString = destinationFilePath.str();
    std::cout << "Destination directory: " << destinationDirectory << std::endl;
    std::cout << "Downloading to: " << destinationFilePathString << std::endl;
    
    std::filesystem::create_directories(destinationDirectory);
    ftp_client->DownloadFile(destinationFilePathString, filePathString);
    return destinationFilePathString;
}

std::string getVideo(embeddedmz::CFTPClient *ftp_client, int jobId, std::string videoName) {
    std::stringstream filePath;
    filePath << "/" << jobId << "/input/" << videoName;
    std::string filePathString = filePath.str();
    std::cout << "Video to download: " << filePathString << std::endl;

    std::stringstream destinationFilePath;
    destinationFilePath << "./workspace/" << jobId << "/input";
    std::string destinationDirectory = destinationFilePath.str();
    destinationFilePath << "/" << videoName;
    std::string destinationFilePathString = destinationFilePath.str();
    std::cout << "Destination directory: " << destinationDirectory << std::endl;
    std::cout << "Downloading to: " << destinationFilePathString << std::endl;

    std::filesystem::create_directories(destinationDirectory);
    ftp_client->DownloadFile(destinationFilePathString, filePathString);
    return destinationFilePathString;
}


int uploadFrame(embeddedmz::CFTPClient *ftp_client, int jobId, int frameNumber) {
	std::cout << "Beginning upload of frame " << frameNumber << " for job " << jobId << std::endl;

	std::stringstream sourceFilepath;
	sourceFilepath << "./workspace/" << jobId << "/processed/frame_";
	std::string frame = std::to_string(frameNumber);
    int frameNumberLength = frame.size();
    int paddingZeroCount = 4 - frameNumberLength;
    if (paddingZeroCount > 0) {
        frame.insert(0, paddingZeroCount, '0');
    }
	sourceFilepath << frame << ".png";
	std::string sourceFilepathString = sourceFilepath.str();

	std::stringstream destinationFilepath;
	destinationFilepath << "/" << jobId << "/processed/frame_" << frame << ".png";
	std::string destinationFilepathString = destinationFilepath.str();

	ftp_client->UploadFile(sourceFilepathString, destinationFilepathString, true);
	std::cout << "Frame " << frameNumber << " for job " << jobId << " upload complete!" << std::endl;
	return 0;
}

int uploadVideo(embeddedmz::CFTPClient *ftp_client, int jobId) {
	std::cout << "Beginning upload of video for job " << jobId << std::endl;

	std::stringstream sourceFilepath;
	sourceFilepath << "./workspace/" << jobId << "/output/output.mp4";
	std::string sourceFilepathString = sourceFilepath.str();

	std::stringstream destinationFilepath;
	destinationFilepath << "/" << jobId << "/output/output.mp4";
	std::string destinationFilepathString = destinationFilepath.str();

	ftp_client->UploadFile(sourceFilepathString, destinationFilepathString, true);
	std::cout << "Video for job " << jobId << " complete!" << std::endl;
	return 0;
}


int getFrameSet(embeddedmz::CFTPClient *ftp_client, int jobId, int highestFrame) {
	std::cout << "Beginning download of frame set for job " << jobId << std::endl;
	std::stringstream sourceFilepath;
	sourceFilepath << "/" << jobId << "/processed/frame_";
	std::string sourceFilepathString = sourceFilepath.str();

	std::stringstream destinationFilepath;
	destinationFilepath << "./workspace/" << jobId << "/processed";
	std::string destinationDirectory = destinationFilepath.str();
	destinationFilepath << "/frame_";
	std::string destinationFilepathString = destinationFilepath.str();

	std::string fileExtension(".png");

	std::filesystem::create_directories(destinationDirectory);

	for (int i = 1; i <= highestFrame; i++) {
		std::string frame = std::to_string(i);
		int frameNumberLength = frame.size();
		int paddingZeroCount = 4 - frameNumberLength;
		if (paddingZeroCount > 0) {
			frame.insert(0, paddingZeroCount, '0');
		}
		ftp_client->DownloadFile(destinationFilepathString + frame + fileExtension, sourceFilepathString + frame + fileExtension);
	}
	return 0;
}


int uploadFrameSet(embeddedmz::CFTPClient *ftp_client, int jobId) {
	std::cout << "Beginning upload of frame set for job " << jobId << std::endl;
	std::stringstream sourceFilepath;
	sourceFilepath << "./workspace/" << jobId << "/unprocessed/";
	std::string sourceFilepathString = sourceFilepath.str();

	std::stringstream destinationFilepath;
	destinationFilepath << "/" << jobId << "/unprocessed/";
	std::string destinationFilepathString = destinationFilepath.str();

	std::filesystem::path sourcePath(sourceFilepathString);

	int highestFrame = -1;

	for (const auto & entry : std::filesystem::directory_iterator(sourcePath)) {
		std::string filename = entry.path().filename().string();
		
		std::string fileNumberString = filename.substr(6, 10);
		int testNumber = std::stoi(fileNumberString);
		//std::cout << "File being uploaded: " << filename << " Number value: " << testNumber << std::endl;
		if (testNumber > highestFrame) {
			highestFrame = testNumber;
		}
		ftp_client->UploadFile(sourceFilepathString + filename, destinationFilepathString + filename, true);
	}
	std::cout << "Frame set for job " << jobId << " complete!" << std::endl;
	return highestFrame;
}
