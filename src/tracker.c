#include "tracker.h"

/**
 * Sends the list of peers and seeders to all clients at startup.
 */
void send_peers_to_clients(TrackerDataSet_t* m_tracker) {
    MPI_Status mpi_status;
    Swarm_t* swarms = m_tracker->swarms;

    // Iterate through all clients
    for(int i = 0; i < m_tracker->client_count; ++i){
        // Skip clients that are seeders
        if(m_tracker->data[i].client_type == SEEDER)
            continue;

        Client_Type_t client_type;
        // Receive the client type from any source
        if(MPI_Recv(&client_type, 1, MPI_INT, MPI_ANY_SOURCE, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Recv failed while receiving client type.\n");
            continue;
        }

        int client_rank = mpi_status.MPI_SOURCE;
        m_tracker->data[client_rank - 1].client_type = client_type;

        // Receive the number of files the client wants
        size_t wanted_file_count;
        if(MPI_Recv(&wanted_file_count, 1, MPI_UNSIGNED, client_rank, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Recv failed while receiving wanted file count.\n");
            continue;
        }

        // Allocate memory for the file IDs the client is interested in
        int* files_id = (int*)malloc(wanted_file_count * sizeof(int));
        if(!files_id){
            fprintf(stderr, "Memory allocation failed for files_id.\n");
            continue;
        }

        // Receive the actual file IDs
        if(MPI_Recv(files_id, wanted_file_count, MPI_INT, client_rank, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Recv failed while receiving file IDs.\n");
            free(files_id);
            continue;
        }

        // For each wanted file, send the relevant swarm information
        for(size_t j = 0; j < wanted_file_count; ++j){
            int wanted_swarm_id = files_id[j];

            // Validate the swarm ID
            if(wanted_swarm_id <= 0 || wanted_swarm_id > m_tracker->swarm_size){
                fprintf(stderr, "Invalid Swarm_t ID %d for client %d.\n", wanted_swarm_id, client_rank);
                continue;
            }

            Swarm_t* current_swarm = &swarms[wanted_swarm_id - 1];
            int in_swarm_count = current_swarm->clients_in_swarm_count;
            int* file_swarm = current_swarm->clients_in_swarm;

            // Send the number of clients in the swarm
            if(MPI_Send(&in_swarm_count, 1, MPI_INT, client_rank, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD) != MPI_SUCCESS ||
               MPI_Send(file_swarm, in_swarm_count, MPI_INT, client_rank, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD) != MPI_SUCCESS){
                fprintf(stderr, "MPI_Send failed while sending Swarm_t info for Swarm_t ID %d to client %d.\n", wanted_swarm_id, client_rank);
                continue;
            }

            // Send segment information for each client in the swarm
            for(int k = 0; k < in_swarm_count; ++k){
                int peer_rank = file_swarm[k];
                TrackerData_t* peer_data = &m_tracker->data[peer_rank - 1];

                // Find the file data corresponding to the wanted swarm ID
                FileData_t* peer_file = find_file_data(peer_data->files, peer_data->files_count, wanted_swarm_id);
                if(!peer_file){
                    fprintf(stderr, "Peer %d does not have file ID %d.\n", peer_rank, wanted_swarm_id);
                    continue;
                }

                // Send the number of segments and the peer's rank
                if(MPI_Send(&peer_file->segment_count, 1, MPI_UNSIGNED, client_rank, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD) != MPI_SUCCESS ||
                   MPI_Send(&peer_rank, 1, MPI_INT, client_rank, PEERS_SEEDERS_TRANSFER_TAG, MPI_COMM_WORLD) != MPI_SUCCESS){
                    fprintf(stderr, "MPI_Send failed while sending segment count and rank for peer %d.\n", peer_rank);
                    continue;
                }

                // Send each segment's hash
                for(int l = 0; l < peer_file->segment_count; ++l){
                    if(MPI_Send(peer_file->segments[l].hash, HASH_SIZE, MPI_CHAR, client_rank, HASH_TAG, MPI_COMM_WORLD) != MPI_SUCCESS){
                        fprintf(stderr, "MPI_Send failed while sending segment hash for peer %d.\n", peer_rank);
                    }
                }
            }
        }

        // Free the allocated memory for file IDs
        free(files_id);
    }
}

/**
 * Updates the tracker Swarm_t information based on client messages.
 */
void update_tracker_swarm(TrackerDataSet_t* m_tracker, int rank, char* buff){
    int file_id = 0;
    // Receive the file ID from the client
    if(MPI_Recv(&file_id, 1, MPI_INT, rank, INFORM_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
        fprintf(stderr, "MPI_Recv failed while receiving file ID from client %d.\n", rank);
        return;
    }

    // Check if the client already has the file; if not, add it
    if(!tracker_client_has_file(m_tracker, file_id, rank - 1))
        tracker_add_file_to_owned(m_tracker, file_id, rank - 1);

    // Retrieve the file data for the client
    FileData_t* client_file_data = find_file_data(m_tracker->data[rank - 1].files,
                                                  m_tracker->data[rank - 1].files_count,
                                                  file_id);
    if(!client_file_data){
        fprintf(stderr, "File ID %d not found for client %d after adding.\n", file_id, rank);
        return;
    }

    // Receive 10 segment hashes from the client
    for(size_t i = 0; i < 10; ++i){
        if(MPI_Recv(buff, BUFF_SIZE, MPI_CHAR, rank, INFORM_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Recv failed while receiving segment hash from client %d.\n", rank);
            continue;
        }
        // Copy the received hash into the client's file data
        strncpy(client_file_data->segments[client_file_data->segment_count].hash, buff, HASH_SIZE - 1);
        client_file_data->segments[client_file_data->segment_count].hash[HASH_SIZE - 1] = '\0'; // Ensure null-termination
        ++client_file_data->segment_count;
    }

    // Update the swarm information based on the new segments
    // The second parameter seems to indicate the total number of clients plus one
    create_file_swarms(m_tracker, m_tracker->client_count + 1);
}

/**
 * Receives data from all clients and initializes the tracker state.
 */
void receive_data_from_clients(TrackerDataSet_t* m_tracker, int numtasks) {
    m_tracker->client_count = numtasks - 1;
    // Allocate memory for tracker data based on the number of clients
    m_tracker->data = (TrackerData_t*)malloc(sizeof(TrackerData_t) * m_tracker->client_count);
    if(!m_tracker->data){
        fprintf(stderr, "Memory allocation failed for tracker data.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    int max_file_id = 0; // To determine the number of swarms

    // Iterate through each client to receive their data
    for(int rank = 1; rank < numtasks; ++rank) {
        int owned_files_count;

        // Receive the number of files owned by the client
        if(MPI_Recv(&owned_files_count, 1, MPI_INT, rank, HASH_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Recv failed while receiving owned file count from client %d.\n", rank);
            owned_files_count = 0; // Assume no files on failure
        }
        m_tracker->data[rank - 1].files_count = owned_files_count;
        m_tracker->data[rank - 1].rank = rank;

        // Receive the client type (seeder or leecher)
        if(MPI_Recv(&m_tracker->data[rank-1].client_type, 1, MPI_INT, rank, CLIENT_TYPE_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Recv failed while receiving client type from client %d.\n", rank);
            m_tracker->data[rank - 1].client_type = LEECHER; // Default to LEECHER on failure
        }

        // If the client owns no files, skip to the next client
        if(owned_files_count == 0){
            m_tracker->data[rank - 1].files = NULL;
            continue;
        }

        // Allocate memory for the client's files
        m_tracker->data[rank - 1].files = (FileData_t*)malloc(sizeof(FileData_t) * owned_files_count);
        if(!m_tracker->data[rank - 1].files){
            fprintf(stderr, "Memory allocation failed for client %d's files.\n", rank);
            m_tracker->data[rank - 1].files_count = 0;
            continue;
        }

        // Receive each file's data from the client
        for(int j = 0; j < owned_files_count; ++j) {
            FileData_t temp_file;
            memset(&temp_file, 0, sizeof(FileData_t));

            // Receive the file name
            if(MPI_Recv(temp_file.file_name, MAX_FILENAME, MPI_CHAR, rank, HASH_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
                fprintf(stderr, "MPI_Recv failed while receiving file name from client %d.\n", rank);
                continue;
            }
            // Copy the file name to the tracker's data
            strncpy(m_tracker->data[rank - 1].files[j].file_name, temp_file.file_name, MAX_FILENAME - 1);
            m_tracker->data[rank - 1].files[j].file_name[MAX_FILENAME - 1] = '\0'; // Ensure null-termination

            // Extract and set the file ID based on the file name's last character
            m_tracker->data[rank - 1].files[j].file_id = atoi(&temp_file.file_name[strlen(temp_file.file_name) - 1]);

            char* file_digit = &temp_file.file_name[strlen(temp_file.file_name) - 1];
            max_file_id = MAX(max_file_id, atoi(file_digit));

            // Receive the number of segments for this file
            if(MPI_Recv(&temp_file.segment_count, 1, MPI_UNSIGNED, rank, HASH_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
                fprintf(stderr, "MPI_Recv failed while receiving segment count from client %d.\n", rank);
                m_tracker->data[rank - 1].files[j].segment_count = 0;
                continue;
            }
            m_tracker->data[rank - 1].files[j].segment_count = temp_file.segment_count;

            // Receive each segment's hash
            for(int k = 0; k < temp_file.segment_count; ++k) {
                if(MPI_Recv(m_tracker->data[rank - 1].files[j].segments[k].hash, HASH_SIZE, MPI_CHAR, rank, HASH_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE) != MPI_SUCCESS){
                    fprintf(stderr, "MPI_Recv failed while receiving segment hash from client %d.\n", rank);
                    strcpy(m_tracker->data[rank - 1].files[j].segments[k].hash, ""); // Set to empty on failure
                }
            }
        }
    }

    // After receiving all clients' data, create swarms based on the maximum file ID
    m_tracker->swarm_size = max_file_id;
    if(m_tracker->swarm_size == 0){
        m_tracker->swarms = NULL;
        return;
    }

    create_file_swarms(m_tracker, numtasks);

    // Notify all clients that the tracker has successfully initialized
    for(int rank = 1; rank < numtasks; ++rank){
        if(MPI_Send("OK", 2, MPI_CHAR, rank, ACK_TAG, MPI_COMM_WORLD) != MPI_SUCCESS){
            fprintf(stderr, "MPI_Send failed while sending OK to client %d.\n", rank);
        }
    }
}

/**
 * Creates swarms for each file based on the tracker data.
 */
void create_file_swarms(TrackerDataSet_t* m_tracker, int numtasks) {
    // Allocate memory for all swarms based on the swarm size
    m_tracker->swarms = (Swarm_t*)malloc(sizeof(Swarm_t) * m_tracker->swarm_size);
    if(!m_tracker->swarms){
        fprintf(stderr, "Memory allocation failed for swarms.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    // Initialize each swarm with the corresponding file name
    for(int i = 0; i < m_tracker->swarm_size; ++i) {
        snprintf(m_tracker->swarms[i].file_name, MAX_FILENAME, "file%d", i + 1);
        m_tracker->swarms[i].clients_in_swarm = NULL;
        m_tracker->swarms[i].clients_in_swarm_count = 0;
    }

    // Populate each swarm with the ranks of clients that own the file
    for(int rank = 1; rank < numtasks; ++rank) {
        TrackerData_t* client_data = &m_tracker->data[rank - 1];

        for (int i = 0; i < client_data->files_count; ++i) {
            int file_id = client_data->files[i].file_id;
            // Validate the file ID
            if(file_id <= 0 || file_id > m_tracker->swarm_size){
                fprintf(stderr, "Invalid file ID %d for client %d.\n", file_id, rank);
                continue;
            }

            Swarm_t* current_swarm = &m_tracker->swarms[file_id - 1];
            int* swarm_count = &current_swarm->clients_in_swarm_count;

            // Allocate or reallocate memory to store client ranks in the swarm
            if(current_swarm->clients_in_swarm == NULL){
                current_swarm->clients_in_swarm = (int*)malloc(sizeof(int));
                if(!current_swarm->clients_in_swarm){
                    fprintf(stderr, "Memory allocation failed for Swarm_t %d.\n", file_id);
                    continue;
                }
                current_swarm->clients_in_swarm[0] = rank;
                (*swarm_count) = 1;
            }
            else{
                int* temp = (int*)realloc(current_swarm->clients_in_swarm, (*swarm_count + 1) * sizeof(int));
                if(!temp) {
                    fprintf(stderr, "Realloc failed for Swarm_t %d.\n", file_id);
                    continue;
                }
                current_swarm->clients_in_swarm = temp;
                current_swarm->clients_in_swarm[*swarm_count] = rank;
                (*swarm_count)++;
            }
        }
    }
}

/**
 * Checks if a client already has a specific file.
 */
bool tracker_client_has_file(TrackerDataSet_t* m_tracker, int file_id, int rank_index){
    // If the client has no files, return false
    if(m_tracker->data[rank_index].files_count == 0)
        return false;

    // Iterate through the client's files to check for the file ID
    for(int i = 0; i < m_tracker->data[rank_index].files_count; ++i){
        if(m_tracker->data[rank_index].files[i].file_id == file_id)
            return true;
    }

    return false;
}

/**
 * Adds a new file to the list of files owned by a client in the tracker.
 */
void tracker_add_file_to_owned(TrackerDataSet_t* m_tracker, int file_id, int rank_index){
    // Calculate the new file count after adding the file
    size_t new_files_count = m_tracker->data[rank_index].files_count + 1;

    FileData_t* updated_files = NULL;
    // Allocate or reallocate memory for the client's files
    if(m_tracker->data[rank_index].files == NULL){
        updated_files = (FileData_t*)malloc(sizeof(FileData_t) * new_files_count);
        if(!updated_files){
            fprintf(stderr, "Memory allocation failed while adding file to client %d.\n", m_tracker->data[rank_index].rank);
            return;
        }
    }
    else{
        updated_files = (FileData_t*)realloc(m_tracker->data[rank_index].files, sizeof(FileData_t) * new_files_count);
        if(!updated_files){
            fprintf(stderr, "Realloc failed while adding file to client %d.\n", m_tracker->data[rank_index].rank);
            return;
        }
    }

    // Update the tracker with the new file array and count
    m_tracker->data[rank_index].files = updated_files;
    m_tracker->data[rank_index].files_count = new_files_count;

    // Initialize the new file's data
    FileData_t* new_file = &m_tracker->data[rank_index].files[new_files_count - 1];
    memset(new_file, 0, sizeof(FileData_t));
    snprintf(new_file->file_name, MAX_FILENAME, "file%d", file_id);
    new_file->file_id = file_id;
    new_file->segment_count = 0;
}

/**
 * Frees all allocated memory within the tracker data structure.
 */
void free_tracker(TrackerDataSet_t* m_tracker) {
    if(!m_tracker)
        return;

    // Free each client's file data
    for (int i = 0; i < m_tracker->client_count; ++i) {
        if(m_tracker->data[i].files){
            free(m_tracker->data[i].files);
            m_tracker->data[i].files = NULL;
        }
    }

    // Free the swarm information
    if(m_tracker->swarms){
        for(int i = 0; i < m_tracker->swarm_size; ++i){
            if(m_tracker->swarms[i].clients_in_swarm){
                free(m_tracker->swarms[i].clients_in_swarm);
                m_tracker->swarms[i].clients_in_swarm = NULL;
            }
        }
        free(m_tracker->swarms);
        m_tracker->swarms = NULL;
    }

    // Free the main tracker data array
    if(m_tracker->data){
        free(m_tracker->data);
        m_tracker->data = NULL;
    }
}
