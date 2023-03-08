#ifndef PROCESS_HANDLER_H
#define PROCESS_HANDLER_H

#include <vector>
#include <string>
#include <pqxx/pqxx>

#include "FTPClient.h"

class ProcessHandler {
	public:
		ProcessHandler(std::vector<int>, int);
		void processThread();
	private:
		std::vector<int> process_thread_fds;
		int processing_socket_fd;
};

int handlePreprocessing(embeddedmz::CFTPClient*, int, std::string, pqxx::connection*);
int handleCoordinateDetection(embeddedmz::CFTPClient*, int, int, pqxx::connection*);
//int handleDrawing(embeddedmz::CFTPClient*, int, int, pqxx::connection*);
//int handlePostprocessing(embeddedmz::CFTPClient*, int, pqxx::connection*);

std::string getFrame(embeddedmz::CFTPClient*, int, int);
std::string getVideo(embeddedmz::CFTPClient*, int, std::string);
int uploadFrame(embeddedmz::CFTPClient*, int, std::string);
int uploadVideo(embeddedmz::CFTPClient*, int, std::string);
int getFrameSet(embeddedmz::CFTPClient*, int);
int uploadFrameSet(embeddedmz::CFTPClient*, int);

#endif
