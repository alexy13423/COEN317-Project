#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <pqxx/pqxx>

#include <unistd.h>
#include <sys/poll.h>

#include "processing_handler.h"

#include "FTPClient.h"

ProcessHandler::ProcessHandler(std::vector<int> process_thread_fds, int processing_socket_fd) {
    this->process_thread_fds = process_thread_fds;
    this->processing_socket_fd = processing_socket_fd;
}

//ffprobe -hide_banner -select_streams v -show_entries stream=width,height,r_frame_rate,duration -of default=noprint_wrappers=1
//ffmpeg -i Big_Buck_Bunny_Trailer_1080p.ogx frame%04d.png
//ffmpeg -hide_banner -r 30 -s 1920x1080 -i frame%04d.png -pix_fmt yuv420p out/out.mp4
void ProcessHandler::processThread() {
	std::cout << "Process handler todo." << std::endl;

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
				//TODO: More robust message processing to identify job and arguments here.
				memset(buffer, 0, 1024);

				int jobId = 1;
				std::string input_video_filename = pipeValue;

				handlePreprocessing(&FTPClient, jobId, input_video_filename, &dbConnection);
			}
		}
	}
}

int handlePreprocessing(embeddedmz::CFTPClient *ftp_client, int jobId, std::string videoName, pqxx::connection *db_connection) {
	
	std::string video_filepath = getVideo(ftp_client, jobId, videoName);

	int external_process_fds[2];
	int pipe_status = pipe(external_process_fds);
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
			close (external_process_fds[1]);
			FILE *external_fd_file = fdopen(external_process_fds[0], "r");
			if (external_fd_file == NULL) {
				std::cerr << "Processing thread external fd fdopen failure." << std::endl;
				//return;
				exit(1);
			}
			//char line[2000];
			char *line = NULL;
			size_t len = 0;
			//memset (&line[0], 0, sizeof(line));
			int bytes_read = getline(&line, &len, external_fd_file);
			pqxx::work txn(*db_connection);
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
				" where videoid = 1");
			txn.commit();
			std::cout << "Processing of thread output complete!" << std::endl;
		}
	}
	close (external_process_fds[0]);

	//processing video frames now, go again
	pipe_status = pipe(external_process_fds);
	if (pipe_status < 0) {
		std::cerr << "Process handler pipe failed." << std::endl;
		exit(1);
	}

	//std::filesystem::create_directories(destinationDirectory);
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
			dup2(external_process_fds[1], 1);
			close (external_process_fds[0]);
			std::stringstream framePath;
			framePath << "./workspace/" << 1 << "/unprocessed/frame_%04d.png";
			std::string framePathString = framePath.str();
            //ffmpeg -i Big_Buck_Bunny_Trailer_1080p.ogx frame%04d.png
			std::cout << "Extracting frames from video: " << video_filepath << std::endl;
			std::cout << "Frames extracted being saved as: " << framePathString << std::endl;
			execl ("/usr/bin/ffmpeg", "-hide_banner", "-v", "quiet", "-i", video_filepath.c_str(), framePathString.c_str(), (char *) NULL);
			exit(1); //this should not execute, because exec
		}
		default: {
			close (external_process_fds[1]);
			FILE *external_fd_file = fdopen(external_process_fds[0], "r");
			if (external_fd_file == NULL) {
				std::cerr << "Processing thread external fd fdopen failure." << std::endl;
				//return;
				exit(1);
			}
			//char line[2000];
			char *line = NULL;
			size_t len = 0;
			//memset (&line[0], 0, sizeof(line));
			int bytes_read = getline(&line, &len, external_fd_file);
			while (bytes_read > 0) {
				std::string external_string(line);
				std::cout << "Processing thread output line: " << external_string << std::endl;
				bytes_read = getline(&line, &len, external_fd_file);
			}
			std::cout << "Processing of thread output complete!" << std::endl;
		}
	}

	uploadFrameSet(ftp_client, jobId);
	return 0;
}

int handleCoordinateDetection(embeddedmz::CFTPClient *ftp_client, int jobId, int frameNumber, pqxx::connection *db_connection) {
	std::string frame_filepath = getFrame(ftp_client, jobId, frameNumber);

	int external_process_fds[2];
	int pipe_status = pipe(external_process_fds);
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
			std::stringstream framePath;
			//framePath << "./workspace/" << 1 << "/unprocessed/frame_%04d.png";
			//std::string framePathString = framePath.str();
            //ffmpeg -i Big_Buck_Bunny_Trailer_1080p.ogx frame%04d.png
			//std::cout << "Extracting frames from video: " << video_filepath << std::endl;
			//std::cout << "Frames extracted being saved as: " << framePathString << std::endl;
			execl ("/usr/bin/ffmpeg", "-hide_banner", "-v", "quiet", "-i", video_filepath.c_str(), framePathString.c_str(), (char *) NULL);
			exit(1); //this should not execute, because exec
		}
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
    filePath << ".jpg";
    std::string filePathString = filePath.str();
    std::cout << "Frame to download: " << filePathString << std::endl;

    std::stringstream destinationFilePath;
    destinationFilePath << "./workspace/" << jobId << "/unprocessed";
    std::string destinationDirectory = destinationFilePath.str();
    destinationFilePath << "/frame_" << frame << ".jpg";
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
	std::cout << "Frame " << frameNumber << " for job " << jobId << " complete!" << std::endl;
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

/*
int getFrameSet(embeddedmz::CFTPClient ftp_client, int jobId) {
}
*/

int uploadFrameSet(embeddedmz::CFTPClient *ftp_client, int jobId) {
	std::cout << "Beginning upload of frame set for job " << jobId << std::endl;
	std::stringstream sourceFilepath;
	sourceFilepath << "./workspace/" << jobId << "/unprocessed/";
	std::string sourceFilepathString = sourceFilepath.str();

	std::stringstream destinationFilepath;
	destinationFilepath << "/" << jobId << "/unprocessed/";
	std::string destinationFilepathString = destinationFilepath.str();

	std::filesystem::path sourcePath(sourceFilepathString);

	for (const auto & entry : std::filesystem::directory_iterator(sourcePath)) {
		std::string filename = entry.path().filename().string();
		ftp_client->UploadFile(sourceFilepathString + filename, destinationFilepathString + filename, true);
	}
	std::cout << "Frame set for job " << jobId << " complete!" << std::endl;
	return 0;
}
