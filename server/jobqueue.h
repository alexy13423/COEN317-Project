#ifndef JOB_QUEUE_H
#define JOB_QUEUE_H

#include <string>
#include <mutex>
#include <condition_variable>

enum class JobType { preprocessing = 0, coordinate = 1, drawing = 2, postprocessing = 3 };
enum class JobStatus { waiting = 0, pending = 1, active = 2, complete = 3 };

class Job {
	public:
		Job(int, std::string);
		Job(int, int, JobType);
		Job(int);

		int getVideoId();
		int getFrameId();
		std::string getFileName();
		//bool getJobActive();
		//bool getJobComplete();
		JobType getJobType();
		JobStatus getJobStatus();
		Job* getNextJob();

		void setPending();
		void assignJob(int);
		void completeJob();
		void linkJobQueue(Job*);

		//std::string getJobData();
	private:
		int videoId;
		int frameId;
		std::string fileName;
		JobType jobType;
		Job *nextJob;

		int assignedClient;
		//bool jobComplete;
		JobStatus status;
};

class JobQueue {
	public:
		JobQueue();
		void createJob(int, std::string);
		void createJob(int, int, JobType);
		void createJob(int);

		//Job* findJobFromInfo(int, JobType);
		Job* findJobFromInfo(int, int, JobType);
		int getAvailableJobs();
		Job* getNextJob();
		void setNextJobPending();
		void assignNextJob(int);
		std::string getJobQueueData();

		std::mutex job_queue_mutex;
		std::condition_variable job_queue_cv;

		std::mutex socket_guard_mutex;
		int socket_guard;
		std::condition_variable socket_guard_cv;
	private:
		int queueLength;
		Job *firstJob;
};

std::string convertJobTypeToString(JobType);

#endif
