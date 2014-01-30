/*
 * ExperimentManager2.cpp
 *
 *  Created on: Aug 31, 2012
 *      Author: jie
 */

#include "SenderStatusProxy.h"
#include "ExperimentManager2.h"


ExperimentManager2::ExperimentManager2() {
	pthread_mutex_init(&write_mutex, NULL);
}


ExperimentManager2::~ExperimentManager2() {
	pthread_mutex_destroy(&write_mutex);
}



void ExperimentManager2::ReadFileSizes(vector<int>& file_sizes) {
	system("sudo cp /users/jieli/src/file_sizes.csv /tmp/temp");
	// read in file sizes and inter-arrival-times
	ifstream fs_file("/tmp/temp/file_sizes.csv");
	double size = 0;

	while (fs_file >> size) {
		file_sizes.push_back((int)size);
	}
	fs_file.close();
}


void ExperimentManager2::ReadInterArrivals(vector<double>& inter_arrival_times) {
	system("sudo cp /users/jieli/src/inter_arrival_times.csv /tmp/temp");
	ifstream irt_file("/tmp/temp/inter_arrival_times.csv");
	double time;
	while (irt_file >> time) {
		if (time < 20.0)
			inter_arrival_times.push_back(time);
	}
	irt_file.close();
}


void ExperimentManager2::GenerateFile(string file_name, int size) {
	static const int BUF_SIZE = 4096;
	static char buf[BUF_SIZE];

	int remained_size = size; //* 100;
	ofstream outfile(file_name.c_str(), ofstream::binary | ofstream::trunc);
	while (remained_size > 0) {
		int data_len = (remained_size > BUF_SIZE) ? BUF_SIZE : remained_size;
		outfile.write(buf, data_len);
		remained_size -= data_len;
	}
	outfile.close();
}


static const int 	NUM_EXPERIMENTS = 5;
static const int 	FILE_COUNT = 500;
static const int 	SLOW_RECEIVER_RATIO = 40;  // in percent
void ExperimentManager2::StartExperiment2(SenderStatusProxy* sender_proxy, VCMTPSender* sender) {
	this->sender_proxy = sender_proxy;
	this->sender = sender;

	// Set the sending thread to use Sched_RR high priority mode
	//sender->SetSchedRR(true);

	//sender->SetSendRate(600);
	system("mkdir /tmp/temp");
	system("sudo rm /tmp/temp/temp*.dat");

	//list<int> recv_socks = sender->GetReceiverTCPSockets();
	//sender->ExecuteCommandOnReceivers("mkdir /tmp/temp", 0, recv_socks.size());

	int TIMEOUT_RATIO[] = {5000}; //{5000, 1000}; //{10000000}; //{10000, 5000};
	int NUM_TIMEOUT_RATIO = 1;
	int RHO[] = {80}; //{40, 80};  //{80, 90}; //   // in percent
	int NUM_RHO = 1;
	int LOSS_RATE[] = {100}; //{50, 100}; //{200, 400};  //{10, 20}; // out of 1000 packets
	int NUM_LOSS_RATE = 1;

	vector<int> file_sizes;
	ReadFileSizes(file_sizes);

	vector<double> inter_arrival_times;
	ReadInterArrivals(inter_arrival_times);

	for (int time_index = 0; time_index < NUM_TIMEOUT_RATIO; time_index++) {
		for (int rho_index = 0; rho_index < NUM_RHO; rho_index++) {
			for (int loss_index = 0; loss_index < NUM_LOSS_RATE; loss_index++) {
				// Run the experiments for NUM_EXPERIMENTS times
				char file_name[100];
				sprintf(file_name, "exp_timeout%d_rho%d_loss%d_nodes%d.csv",
						TIMEOUT_RATIO[time_index], RHO[rho_index], LOSS_RATE[loss_index], sender->GetReceiverTCPSockets().size());
				result_file.open(file_name);
				result_file << "#Node ID, Log Time (Sec), File ID, File Size (bytes), Transfer Time (sec), Retx Bytes, Success, Is Slow Node" << endl;
				RunOneExperimentSet(file_sizes, inter_arrival_times, TIMEOUT_RATIO[time_index],
						RHO[rho_index], LOSS_RATE[loss_index]);
				result_file.close();
			}
		}
	}
}




