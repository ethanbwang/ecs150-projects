#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include <deque>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "FileService.h"
#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HttpService.h"
#include "HttpUtils.h"
#include "MyServerSocket.h"
#include "MySocket.h"
#include "dthread.h"

using namespace std;

int PORT = 8080;
int THREAD_POOL_SIZE = 1;
int BUFFER_SIZE = 1;
string BASEDIR = "static";
string SCHEDALG = "FIFO";
string LOGFILE = "/dev/null";

vector<HttpService *> services;

HttpService *find_service(HTTPRequest *request) {
  // find a service that is registered for this path prefix
  for (unsigned int idx = 0; idx < services.size(); idx++) {
    if (request->getPath().find(services[idx]->pathPrefix()) == 0) {
      return services[idx];
    }
  }

  return NULL;
}

void invoke_service_method(HttpService *service, HTTPRequest *request,
                           HTTPResponse *response) {
  stringstream payload;

  // invoke the service if we found one
  if (service == NULL) {
    // not found status
    response->setStatus(404);
  } else if (request->isHead()) {
    service->head(request, response);
  } else if (request->isGet()) {
    service->get(request, response);
  } else {
    // The server doesn't know about this method
    response->setStatus(501);
  }
}

void handle_request(MySocket *client) {
  HTTPRequest *request = new HTTPRequest(client, PORT);
  HTTPResponse *response = new HTTPResponse();
  stringstream payload;

  // read in the request
  bool readResult = false;
  try {
    payload << "client: " << (void *)client;
    sync_print("read_request_enter", payload.str());
    readResult = request->readRequest();
    sync_print("read_request_return", payload.str());
  } catch (...) {
    // swallow it
  }

  if (!readResult) {
    // there was a problem reading in the request, bail
    delete response;
    delete request;
    sync_print("read_request_error", payload.str());
    return;
  }

  HttpService *service = find_service(request);
  invoke_service_method(service, request, response);

  // send data back to the client and clean up
  payload.str("");
  payload.clear();
  payload << " RESPONSE " << response->getStatus()
          << " client: " << (void *)client;
  sync_print("write_response", payload.str());
  cout << payload.str() << endl;
  client->write(response->response());

  delete response;
  delete request;

  payload.str("");
  payload.clear();
  payload << " client: " << (void *)client;
  sync_print("close_connection", payload.str());
  client->close();
  delete client;
}

// Shared variables
pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t wait_cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t wait_cond = PTHREAD_COND_INITIALIZER;

pthread_mutex_t buf_full_cond_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t buf_full_cond = PTHREAD_COND_INITIALIZER;

deque<pair<MySocket *, unique_ptr<HTTPRequest>>> req_queue =
    deque<pair<MySocket *, unique_ptr<HTTPRequest>>>();

void read_request(MySocket *client, HTTPRequest *request) {
  // Reads a request from the client into request
  stringstream payload;

  // read in the request
  bool readResult = false;
  try {
    payload << "client: " << (void *)client;
    sync_print("read_request_enter", payload.str());
    readResult = request->readRequest();
    sync_print("read_request_return", payload.str());
  } catch (...) {
    // swallow it
  }

  if (!readResult) {
    // there was a problem reading in the request, bail
    sync_print("read_request_error", payload.str());
    return;
  }
}

void *worker_job(void *emily) {
  /*
   * Worker thread function
   * Waits for a request, then processes it, responds to the client, and cleans
   * up.
   */
  // Worker loop
  while (true) {
    // Wait for a request
    dthread_mutex_lock(&wait_cond_mutex);
    while (req_queue.size() == 0) {
      if (dthread_cond_wait(&wait_cond, &wait_cond_mutex) != 0) {
        cerr << "Wait on wait condition failed\n";
        return NULL;
      }
    }
    dthread_mutex_unlock(&wait_cond_mutex);

    // Process request
    // Get request at front of the queue
    dthread_mutex_lock(&buf_full_cond_mutex);
    dthread_mutex_lock(&req_queue_mutex);

    MySocket *client = req_queue.front().first;
    unique_ptr<HTTPRequest> req = move(req_queue.front().second);
    req_queue.pop_front();
    // Signal that a request was processed (buffer shrunk by 1)
    dthread_cond_signal(&buf_full_cond);

    dthread_mutex_unlock(&req_queue_mutex);
    dthread_mutex_unlock(&buf_full_cond_mutex);

    // Handle request
    auto response = make_unique<HTTPResponse>();
    *response = HTTPResponse();
    stringstream payload;

    HttpService *service = find_service(req.get());
    invoke_service_method(service, req.get(), response.get());

    // Send data back to the client
    payload << " RESPONSE " << response->getStatus()
            << " client: " << (void *)client;
    sync_print("write_response", payload.str());
    cout << payload.str() << '\n';

    // Close connection
    payload.str("");
    payload.clear();
    payload << " client: " << (void *)client;
    sync_print("close_connection", payload.str());
    client->close();
    delete client;
  }
  return NULL;
}

int main(int argc, char *argv[]) {

  signal(SIGPIPE, SIG_IGN);
  int option;

  while ((option = getopt(argc, argv, "d:p:t:b:s:l:")) != -1) {
    switch (option) {
    case 'd':
      BASEDIR = string(optarg);
      break;
    case 'p':
      PORT = atoi(optarg);
      break;
    case 't':
      THREAD_POOL_SIZE = atoi(optarg);
      break;
    case 'b':
      BUFFER_SIZE = atoi(optarg);
      break;
    case 's':
      SCHEDALG = string(optarg);
      break;
    case 'l':
      LOGFILE = string(optarg);
      break;
    default:
      cerr << "usage: " << argv[0] << " [-p port] [-t threads] [-b buffers]"
           << endl;
      exit(1);
    }
  }

  set_log_file(LOGFILE);

  sync_print("init", "");
  MyServerSocket *server = new MyServerSocket(PORT);
  MySocket *client;

  // The order that you push services dictates the search order
  // for path prefix matching
  services.push_back(new FileService(BASEDIR));

  pthread_t thread_pool[THREAD_POOL_SIZE];
  // Create workers
  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    if (dthread_create(&thread_pool[i], NULL, &worker_job, NULL) != 0) {
      cerr << "Error creating thread\n";
      return 1;
    }
  }

  // Supervisor loop (multi-threaded web server)
  while (true) {
    // Wait for buffer to clear up
    dthread_mutex_lock(&buf_full_cond_mutex);
    while (static_cast<int>(req_queue.size()) == BUFFER_SIZE) {
      if (dthread_cond_wait(&buf_full_cond, &buf_full_cond_mutex) != 0) {
        cerr << "Wait on buffer full condition failed\n";
        exit(1);
      }
    }
    dthread_mutex_unlock(&buf_full_cond_mutex);

    // Wait for requests
    sync_print("waiting_to_accept", "");
    client = server->accept();

    // Add request to queue
    sync_print("client_accepted", "");
    dthread_mutex_lock(&wait_cond_mutex);
    dthread_mutex_lock(&req_queue_mutex);

    auto req_p = make_unique<HTTPRequest>(client, PORT);
    read_request(client, req_p.get());
    req_queue.push_back(make_pair(client, move(req_p)));
    // Signal new request
    dthread_cond_signal(&wait_cond);

    dthread_mutex_unlock(&req_queue_mutex);
    dthread_mutex_unlock(&wait_cond_mutex);
  }

  // Single-threaded web server logic
  // while (true) {
  //   sync_print("waiting_to_accept", "");
  //   client = server->accept();
  //   sync_print("client_accepted", "");
  //   handle_request(client);
  // }
}
