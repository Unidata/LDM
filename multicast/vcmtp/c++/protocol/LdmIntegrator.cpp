/*
 * LdmIntegrator.cpp
 *
 *  Created on: Oct 22, 2012
 *      Author: jie
 */

#include "LdmIntegrator.h"
#include "SenderStatusProxy.h"

LdmIntegrator::LdmIntegrator(VCMTPSender* s, string save_path, SenderStatusProxy* p) {
	sender = s;
	if (save_path.size() > 0 && save_path[save_path.size() - 1] == '/')
		save_dir = save_path;
	else
		save_dir = save_path + '/';

	proxy = p;
	keep_alive = false;
	send_thread_exited = true;
	recv_thread_exited = true;
	pthread_mutex_init(&send_mutex, NULL);
}

LdmIntegrator::~LdmIntegrator() {
	pthread_mutex_destroy(&send_mutex);
}


void LdmIntegrator::Start() {
	keep_alive = true;

	pthread_create(&send_thread, NULL, &LdmIntegrator::StartSendThread, this);
	pthread_create(&recv_thread, NULL, &LdmIntegrator::StartReceiveThread, this);
}


void LdmIntegrator::Stop() {
	keep_alive = false;
	while (!(send_thread_exited && recv_thread_exited)) {
		usleep(5000);
	}
}


void* LdmIntegrator::StartSendThread(void* ptr) {
	((LdmIntegrator*)ptr)->RunSendThread();
	return NULL;
}


void LdmIntegrator::RunSendThread() {
	vector<string> files;
	vector<uint>	ids;
	char msg[512];
	send_thread_exited = false;
	while (keep_alive) {
		GetFilesInDirectory(files);
		if (files.size() == 0) {
			usleep(50000);
			continue;
		}

		for (int i = 0; i < files.size(); i++) {
			ids.push_back(sender->SendFile(files[i].c_str(),
							proxy->GetRetransmissionTimeoutRatio()));
		}

		for (int i = 0; i < ids.size(); i++) {
			while (!sender->IsTransferFinished(ids[i])) {
				usleep(10000);
			}
		}

		sprintf(msg, "%d products have been multicast.", ids.size());
		proxy->SendMessageLocal(INFORMATIONAL, msg);

		for (int i = 0; i < files.size(); i++) {
			unlink(files[i].c_str());
		}
		files.clear();
		ids.clear();
	}

	send_thread_exited = true;
}

int LdmIntegrator::GetFilesInDirectory(vector<string> &files)
{
    DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(save_dir.c_str())) == NULL) {
        cout << "Error(" << errno << ") opening " << save_dir << endl;
        return errno;
    }

    while ((dirp = readdir(dp)) != NULL) {
    	string fname = save_dir + dirp->d_name;
    	if (fname[fname.size() - 1] != '.')
    		files.push_back(fname);
    }
    closedir(dp);
    return 0;
}




//======================= Receive files from a remote source =======================
void* LdmIntegrator::StartReceiveThread(void* ptr) {
	((LdmIntegrator*)ptr)->RunReceiveThread();
	return NULL;
}


void LdmIntegrator::RunReceiveThread() {
	recv_thread_exited = false;

	int server_sock;
	if ((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		recv_thread_exited = true;
		SendMessage("Error creating a new socket!");
		return;
	}

	struct sockaddr_in server_addr;
	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(SERVER_PORT);

	if (bind(server_sock, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0) {
		recv_thread_exited = true;
		SendMessage("Error binding a socket!");
		return;
	}

	if (listen(server_sock, 1000) < 0) {
		recv_thread_exited = true;
		SendMessage("Error start listening!");
		return;
	}

	int count = 1;
	char buf[BUFFER_SIZE];
	char file_name[1024];
	int bytes;
	int product_len;
	while (keep_alive) {
		int sock;
		if ((sock = accept(server_sock, (struct sockaddr*) NULL, NULL)) < 0) {
			recv_thread_exited = true;
			return;
		}

		if ((bytes = recv(sock, &product_len, sizeof(int), 0)) < 0) {
			recv_thread_exited = true;
			return;
		}

		sprintf(file_name, "%sproduct%d.dat", save_dir.c_str(), count);
		ofstream outfile(file_name);
		int remained = product_len;
		while (remained > 0) {
			int wr_len = remained > BUFFER_SIZE ? BUFFER_SIZE : remained;
			recv(sock, buf, wr_len, 0);
			outfile.write(buf, wr_len);
			remained -= wr_len;
		}
		outfile.close();
		count++;

		if (close(sock) < 0) {
			SendMessage("Error: cannot close the socket!");
		}
	}

	close(server_sock);
	recv_thread_exited = true;
}


void LdmIntegrator::SendMessage(const char* msg) {
	pthread_mutex_lock(&send_mutex);
	proxy->SendMessageLocal(INFORMATIONAL, msg);
	pthread_mutex_unlock(&send_mutex);
}




