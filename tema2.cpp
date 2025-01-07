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
#include <utility>
#include <algorithm>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100

using namespace std;

ofstream fout("debug1.out");

struct my_file {
    string file_name;
    vector<string> fileParts;
};

struct tracker_info {
    vector<int> seeds;
    vector<string> hashes;
};


vector<string> missing_files;
vector<my_file> files;

struct second_compare {
    bool operator()(const pair<int, int>& a, const pair<int, int>& b) {
        return a.second > b.second;
    }
};

void *download_thread_func(void *arg)
{
    int index = 0;
    int rank = *(int*) arg;
    for (string missing : missing_files) {
    //     // cout << "Peer " << rank << " missing file: " << missing << "\n";
        int code = 0;
        MPI_Send(&code, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD);
        MPI_Send(missing.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
        int num_seeds;
        MPI_Recv(&num_seeds, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        vector<int> effort;
        // cout << missing << " Rank:" << rank << " seeds:" << num_seeds << "\n";
        for (int i = 0; i < num_seeds; i++) {
            int seed;
            MPI_Recv(&seed, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            effort.push_back(seed);
            // cout << rank << " " << missing << " seed:" << seed<< "\n";
        }
        int num_hashes;
        vector<string> wanted_hashes;
        MPI_Recv(&num_hashes, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        for (int i = 0; i < num_hashes; i++) {
            char hash[HASH_SIZE + 1];
            MPI_Recv(hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            hash[HASH_SIZE] = '\0';
            wanted_hashes.push_back(string(hash));
            // cout << string(hash) << "\n";
        }
    //     // start requesting
        int total = wanted_hashes.size();
        int i = 0, local = 0;
        my_file new_file;
        files.push_back(new_file);
        files.back().file_name = missing;
    //     // cout << rank << " " << missing << "\n";
    //     // cout << total << "\n";
        while (i < total) {
            if (effort[index] == rank) {
                index = (index + 1) % effort.size();
                continue;
            }
            MPI_Send("NRequest", 9, MPI_CHAR, effort[index], 3, MPI_COMM_WORLD);
            MPI_Send(wanted_hashes[i].c_str(), HASH_SIZE, MPI_CHAR, effort[index], 3, MPI_COMM_WORLD);
            int rec;
            char response[15];
            MPI_Recv(response, 15, MPI_CHAR, effort[index], 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            response[14] = '\0';
            if (string(response) == "OK") {
                // cout << rank << " "<< "found part" << i << "\n";
                files.back().fileParts.push_back(wanted_hashes.at(i));
                i++;
                local++;
                // if (i == total)
                //     break;
            }  
            index = (index + 1) % effort.size();
            if (string(response) == "NOT OK") {
                // cout << "Trying again at index: " << index << "\n";
            }
            if (local == 10) {
                // int num;
                // int code = 2;
                // MPI_Send(&code, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD);
                // MPI_Send(missing.c_str(), MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 1, MPI_COMM_WORLD);
                // MPI_Recv(&num, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                // effort.clear();
                // // cout << rank << "\n";
                // for (int i = 0; i < num; i++) {
                //     int seed;
                //     MPI_Recv(&seed, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                //     effort.push_back(seed);
                // }
                // // cout << rank << " " << " " << missing << " " << i << "\n";
                // local = 0;
            }
        }
        int idx = 0;
        ofstream out(string("client" + to_string(rank) + "_" + missing));
        for (string hash : files.back().fileParts)  {
            idx++;
            if (idx != files.back().fileParts.size())
                out << hash << "\n";
            else
                out << hash;
        }
    }
    int code = 1;
    MPI_Send(&code, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD);
    return NULL;
}


void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    while (1) {
        MPI_Status status;
        char comm[9];
        MPI_Recv(comm, 9, MPI_CHAR, MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, &status);
        comm[9] = '\0';
        if (string(comm) == "Shutdown") {
            break;
        }
        char buf[HASH_SIZE+ 1];
        MPI_Recv(buf, HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        buf[HASH_SIZE] = '\0';
        string hash(buf);
        bool found = false;
        for (int i = 0; i < files.size(); i++) {
            for (string my_hash : files.at(i).fileParts) {
                if (my_hash == hash) {
                    MPI_Send("OK", 15, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
                    found = true;
                }
            }
        }
        if (not found) {
            MPI_Send("NOT OK", 15, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        }
    }
    return NULL;
}

void tracker(int numtasks, int rank) {
    map<string, struct tracker_info> database;
    for (int i = 1; i < numtasks; i++) {
        int num_files_to_read;
        MPI_Recv(&num_files_to_read, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        for (int index = 0; index < num_files_to_read; index++) {
            int size;
            MPI_Recv(&size, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            char buffer[MAX_FILENAME + 1];
            MPI_Recv(buffer, MAX_FILENAME, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buffer[MAX_FILENAME] = '\0';
            database[string(buffer)].seeds.push_back(i);
            int num_parts;
            MPI_Recv(&num_parts, 1, MPI_INT, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            for (int idx = 0; idx < num_parts; idx++) {
                char *buff = new char[HASH_SIZE + 1];
                MPI_Recv(buff, HASH_SIZE, MPI_CHAR, i, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                buff[HASH_SIZE] = '\0';
                if (database[string(buffer)].hashes.size() <= num_parts)
                    database[string(buffer)].hashes.push_back(string(buff));
            }
        }
    }

    for (int i = 1; i < numtasks; i++) {
        MPI_Send("ACK", 4, MPI_CHAR, i, 0, MPI_COMM_WORLD);
    }
    // for (auto it : database) {
    //     cout << it.first << "\n";
    //     cout << it.second.seeds.size() << "\n";
    //     cout << it.second.hashes.size() << "\n\n";
    // }
    
    int ctr = 0;
    while (true) {
        MPI_Status status;
        int code;
        MPI_Recv(&code, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        if (code == 1) { 
            ctr++;
            if (ctr == numtasks - 1) {
                for (int i = 1; i < numtasks; i++) {
                    MPI_Send("Shutdown", 9, MPI_CHAR, i, 3, MPI_COMM_WORLD);
                }
            break;
        }
            continue;
        }
        if (code == 2) {
            // char filename[MAX_FILENAME + 1];
            // MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            // filename[MAX_FILENAME] = '\0';
            // int x = static_cast<int>(database[string(filename)].seeds.size());
            // MPI_Send(&x, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            // for (int seed : database[string(filename)].seeds) {
            //     MPI_Send(&seed, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
            // }
            // continue;
        }
        char buffer[MAX_FILENAME + 1];
        MPI_Recv(buffer, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);
        buffer[MAX_FILENAME] = '\0';
        string to_be_added(buffer);
    //     // for (auto it : database) {
    //     //     fout << it.first << " ";
    //     // }
    //     // fout << "\n";
        int x = static_cast<int>(database[to_be_added].seeds.size());
        MPI_Send(&x, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        for (int seed : database[to_be_added].seeds) {
            MPI_Send(&seed, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        }
        int y = static_cast<int>(database[to_be_added].hashes.size());
        MPI_Send(&y, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        for (string hash : database[to_be_added].hashes) {
            MPI_Send(hash.c_str(), HASH_SIZE, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD);
        }
    // if (find(database[to_be_added].seeds.begin(), database[to_be_added].seeds.end(), status.MPI_SOURCE) == database[to_be_added].seeds.end()) {
    //    cout << to_be_added << " " << status.MPI_SOURCE << "\n";
    //    database[to_be_added].seeds.push_back(status.MPI_SOURCE);
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
        MPI_Send(file1.file_name.c_str(), size, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        int num_parts;
        fin >> num_parts;
        MPI_Send(&num_parts, 1, MPI_INT, TRACKER_RANK, 0, MPI_COMM_WORLD);
        for (int j = 0; j < num_parts; j++) {
            string part;
            fin >> part;
            MPI_Send(part.c_str(), HASH_SIZE, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
            file1.fileParts.push_back(part);
        }
        files.push_back(file1);
    }
    char *buf = new char[3];
    MPI_Recv(buf, 4, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD,MPI_STATUS_IGNORE);
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
