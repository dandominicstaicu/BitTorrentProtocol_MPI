#ifndef _UTILS_H_
#define _UTILS_H_

// * Macros
#define MAX(a, b) ((a > b) ? a : b)

// * Mpi TAGS
#define HASH_TAG 0
#define CLIENT_TYPE_TAG 1
#define ACK_TAG 2
#define PEERS_SEEDERS_TRANSFER_TAG 3
#define REQUEST_TAG 4
#define INFORM_TAG 5


#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define BUFF_SIZE 64

#include <mpi.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

// * Client Type Enum
typedef enum Client_Type_t {
    SEEDER = 0,
    PEER,
    LEECHER
} Client_Type_t;

// * File Segment Structure
typedef struct FileSegment_t {
    char hash[HASH_SIZE + 1];
} FileSegment_t;

// * File Data Structure
typedef struct FileData_t {
    char file_name[MAX_FILENAME];
    int file_id; // * ID of the file (e.g., file<file_id>)
    size_t segment_count;
    FileSegment_t segments[MAX_CHUNKS];
} FileData_t;

// * File Name Structure
typedef struct FileName_t {
    char file_name[MAX_FILENAME];
} FileName_t;


// * Swarm Structure
// * A Swarm_t for a file = all clients that own part of that file
// * swarms[0] = clients owning parts of file1, and so on
typedef struct Swarm_t {
    char file_name[MAX_FILENAME];
    int *clients_in_swarm;
    int clients_in_swarm_count;
} Swarm_t;

// * Peer Information Structure
typedef struct PeerInfo_t {
    int file_id; // * ID of the file (Swarm_t associated with file<file_id>)
    int peer_rank;
    size_t segment_count;
    FileSegment_t segments[MAX_CHUNKS];
} PeerInfo_t;


// * Tracker Data Structure
// * TrackerData_t[0] = data for Client 1, and so on
typedef struct TrackerData_t {
    int rank; // * Rank of the client
    size_t files_count;
    FileData_t *files; // * Files that the client owns
    Client_Type_t client_type;
} TrackerData_t;

// * Peers List Structure
typedef struct PeersList_t {
    PeerInfo_t *peers_array; // * Array of peers/seeders
    int peers_count;
} PeersList_t;

typedef struct TrackerDataSet_t {
    int client_count;
    TrackerData_t *data;
    Swarm_t *swarms; // * swarms for each file
    int swarm_size;
} TrackerDataSet_t;

// * Client Files Structure
typedef struct ClientFiles_t {
    int client_rank;
    size_t owned_files_count;
    FileData_t *owned_files;
    size_t wanted_files_count;
    FileName_t *wanted_files;
    PeersList_t *peers;
    Client_Type_t client_type;
} ClientFiles_t;


#endif