void ExperimentManager2::RunOneExperimentSet(vector<int>& file_sizes, vector<double>& inter_arrival_times,
											int timeout_ratio, int rho, int loss_rate) {
	// set loss rate
	list<int> recv_socks = sender->GetReceiverTCPSockets();
	int num_slow_receivers = recv_socks.size() * SLOW_RECEIVER_RATIO / 100;
	list<int>::const_iterator it;
	for (it = recv_socks.begin(); it != recv_socks.end(); it++) {
		sender->SetReceiverLossRate(*it, 0);
	}

	it = recv_socks.begin();
	for (int i = 0; i < num_slow_receivers; i++) {
		sender->SetReceiverLossRate(*it, loss_rate);
		it++;
	}

	sender->ResetSessionID();

	char str[256];
	for (int n = 0; n < NUM_EXPERIMENTS; n++) {
		//sender->ExecuteCommandOnReceivers("sudo rm /tmp/temp/temp*.dat", 0, recv_socks.size());
		sprintf(str, "\n\n***** Run %d *****\nGenerating files...\n", n + 1);
		sender_proxy->SendMessageLocal(INFORMATIONAL, str);
		// Generate files
		int file_index = 1;
		char file_name[256];
		File_Sample sample;
		for (int i = 0; i < FILE_COUNT; i++) {
			int index = /*n * FILE_COUNT + */i;
			int fsize= file_sizes[index] * 2 * rho / 100;
			sample.file_sizes.push_back(fsize);
			sample.total_file_size += fsize;
			double inter_arrival_time = inter_arrival_times[index] * 2;
			sample.inter_arrival_times.push_back(inter_arrival_time);
			sample.total_time += inter_arrival_time;

			// Generate the file
			sprintf(file_name, "/tmp/temp/temp%d.dat", file_index++);
			GenerateFile(file_name, fsize);
		}


		sender->ResetAllReceiverStats();
		sender->ResetMetadata();

		// Start sending files
		sender_proxy->SendMessageLocal(INFORMATIONAL, "Sending files...\n");
		//system("sudo sync && sudo echo 3 > /proc/sys/vm/drop_caches");

		struct timespec time_spec;
		time_spec.tv_sec = 0;
		time_spec.tv_nsec = 0;

		CpuCycleCounter cpu_counter;
		AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
		double sent_time = 0.0;
		double curr_time = 0.0;

		int file_id = 0;
		char str[500];
		for (int i = 0; i < FILE_COUNT; i++) {
			if (i % 100 == 0) {
				sprintf(str, "Sending file %d", i + 1);
				sender_proxy->SendMessageLocal(INFORMATIONAL, str);
			}

			sent_time += sample.inter_arrival_times[i];
			curr_time = GetElapsedSeconds(cpu_counter);
			double time_diff = sent_time - curr_time; // - last_time_mark;
			if (time_diff > 0) {
				if (time_diff > 1.0) {
					time_spec.tv_sec = (int) time_diff;
					time_spec.tv_nsec = (time_diff - time_spec.tv_sec) * 1000000000;
				} else {
					time_spec.tv_sec = 0;
					time_spec.tv_nsec = time_diff * 1000000000;
				}
				nanosleep(&time_spec, NULL);
			}

			sprintf(file_name, "/tmp/temp/temp%d.dat", i + 1);
			file_id = sender->SendFile(file_name, timeout_ratio);
		}

		while (!sender->IsTransferFinished(file_id)) {
			usleep(2000);
		}

		sender->CollectExpResults();

		double transfer_time = GetElapsedSeconds(cpu_counter);
		double pho = sample.total_file_size * 8.0 / sample.total_time / (100 * 1000000.0);
		double throughput = sample.total_file_size / 1000000.0 / transfer_time * 8;
		sprintf(str,
				"Experiment Finished.\n\n***** Statistics *****\nTotal No. Files: %d\nTotal File Size: %d bytes\n"
						"Total Arrival Time Span: %.2f second\nPho Value: %.2f\nTotal Transfer Time: %.2f seconds\n"
						"Throughput: %.2f Mbps\n*****End of Statistics *****\n\n",
				FILE_COUNT, sample.total_file_size, sample.total_time, pho,
				transfer_time, throughput);
		sender_proxy->SendMessageLocal(INFORMATIONAL, str);
	}

	sleep(5);
}


void ExperimentManager2::HandleExpResults(string msg) {
	pthread_mutex_lock(&write_mutex);
	if (result_file.is_open()) {
		result_file << msg;
		result_file.flush();
	}
	pthread_mutex_unlock(&write_mutex);
}





