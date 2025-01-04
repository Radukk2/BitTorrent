#include <iostream>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <mpi.h>
#include <vector>
#include <fstream>
#include <map>
#include <set>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

using namespace std;

struct my_file {
    string file_name;
    vector<string> fileParts;
};

struct tracker_info {
    vector<int> seeds, peers;
    set<string> hashes;
};


vector<string> missing_files;
vector<my_file> files;

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    for (string missing : missing_files) {
        MPI_Send(missing.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
        int num_seeds;
        MPI_Recv(&num_seeds, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, nullptr);
        cout <<rank << " "<<  num_seeds << "\n";
        // for (int i = 0; i < num_seeds; i++) {
        //     int seed;
        //     MPI_Recv(&seed, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD, nullptr);
        // }
    }
    const char* exitMessage = "EXIT";
    MPI_Send(exitMessage, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);

    return NULL;
}


void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;

    return NULL;
}

void tracker(int numtasks, int rank) {
    map<string, struct tracker_info> database;
    for (int i = 1; i < numtasks; i++) {
        int num_files_to_read;
        MPI_Recv(&num_files_to_read, 1, MPI_INT, i, MPI_ANY_TAG, MPI_COMM_WORLD, nullptr);
        for (int index = 0; index < num_files_to_read; index++) {
            int size;
            MPI_Recv(&size, 1, MPI_INT, i, 0, MPI_COMM_WORLD, nullptr);
            char* buffer = new char[size + 1];
            MPI_Recv(buffer, size, MPI_CHAR, i, 1, MPI_COMM_WORLD, nullptr);
            database[string(buffer)].seeds.push_back(i);
            int num_parts;
            MPI_Recv(&num_parts, 1, MPI_INT, i, 0, MPI_COMM_WORLD, nullptr);
            for (int idx = 0; idx < num_parts; idx++) {
                char* buff = new char[HASH_SIZE + 1];
                MPI_Recv(buff, HASH_SIZE, MPI_CHAR, i, 1, MPI_COMM_WORLD, nullptr);
                database[string(buffer)].hashes.insert(string(buff));
            }
        }
    }
    for (int i = 1; i < numtasks; i++)
        MPI_Send("ACK", 4, MPI_CHAR, i, 1, MPI_COMM_WORLD);
    
    int ctr = 0;
    while (true) {
        MPI_Status status;
        char* buffer = new char[MAX_FILENAME + 1];
        MPI_Recv(buffer, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        buffer[MAX_FILENAME] = '\0';
        string to_be_added(buffer);
        if (to_be_added == "EXIT") { 
            ctr++;
        }
        if (ctr == numtasks - 1) {
            break;
        }
        int x = static_cast<int>(database[to_be_added].seeds.size());
        MPI_Send(&x, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
        // for (int seed : database[to_be_added].seeds) {
        //     MPI_Send(&seed, 1, MPI_INT, status.MPI_SOURCE, 0, MPI_COMM_WORLD);
        // }
    }


}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    ifstream fin(string("test1/in" + to_string(rank) + ".txt"));
    string line;
    int num_files_to_read, num_files_to_write; 
    fin >> num_files_to_read;
    MPI_Send(&num_files_to_read, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
    for (int i = 0; i < num_files_to_read; i++) {
        my_file file1;
        fin >> file1.file_name;
        int size = static_cast<int>(file1.file_name.size());  
        MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(file1.file_name.c_str(), size, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
        int num_parts;
        fin >> num_parts;
        MPI_Send(&num_parts, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        for (int j = 0; j < num_parts; j++) {
            string part;
            fin >> part;
            MPI_Send(part.c_str(), HASH_SIZE, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
            file1.fileParts.push_back(part);
        }
        files.push_back(file1);
    }
    char *buf = new char[3];
    MPI_Recv(buf, 4, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD,nullptr);
    if (string(buf) != "ACK") {
        cerr << "ACK not received!";
        return;
    }
    fin >> num_files_to_write;
    for(int i = 0; i < num_files_to_write; i++) {
        string file_to_write;
        fin >> file_to_write;
        missing_files.push_back(file_to_write);
    }

    

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
