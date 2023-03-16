#include <string>
#include <sstream>
#include <chrono>
#include <iostream>

#include "jobqueue.h"

Job::Job(int videoId, std::string fileName) {
	this->videoId = videoId;
	this->fileName = fileName;
	frameId = -1;

	jobType = JobType::preprocessing;
	status = JobStatus::waiting;
	//jobComplete = false;
	assignedClient = -1;
	nextJob = (Job*) NULL;
}

Job::Job(int videoId, int frameId, JobType jobType) {
	this->videoId = videoId;
	this->frameId = frameId;
	this->jobType = jobType;

	//jobComplete = false;
	status = JobStatus::waiting;
	assignedClient = -1;
	nextJob = (Job*) NULL;
}

Job::Job(int videoId) {
	this->videoId = videoId;

	jobType = JobType::postprocessing;

	//jobComplete = false;
	status = JobStatus::waiting;
	assignedClient = -1;
	nextJob = (Job*) NULL;
}

void Job::assignJob(int workerId) {
	assignedClient = workerId;
	status = JobStatus::active;
}

void Job::completeJob() {
	//jobComplete = true;
	status = JobStatus::complete;
}

void Job::linkJobQueue(Job *nextJob) {
	this->nextJob = nextJob;
}

int Job::getVideoId() {
	return videoId;
}

int Job::getFrameId() {
	return frameId;
}

std::string Job::getFileName() {
	return fileName;
}

/*
bool Job::getJobActive() {
	if (assignedClient == -1) {
		return false;
	} else if (jobComplete) {
		return false;
	} else return true;
}

bool Job::getJobComplete() {
	return jobComplete;
}*/

JobType Job::getJobType() {
	return jobType;
}

JobStatus Job::getJobStatus() {
	return status;
}

Job* Job::getNextJob() {
	return nextJob;
}

void Job::setPending() {
	status = JobStatus::pending;
}

JobQueue::JobQueue() {
	queueLength = 0;
	firstJob = (Job*) NULL;
	socket_guard = 1;
}

void JobQueue::createJob(int videoId, std::string fileName) {
	Job* newJob = new Job(videoId, fileName);
	if (!firstJob) {
		firstJob = newJob;
	} else {
		bool keepProcessing = true;
		int index = 1;
		Job *currentJob = firstJob;
		while (keepProcessing) {
			Job *nextJob = currentJob->getNextJob();
			if (!nextJob) {
				keepProcessing = false;
			} else currentJob = nextJob;
		}
		currentJob->linkJobQueue(newJob);
	}
}

void JobQueue::createJob(int videoId, int frameId, JobType jobType) {
	Job* newJob = new Job(videoId, frameId, jobType);
	if (!firstJob) {
		firstJob = newJob;
	} else {
		bool keepProcessing = true;
		Job *currentJob = firstJob;
		while (keepProcessing) {
			Job *nextJob = currentJob->getNextJob();
			if (!nextJob) {
				keepProcessing = false;
			} else currentJob = nextJob;
		}
		currentJob->linkJobQueue(newJob);
	}
}

void JobQueue::createJob(int videoId) {
	Job* newJob = new Job(videoId);
	if (!firstJob) {
		firstJob = newJob;
	} else {
		bool keepProcessing = true;
		Job *currentJob = firstJob;
		while (keepProcessing) {
			Job *nextJob = currentJob->getNextJob();
			if (!nextJob) {
				keepProcessing = false;
			} else currentJob = nextJob;
		}
		currentJob->linkJobQueue(newJob);
	}
}

Job* JobQueue::findJobFromInfo(int videoId, int frameId, JobType jobType) {
	Job* result = (Job*) NULL;
	Job* check = firstJob;
	while (check) {
		if (jobType == JobType::preprocessing || jobType == JobType::postprocessing) {
			if (check->getVideoId() == videoId && check->getJobType() == jobType) {
				result = check;
				break;
			}
		} else if (jobType == JobType::coordinate || jobType == JobType::drawing) {
			if (check->getVideoId() == videoId && check->getFrameId() == frameId && check->getJobType() == jobType) {
				result = check;
				break;
			}
		}

		check = check->getNextJob();
	}
	return result;
}

int JobQueue::getAvailableJobs() {
	int result = 0;
	Job *currentJob = firstJob;
	while (currentJob) {
		if (currentJob->getJobStatus() == JobStatus::waiting) {
			result++;
		}
		Job *nextJob = currentJob->getNextJob();
		currentJob = nextJob;
	}
	return result;
}

Job* JobQueue::getNextJob() {
	Job *result = firstJob;
	if (result->getJobStatus() == JobStatus::waiting) {
		std::cout << "Returning job of type: " << convertJobTypeToString(result->getJobType()) << std::endl;
		std::cout << "Returning job with video id: " << result->getVideoId() << std::endl;
		return result;
	}
	bool keepProcessing = true;
	while (keepProcessing) {
		result = result->getNextJob();
		if (!result || result->getJobStatus() == JobStatus::waiting) {
			keepProcessing = false;
		}
	}
	return result;
}

std::string JobQueue::getJobQueueData() {
	std::stringstream resultStream;

	Job *currentPointer = firstJob;
	int completedJobs = 0;
	int pendingJobs = 0;
	int activeJobs = 0;

	while (true) {
		JobType currentType = currentPointer->getJobType();
		JobStatus currentStatus = currentPointer->getJobStatus();
		
		if (currentType == JobType::coordinate || currentType == JobType::drawing) {
			if (currentStatus == JobStatus::waiting || currentStatus == JobStatus::pending) {
				pendingJobs++;
			} else if (currentStatus == JobStatus::active) {
				activeJobs++;
			} else if (currentStatus == JobStatus::complete) {
				completedJobs++;
			}
		}

		currentPointer = currentPointer->getNextJob();
		if (currentPointer == NULL) {
			break;
		}
	}

	resultStream << "Pending frames: " << pendingJobs << " Active frames being processed: " << activeJobs << " Completed frames: " << completedJobs << '\n';

	std::string result = resultStream.str();
	//std::cout << "Job queue data: " << result;
	return result;
}

std::string convertJobTypeToString(JobType convert) {
	if (convert == JobType::preprocessing) {
		return std::string("preprocessing");
	} else if (convert == JobType::coordinate) {
		return std::string("coordinate");
	} else if (convert == JobType::drawing) {
		return std::string("drawing");
	} else if (convert == JobType::postprocessing) {
		return std::string("postprocessing");
	} else return std::string("unknown");
}
