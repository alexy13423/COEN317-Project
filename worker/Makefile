worker:
	g++ `pkg-config --cflags opencv4` -o worker worker.cpp socket_handler.cpp processing_handler.cpp libftpclient.a -lcurl -lpqxx -lpq `pkg-config --libs opencv4`

clean:
	rm worker
