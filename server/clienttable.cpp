#include "clienttable.h"

Client::Client(int client_id) {
	this->client_id = client_id;
	job_id = -1;
}

void Client::setJobId(int job_id) {
	this->job_id = job_id;
}

int Client::getJobId() {
	return job_id;
}

ClientTable::ClientTable() {
}

void ClientTable::addNewClient(int newClientId) {
	Client newClient(newClientId);
	clients.push_back(newClient);
}
