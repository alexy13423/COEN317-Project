#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <pqxx/pqxx>

#include <stdlib.h>
#include <unistd.h>

#include "processing.h"

ProcessingHandler::ProcessingHandler(WorkerTable *workerTable, JobQueue *jobQueue, int processing_worker_fd, pqxx::connection *dbConnection) {
	this->workerTable = workerTable;
	this->jobQueue = jobQueue;
	this->processing_worker_fd = processing_worker_fd;
	//this->processing_client_fd = processing_client_fd;
	this->dbConnection = dbConnection;

	keepLooping = true;
}

void ProcessingHandler::doShutdown() {
	std::cout << "Processing: shutdown signal sent." << std::endl;
	keepLooping = false;
}

void ProcessingHandler::processingThread() {
	while (keepLooping) {
		/*
			Step 1: Get a job from the job queue.
			Step 2: Get a worker from the worker table.
			Step 3: Send job to worker via the worker socket pipe.
			Step 4: Repeat ad infinitum.
		*/

		//Get a job from the job queue.
		std::unique_lock job_queue_lock(jobQueue->job_queue_mutex);
		int availableJobs = jobQueue->getAvailableJobs();
		while (availableJobs == 0 && keepLooping) {
			std::cout << "Processing: Waiting for available job...Available jobs: " << availableJobs << std::endl;
			jobQueue->job_queue_cv.wait(job_queue_lock);
			availableJobs = jobQueue->getAvailableJobs();
			std::cout << "Checking available jobs after signal: " << availableJobs << std::endl;
		}
		if (!keepLooping) {
			std::cout << "Processing: Shutdown signal detected." << std::endl;
			job_queue_lock.unlock();
			break;
		}
		//std::cout << "asdf" << std::endl;
		Job *processingJob = jobQueue->getNextJob();
		std::cout << "Processing: Pending job detected." << std::endl;
		processingJob->setPending();
		job_queue_lock.unlock();

		//Get a worker from the worker table.
		std::unique_lock worker_table_lock(workerTable->worker_table_mutex);
		bool worker_available = workerTable->checkWorkerAvailable();
		while (!worker_available && keepLooping) {
			std::cout << "Processing: Waiting for available worker..." << std::endl;
			workerTable->worker_table_cv.wait(worker_table_lock);
			worker_available = workerTable->checkWorkerAvailable();
		}
		if (!keepLooping) {
			std::cout << "Processing: Shutdown signal detected." << std::endl;
			worker_table_lock.unlock();
			break;
		}

		Worker *selectedWorker = workerTable->getAvailableWorker();
		int workerId = selectedWorker->getWorkerId();
		//selectedWorker->setPending();
		selectedWorker->setActivityStatus(WorkerStatus::pending);
		worker_table_lock.unlock();

		//Construct the message to be sent to the worker.
		std::stringstream message_to_send;
		std::string jobType;
		JobType processingJobType = processingJob->getJobType();
		jobType = convertJobTypeToString(processingJobType);
		std::cout << "Job type: " << jobType << std::endl;
		std::cout << "Job id: " << processingJob->getVideoId() << std::endl;
		std::cout << "Job frame id: " << processingJob->getFrameId() << std::endl;

		std::cout << "Worker id: " << selectedWorker->getWorkerId() << std::endl;
		message_to_send << "sendworker " << workerId;
		switch(processingJob->getJobType()) {
			case JobType::preprocessing:
				message_to_send << " preprocessing " << processingJob->getVideoId()
								<< " " << processingJob->getFileName();
				break;
			case JobType::coordinate:
				message_to_send << " coordinate " << processingJob->getVideoId()
								<< " " << processingJob->getFrameId();
				break;
			case JobType::drawing:
				message_to_send << " drawing " << processingJob->getVideoId()
								<< " " << processingJob->getFrameId();
				break;
			case JobType::postprocessing:
				message_to_send << " postprocessing " << processingJob->getVideoId()
								<< " " << -1;
				break;
			default:
				std::cerr << "Unknown job type being processed" << std::endl;
				exit(1);
		}
		std::string job_string = message_to_send.str();
		std::cout << "Processing: Job string to write to worker socket handler: " << job_string << std::endl;
		write(processing_worker_fd, job_string.data(), job_string.length());

		//Need to delay on something here?
		std::unique_lock socket_guard_lock(jobQueue->socket_guard_mutex);
		while (jobQueue->socket_guard == 0) {
			jobQueue->socket_guard_cv.wait(socket_guard_lock);
		}
		socket_guard_lock.unlock();
	}
	std::cout << "Terminating processing thread." << std::endl;
}
