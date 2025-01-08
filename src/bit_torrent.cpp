#include <mpi.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define ACK_SIZE 5

#define DOWNLOAD_REQ 500
#define UPDATE_REQ 501
#define REQ_FILE_INFO 502
#define FINISHED_FILE 503
#define FINISHED_ALL_FILES 504
#define STOP 505

struct file_t {
	string name;
	int chunks_no;
	vector<string> chunks;
};

struct file_tracker_t {
	vector<int> seeds;
	vector<int> peers;
	int chunks_no;
	vector<string> chunks_hashes;
};

pthread_mutex_t mutex;
vector<file_t> downloaded_files;
vector<file_t> owned_files;
vector<string> wanted_files;

void request_file_info(string& wanted_file, int& file_owners_no, vector<int>& file_owners,
										int& file_hashes_no, vector<string>& file_hashes)
{
	int request = REQ_FILE_INFO;
	MPI_Send(&request, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
	MPI_Send(wanted_file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

	// receive all file info from tracker
	MPI_Recv(&file_owners_no, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	for (int i = 0; i < file_owners_no; i++) {
		int owner;
		MPI_Recv(&owner, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		file_owners.push_back(owner);
	}

	MPI_Recv(&file_hashes_no, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);


	for (int i = 0; i < file_hashes_no; i++) { 
		char hash[HASH_SIZE];
		MPI_Recv(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		hash[HASH_SIZE] = '\0';

		file_hashes.push_back(hash);
	}
}

void update_request(string& wanted_file, int& file_owners_no, vector<int>& file_owners) {
	int request = UPDATE_REQ;
	MPI_Send(&request, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
	MPI_Send(wanted_file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

	// clear the list and save the new info received from tracker
	file_owners.clear();

	MPI_Recv(&file_owners_no, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	for (int i = 0; i < file_owners_no; i++) {
		int owner;
		MPI_Recv(&owner, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		file_owners.push_back(owner);
	}
}

void print_file(int& rank, string& wanted_file) {
	string out_file = "client";
	out_file.append(to_string(rank));
	out_file.append("_");
	out_file.append(wanted_file);

	ofstream fout(out_file);

	int downloaded_chunks_no = downloaded_files.back().chunks.size();
	for (int i = 0; i < downloaded_chunks_no; i++) {
		if (i == downloaded_chunks_no - 1)
			fout << downloaded_files.back().chunks[i];
		else
			fout << downloaded_files.back().chunks[i] << endl;
	}

	fout.close();
}

string request_chunk_download(string& wanted_file, string& hash, int& send_rank) {
	// send a download request type to a client and receive the ack
	int request = DOWNLOAD_REQ;
	MPI_Send(&request, 1, MPI_INT, send_rank, 1, MPI_COMM_WORLD);
	MPI_Send(wanted_file.c_str(), MAX_FILENAME, MPI_CHAR, send_rank, 1, MPI_COMM_WORLD);
	MPI_Send(hash.c_str(), HASH_SIZE, MPI_CHAR, send_rank, 1, MPI_COMM_WORLD);

	char ack[ACK_SIZE];

	MPI_Recv(ack, ACK_SIZE, MPI_CHAR, send_rank, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	return string(ack);
}

void download_chunks(int& rank, int& file_hashes_no, vector<string>& file_hashes, 
					string& wanted_file, int& file_owners_no, vector<int>& file_owners) {
	int client = 0;
	int cnt_seg = 10;

	// iterate through all hashes and try downloading them from the list of
	// peers/seeds received from tracker
	// select the client to download from one by one (round robin)
	for (int i = 0; i < file_hashes_no; i++) {
		// once in 10 downloaded chunks send an update request
		int downloaded_chunks_no = downloaded_files.back().chunks.size();
		if (downloaded_chunks_no == cnt_seg) {
			cnt_seg += 10;
			update_request(wanted_file, file_owners_no, file_owners);
		}

		// cycle through the list of clients
		client = client % file_owners.size();

		int send_rank = file_owners[client];
		if (send_rank == rank) {
			client++;
			i--;
			continue;
		}

		string hash = file_hashes[i];			
		string ack = request_chunk_download(wanted_file, hash, send_rank);

		// if chunk was found download it; else move to the next client
		if (ack == "ACK") {
			pthread_mutex_lock(&mutex);
			downloaded_files.back().chunks.push_back(file_hashes[i]);
			downloaded_files.back().chunks_no++;
			pthread_mutex_unlock(&mutex);
		} else {
			i--;
		}

		// move to the next client
		client++;
	}
}

void *download_thread_func(void *arg)
{
	int rank = *(int*) arg;

	// iterate through all the wanted files and download them
	for (auto wanted_file : wanted_files) {
		file_t new_file;
		new_file.name = wanted_file;
		new_file.chunks_no = 0;

		pthread_mutex_lock(&mutex);
		downloaded_files.push_back(new_file);
		pthread_mutex_unlock(&mutex);
	
		int file_owners_no;
		vector<int> file_owners;
		int file_hashes_no;
		vector<string> file_hashes;

		int request;

		// request file info from tracker
		request_file_info(wanted_file, file_owners_no, file_owners, file_hashes_no, file_hashes);

		// download chunks from other clients
		download_chunks(rank, file_hashes_no, file_hashes, wanted_file, file_owners_no, file_owners);

		// finished downloading a file; signal tracker
		request = FINISHED_FILE;
		MPI_Send(&request, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
		MPI_Send(wanted_file.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

		print_file(rank, wanted_file);
	}
	
	// finished downloading all files; signal tracker
	int request = FINISHED_ALL_FILES;
	MPI_Send(&request, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

	return NULL;
}

bool find_chunk(string file_str, string chunk_str) {
	// search for the chunk in the owned files
	for (auto owned_file : owned_files) {
		if (owned_file.name == file_str) {
			for (auto chunk : owned_file.chunks) {
				if (chunk_str == chunk) {
					return true;
				}
			}
			break;
		}
	}

	// search for the chunk in the downloaded files
	pthread_mutex_lock(&mutex);
	for (auto owned_file : downloaded_files) {
		if (owned_file.name == file_str) {
			for (auto chunk : owned_file.chunks) {
				if (chunk_str == chunk) {
					pthread_mutex_unlock(&mutex);
					return true;
				}
			}
			break;
		}
	}
	pthread_mutex_unlock(&mutex);

	return false;
}

void *upload_thread_func(void *arg)
{
	while (1) {
		char file[MAX_FILENAME];
		char file_chunk[HASH_SIZE];
		MPI_Status status;
		int request;

		// receive the request type
		MPI_Recv(&request, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);

		// stop the thread
		if (request == STOP)
			return NULL;

		MPI_Recv(file, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		MPI_Recv(file_chunk, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		
		string file_str(file);
		string chunk_str(file_chunk);
		string ack = "";

		int found = find_chunk(file_str, chunk_str);

		if (found)
			ack.append("ACK");
		else
			ack.append("NACK");

		MPI_Send(ack.c_str(), ACK_SIZE, MPI_CHAR, status.MPI_SOURCE, 2, MPI_COMM_WORLD);	
	}

	return NULL;
}

void receive_files_info(int numtasks, unordered_map<string, file_tracker_t>& file_records) {
	for (int task_rank = 1; task_rank < numtasks; task_rank++) {
		int files_no;

		MPI_Recv(&files_no, 1, MPI_INT, task_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		for (int i = 0; i < files_no; i++) {
			file_tracker_t file;
			char file_name[MAX_FILENAME];

			MPI_Recv(file_name, MAX_FILENAME, MPI_CHAR, task_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			MPI_Recv(&file.chunks_no, 1, MPI_INT, task_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			
			for (int i = 0; i < file.chunks_no; i++) {
				char hash[HASH_SIZE];
				MPI_Recv(hash, HASH_SIZE, MPI_CHAR, task_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				hash[HASH_SIZE] = '\0';
				file.chunks_hashes.push_back(hash);
			}

			// if the file was already added, add only the seed
			if (file_records.find(file_name) == file_records.end()) {
				file_records[file_name] = file;
			} 
			
			file_records[file_name].seeds.push_back(task_rank);
		}
	}
}

void send_file_info(MPI_Status& status, unordered_map<string, file_tracker_t>& file_records) {
	// receive the file that a client wants to download
	char wanted_file[MAX_FILENAME];
	MPI_Recv(wanted_file, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	string file_str(wanted_file);

	// add the client in peers list
	file_records[file_str].peers.push_back(status.MPI_SOURCE);

	// send the number of peers and seeds, then the lists
	int file_owners_no = file_records[file_str].peers.size() + file_records[file_str].seeds.size();	
	MPI_Send(&file_owners_no, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);

	for (auto peer : file_records[file_str].peers) {
		MPI_Send(&peer, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
	}

	for (auto seed : file_records[file_str].seeds) {
		MPI_Send(&seed, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
	}
	
	// send the number of hashes and the file's hashes
	MPI_Send(&file_records[file_str].chunks_no, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
	for (auto chunk_hash : file_records[file_str].chunks_hashes) {
		MPI_Send(chunk_hash.c_str(), HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
	}
}

void send_update(MPI_Status& status, unordered_map<string, file_tracker_t>& file_records) {
	char wanted_file[MAX_FILENAME];
	MPI_Recv(wanted_file, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	string file_str(wanted_file);

	int file_owners_no = file_records[file_str].peers.size() + file_records[file_str].seeds.size();
	
	// send the seeds and the peers again, updated
	MPI_Send(&file_owners_no, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);

	for (auto peer : file_records[file_str].peers) {
		MPI_Send(&peer, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
	}

	for (auto seed : file_records[file_str].seeds) {
		MPI_Send(&seed, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
	}
}

void update_seeds(MPI_Status& status, unordered_map<string, file_tracker_t>& file_records) {
	char wanted_file[MAX_FILENAME];
	MPI_Recv(wanted_file, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	
	// a client finished downloading a file; move client from peers to seeds
	auto it = find(file_records[wanted_file].peers.begin(), file_records[wanted_file].peers.end(), status.MPI_SOURCE);
	if (it != file_records[wanted_file].peers.end()) {
		file_records[wanted_file].seeds.push_back(status.MPI_SOURCE);
		file_records[wanted_file].peers.erase(it);
	}
}

void tracker(int numtasks, int rank) {
	unordered_map<string, file_tracker_t> file_records;
	
	// get info from all clients
	receive_files_info(numtasks, file_records);

	// send ACK to all clients after receiving all info
	for (int i = 1; i < numtasks; i++) {
		string ack = "ACK";
		MPI_Send(ack.c_str(), ACK_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD);

	}

	int cnt = numtasks - 1;
	while (1) {
		if (cnt == 0) {
			// all clients have finished downloading, stop all clients
			for (int i = 1; i < numtasks; i++) {
				int request = STOP;
				MPI_Send(&request, 1, MPI_INT, i, 1, MPI_COMM_WORLD);
			}
			return;
		}

		MPI_Status status;
		int request;

		// receive the request type
		MPI_Recv(&request, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

		if (request == REQ_FILE_INFO) {
			send_file_info(status, file_records);
		} else if (request == UPDATE_REQ) {
			send_update(status, file_records);
		} else if (request == FINISHED_FILE) {
			update_seeds(status, file_records);
		} else if (request == FINISHED_ALL_FILES) {
			cnt--;
		}
    }
}

void parse_input(int rank, int& owned_files_no) {
	string file = "in";
	file.append(to_string(rank));
	file.append(".txt");

	ifstream fin(file);
	
	fin >> owned_files_no;

	for (int i = 0; i < owned_files_no; i++) {
		file_t entry;
		fin >> entry.name;
		fin >> entry.chunks_no;

		for (int j = 0; j < entry.chunks_no; j++) {
			string file_chunk;
			fin >> file_chunk;
			entry.chunks.push_back(file_chunk);
		}

		owned_files.push_back(entry);
	}

	int wanted_files_no;
	fin >> wanted_files_no;

	for (int i = 0; i < wanted_files_no; i++) {
		string file;
		fin >> file;
		wanted_files.push_back(file);
	}

	fin.close();
}

void send_files_info(int owned_files_no) {
	// send the number of files, then all files;
	MPI_Send(&owned_files_no, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);

	for (auto file : owned_files) {
		MPI_Send(file.name.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

		// send the number of chunks, then all chunks;
		MPI_Send(&file.chunks_no, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
		for (auto chunk : file.chunks) {
			MPI_Send(chunk.c_str(), HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
		}
	}
}

void peer(int numtasks, int rank) {
	int owned_files_no;

	// open input file and extract data:
	// owned files, their chunks, wanted files
	parse_input(rank, owned_files_no);

	// send extracted infos to tracker
	send_files_info(owned_files_no);
	
	pthread_t download_thread;
	pthread_t upload_thread;
	void *status;
	int r;

	char ack[ACK_SIZE];
	
	// wait for tracker's ACK after receiving all info
	MPI_Recv(ack, ACK_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	
	r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
	if (r) {
		printf("Error creating the download thread\n");
		exit(-1);
	}

	r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
	if (r) {
		printf("Error creating the upload thread\n");
		exit(-1);
	}

	r = pthread_join(download_thread, &status);
	if (r) {
		printf("Error waiting for the download thread\n");
		exit(-1);
	}

	r = pthread_join(upload_thread, &status);
	if (r) {
		printf("Error waiting for the upload thread\n");
		exit(-1);
	}
}
 
int main (int argc, char *argv[]) {
	int numtasks, rank;

	pthread_mutex_init(&mutex, NULL);

	int provided;

	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);

	if (provided < MPI_THREAD_MULTIPLE) {
		fprintf(stderr, "MPI does not have support for multi-threading\n");
		exit(-1);
	}

	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	
	if (rank == TRACKER_RANK) {
		tracker(numtasks, rank);
	} else {
		peer(numtasks, rank);
	}
	
	pthread_mutex_destroy(&mutex);

	MPI_Finalize();
}