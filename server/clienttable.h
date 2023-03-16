#ifndef CLIENT_TABLE_H
#define CLIENT_TABLE_H

#include <vector>

class Client {
	public:
		Client(int);
		void setJobId(int);
		int getJobId();

	private:
		int client_id;
		int job_id;
};

class ClientTable {
	public:
		ClientTable();
		void addNewClient(int);
		Client *findClientById(int);
		void assignJobToClient(int, int);
	private:
		std::vector<Client> clients;
};

#endif
