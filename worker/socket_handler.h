#ifndef SOCKET_HANDLER_H
#define SOCKET_HANDLER_H

#include <vector>

class SocketHandler {
    public:
        SocketHandler(std::vector<int>, int);
        void socketThread();
    private:
        std::vector<int> socket_thread_fds;
        int socket_processing_fd;
};

#endif