void ExperimentManager2::StartExperiment(SenderStatusProxy* sender_proxy, VCMTPSender* sender) {
	// Randomly generate FILE_COUNT files for the sample
	sender_proxy->SendMessageLocal(INFORMATIONAL, "Generating files...\n");
	system("mkdir /tmp/temp");
	system("cp /users/jieli/src/file_sizes.txt /tmp/temp");
	system("cp /users/jieli/src/inter_arrival_times.txt /tmp/temp");
	File_Sample sample = GenerateFiles();
	system("sudo sync && sudo echo 3 > /proc/sys/vm/drop_caches");
	sender_proxy->SendMessageLocal(INFORMATIONAL, "Files generated.\n");


	sender_proxy->SendMessageLocal(INFORMATIONAL, "Sending files...\n");
	struct timespec time_spec;
	time_spec.tv_sec = 0;
	time_spec.tv_nsec = 0;

	CpuCycleCounter cpu_counter;
	AccessCPUCounter(&cpu_counter.hi, &cpu_counter.lo);
	double last_time_mark = 0.0;
	double sent_time = 0.0;
	double curr_time = 0.0;

	char file_name[256];
	int file_id = 0;
	char str[500];
	for (int i = 0; i < FILE_COUNT; i++) {
		if (i % 100 == 0) {
			sprintf(str, "Sending file %d", i);
			sender_proxy->SendMessageLocal(INFORMATIONAL, str);
		}

		sent_time += sample.inter_arrival_times[i];
		curr_time = GetElapsedSeconds(cpu_counter);
		double time_diff = sent_time - curr_time; // - last_time_mark;
		if (time_diff > 0) {
			if (time_diff > 1.0) {
				time_spec.tv_sec = (int)time_diff;
				time_spec.tv_nsec = (time_diff - time_spec.tv_sec) * 1000000000;
			}
			else {
				time_spec.tv_sec = 0;
				time_spec.tv_nsec = time_diff * 1000000000;
			}

			cout << "Wait for " << time_diff << " seconds" << endl;
			nanosleep(&time_spec, NULL);
		}

		sprintf(file_name, "/tmp/temp/temp%d.dat", i + 1);
		file_id = sender->SendFile(file_name);
	}

	while (!sender->IsTransferFinished(file_id)) {
		usleep(2000);
	}

	double transfer_time = GetElapsedSeconds(cpu_counter);
	double pho = sample.total_file_size * 8 / sample.total_time / (sender->GetSendRate() * 1000000.0);
	double throughput = sample.total_file_size * 8 / 1000000.0 / transfer_time;
	sprintf(str, "Experiment Finished.\n\n***** Statistics *****\nTotal No. Files: %d\nTotal File Size: %d bytes\n"
			"Total Arrival Time Span: %.2f second\nPho Value: %.2f\nTotal Transfer Time: %.2f seconds\n"
			"Throughput: %.2f Mbps\n*****End of Statistics *****\n\n",
			FILE_COUNT, sample.total_file_size, sample.total_time, pho, transfer_time, throughput);
	sender_proxy->SendMessageLocal(INFORMATIONAL, str);
}



// Randomly select and generate files from a given sample
File_Sample ExperimentManager2::GenerateFiles() {
	static const int BUF_SIZE = 4096;
	File_Sample sample;

	cout << "Genearating files..." << endl;
	ifstream fs_file("/tmp/temp/file_sizes.txt");
	int size = 0;
	vector<int> file_sizes;
	while (fs_file >> size) {
		file_sizes.push_back(size);
	}
	fs_file.close();

	// Randomly generate FILE_COUNT files from the sample
	srand(time(NULL));
	int file_index = 1;
	char file_name[50];
	char buf[BUF_SIZE];
	int total_size = 0;
	for (int i = 0; i < FILE_COUNT; i++) {
		int index = rand() % file_sizes.size();
		sample.file_sizes.push_back(file_sizes[index]);
		sample.total_file_size += file_sizes[index];
		total_size += file_sizes[index];

		// Generate the file
		int remained_size = file_sizes[index]; //* 100;
		sprintf(file_name, "/tmp/temp/temp%d.dat", file_index++);
		ofstream outfile (file_name, ofstream::binary | ofstream::trunc);
		while (remained_size > 0) {
			int data_len = (remained_size > BUF_SIZE) ? BUF_SIZE : remained_size;
			outfile.write(buf, data_len);
			remained_size -= data_len;
		}
		outfile.close();
	}
	cout << "Average file size: " << (total_size / FILE_COUNT) << " bytes" << endl;


	// Randomly generate FILE_COUNT inter-arrival times
	ifstream irt_file("/tmp/temp/inter_arrival_times.txt");
	double time;
	vector<double> inter_arrival_times;
	while (irt_file >> time) {
		inter_arrival_times.push_back(time);
	}
	irt_file.close();

	double total_time = 0.0;
	int count = 0;
	while (count < FILE_COUNT) {
		int index = rand() % inter_arrival_times.size();
		if (inter_arrival_times[index] > 1.0)
			continue;

		sample.inter_arrival_times.push_back(inter_arrival_times[index]);
		sample.total_time += inter_arrival_times[index];
		total_time += inter_arrival_times[index];
		count++;
	}
	cout << "Average inter-arrival time: " << (total_time / FILE_COUNT) << " second" << endl;
	return sample;
}

