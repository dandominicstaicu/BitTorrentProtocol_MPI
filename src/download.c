#include "download.h"

// Handles MPI errors by printing an error message and aborting the MPI environment.
static void handle_mpi_error(int error_code, const char* error_message) {
    if (error_code != MPI_SUCCESS) {
        fprintf(stderr, "MPI Error: %s\n", error_message);
        MPI_Abort(MPI_COMM_WORLD, error_code);
    }
}

// Sends the client type to the tracker.
// This tells the tracker whether the client is a SEEDER, PEER, or LEECHER.
static void send_client_type(Client_Type_t client_type) {
    int result = MPI_Send(&client_type, 1, MPI_INT, TRACKER_RANK,
                          PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD);
    handle_mpi_error(result, "Failed to send client_type to tracker");
}

// Sends the list of wanted file IDs to the tracker.
// First sends the number of wanted files, then the actual file IDs.
static void send_wanted_files(ClientFiles_t* client) {
    size_t count = client->wanted_files_count;

    // Inform the tracker how many files we want
    int mpi_result = MPI_Send(&count, 1, MPI_UNSIGNED, TRACKER_RANK,
                              PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD);
    handle_mpi_error(mpi_result, "Failed to send wanted_files_count to tracker");

    // Allocate memory to hold the file IDs
    int* file_ids = malloc(sizeof(int) * count);
    if (!file_ids && count > 0) {
        fprintf(stderr, "Error: Memory allocation failed for file_ids.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Extract the numeric file ID from each file name
    for (size_t i = 0; i < count; ++i) {
        const char* name = client->wanted_files[i].file_name;
        file_ids[i] = atoi(&name[strlen(name) - 1]); // Assumes file ID is the last character
    }

    // Send the array of file IDs to the tracker
    mpi_result = MPI_Send(file_ids, count, MPI_INT, TRACKER_RANK,
                          PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD);
    free(file_ids); // Free the allocated memory after sending
    handle_mpi_error(mpi_result, "Failed to send file IDs to tracker");
}

// Processes the client if it is a SEEDER.
// Since seeders already have the files, no additional info is sent.
static void process_seeder(ClientFiles_t* client) {
    // Seeder does not need to send any more information
}

// Processes the client if it is a PEER or LEECHER.
// Sends the list of wanted files to the tracker.
static void process_peer_or_leecher(ClientFiles_t* client) {
    send_wanted_files(client);
}

// Sends all necessary client information to the tracker based on the client type.
// This includes the client type and, if applicable, the list of wanted files.
static void send_client_information(ClientFiles_t* client) {
    send_client_type(client->client_type);
    if (client->client_type == PEER || client->client_type == LEECHER) {
        process_peer_or_leecher(client);
    } else if (client->client_type == SEEDER) {
        process_seeder(client);
    }
}

// Receives the count of peers/seeders in the swarm for a specific file from the tracker.
static int receive_in_swarm_count() {
    int count;
    MPI_Status status;

    // Get the number of peers/seeders from the tracker
    int result = MPI_Recv(&count, 1, MPI_INT, TRACKER_RANK,
                          PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &status);
    handle_mpi_error(result, "Failed to receive in_swarm_count");
    return count;
}

// Receives the ranks of peers in the swarm from the tracker.
static int* receive_ranks(int count) {
    // Allocate memory to hold the ranks
    int* ranks = malloc(sizeof(int) * count);
    if (!ranks && count > 0) {
        fprintf(stderr, "Error: Memory allocation failed for ranks_in_swarm.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    MPI_Status status;

    // Get the ranks array from the tracker
    int result = MPI_Recv(ranks, count, MPI_INT, TRACKER_RANK,
                          PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &status);
    handle_mpi_error(result, "Failed to receive ranks_in_swarm");
    return ranks;
}

// Receives the file segment hashes from a specific peer in the swarm.
static void receive_segments(int peer_rank, FileSegment_t* segments, size_t segment_count) {
    MPI_Status status;

    // Loop through each segment and receive its hash
    for (size_t i = 0; i < segment_count; ++i) {
        int result = MPI_Recv(segments[i].hash, HASH_SIZE, MPI_CHAR,
                              TRACKER_RANK, HASH_TAG, MPI_COMM_WORLD, &status);
        handle_mpi_error(result, "Failed to receive file segment hash");
    }
}

// Populates the peer information structure with the received data.
static void populate_peer_info(PeersList_t* peers_list, size_t file_idx,
                               int swarm_idx, int file_id, int peer_rank,
                               size_t segment_count, FileSegment_t* segments) {
    peers_list[file_idx].peers_array[swarm_idx].file_id = file_id;
    peers_list[file_idx].peers_array[swarm_idx].peer_rank = peer_rank;
    peers_list[file_idx].peers_array[swarm_idx].segment_count = segment_count;

    // Copy the received segments into the peer's segment list
    memcpy(peers_list[file_idx].peers_array[swarm_idx].segments,
           segments, sizeof(FileSegment_t) * segment_count);
}

// Receives and stores the swarm information for a specific wanted file.
static void receive_and_store_swarm_info(ClientFiles_t* client, size_t file_idx) {
    // Get the number of peers/seeders for this file
    int in_swarm = receive_in_swarm_count();

    // Get the ranks of the peers in the swarm
    int* ranks = receive_ranks(in_swarm);

    PeersList_t* peers_list = client->peers;
    peers_list[file_idx].peers_count = in_swarm;

    // Allocate memory for the peers array if there are peers in the swarm
    if (in_swarm > 0) {
        peers_list[file_idx].peers_array = malloc(sizeof(PeerInfo_t) * in_swarm);
        if (!peers_list[file_idx].peers_array) {
            fprintf(stderr, "Error: Memory allocation failed for peers_array.\n");
            free(ranks);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }
    }

    FileSegment_t temp_segments[MAX_CHUNKS]; // Temporary buffer for segments

    // Loop through each peer in the swarm to receive their segment information
    for (int i = 0; i < in_swarm; ++i) {
        size_t segment_count;
        MPI_Status status;

        // Receive the number of segments this peer has for the file
        int result = MPI_Recv(&segment_count, 1, MPI_UNSIGNED, TRACKER_RANK,
                              PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &status);
        handle_mpi_error(result, "Failed to receive segment_count");

        int peer_rank;

        // Receive the rank of the peer that owns these segments
        result = MPI_Recv(&peer_rank, 1, MPI_INT, TRACKER_RANK,
                          PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &status);
        handle_mpi_error(result, "Failed to receive peer rank from tracker");

        // Receive the actual segment hashes from the peer
        receive_segments(peer_rank, temp_segments, segment_count);

        // Extract the file ID from the file name (assumes last character is the ID)
        int file_id = atoi(&client->wanted_files[file_idx].file_name[
                strlen(client->wanted_files[file_idx].file_name) - 1]);

        // Populate the peer information with the received data
        populate_peer_info(peers_list, file_idx, i, file_id, peer_rank,
                           segment_count, temp_segments);
    }

    free(ranks); // Free the allocated memory for ranks after processing
}

// Receives swarm information for all wanted files of the client.
static void receive_all_swarm_info(ClientFiles_t* client) {
    // Iterate through each wanted file and receive its swarm information
    for (size_t i = 0; i < client->wanted_files_count; ++i) {
        receive_and_store_swarm_info(client, i);
    }
}

// Requests the list of seeders/peers from the tracker and stores the received information.
void request_seeders_peers_list(ClientFiles_t* client) {
    send_client_information(client);   // Send client type and wanted files to the tracker
    receive_all_swarm_info(client);    // Receive swarm information for all wanted files
}

// Checks if the client already owns a file with the given file_id.
// Returns true if owned, false otherwise.
bool file_is_owned(ClientFiles_t* client, int file_id) {
    for (size_t i = 0; i < client->owned_files_count; ++i) {
        if (client->owned_files[i].file_id == file_id) {
            return true;
        }
    }
    return false;
}

// Adds a new file to the client's owned_files array.
// Initializes the new file with zero segments.
void add_file_to_owned(ClientFiles_t* client, int file_id) {
    // Reallocate memory to accommodate the new file
    FileData_t* new_files = realloc(client->owned_files,
                                    sizeof(FileData_t) * (client->owned_files_count + 1));
    if (!new_files) {
        fprintf(stderr, "Error: Memory reallocation failed in add_file_to_owned.\n");
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    client->owned_files = new_files; // Update the owned_files pointer

    // Initialize the newly added file
    FileData_t* new_file = &client->owned_files[client->owned_files_count];
    snprintf(new_file->file_name, sizeof(new_file->file_name), "file%d", file_id);
    new_file->file_id = file_id;
    new_file->segment_count = 0;

    client->owned_files_count++; // Increment the count of owned files
}

// Adds a segment to the FileData_t's segments array if there is capacity.
// Returns true on success, false otherwise.
bool add_segment_to_file_data(FileData_t *data, const FileSegment_t seg) {
    if (data->segment_count >= MAX_CHUNKS) {
        return false; // No more space to add segments
    }

    // Copy the segment's hash into the next available slot
    strncpy(data->segments[data->segment_count].hash, seg.hash, HASH_SIZE);
    data->segment_count++; // Increment the segment count
    return true;
}

// Checks if the FileData_t already contains a specific segment by comparing hashes.
// Returns true if the segment exists, false otherwise.
bool has_segment(const FileData_t *data, const FileSegment_t seg) {
    for (size_t i = 0; i < data->segment_count; ++i) {
        if (strcmp(data->segments[i].hash, seg.hash) == 0) {
            return true; // Segment already exists
        }
    }
    return false; // Segment not found
}

// Finds and returns a pointer to the FileData_t with the specified file_id.
// Returns NULL if the file is not found.
FileData_t* find_file_data(FileData_t* f_data, size_t search_count, int file_id) {
    for (size_t i = 0; i < search_count; ++i) {
        if (f_data[i].file_id == file_id) {
            return &f_data[i]; // File found
        }
    }
    return NULL; // File not found
}

// Writes the segments of a FileData_t structure to a file, one hash per line.
// Flushes the output buffer after each write to ensure data integrity.
void write_to_file(const char* file_name, FileData_t* data) {
    if (!data || data->segment_count == 0) {
        fprintf(stderr, "Warning: write_to_file called with empty or null FileData_t for %s.\n",
                file_name);
    }

    // Open the file for writing
    FILE* out = fopen(file_name, "w");
    if (!out) {
        fprintf(stderr, "Error: Could not open file %s for writing.\n", file_name);
        return; // Exit the function gracefully if the file cannot be opened
    }

    // Write each segment's hash to the file
    for (size_t i = 0; i < data->segment_count; ++i) {
        fprintf(out, "%s\n", data->segments[i].hash);
        fflush(out); // Ensure the data is written immediately
    }

    fclose(out); // Close the file after writing
}
