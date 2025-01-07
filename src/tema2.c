#include "utils.h"
#include "tracker.h"
#include "peer.h"
#include "download.h"

void *download_thread_func(void *arg)
{
    char buffer[BUFF_SIZE];
    int downloaded_segments = 0;
    MPI_Status mpi_status;
    size_t current_file_idx = 0;
    bool continue_downloading = true;
    srand(time(NULL)); // Seed the random number generator

    ClientFiles_t* client = (ClientFiles_t*)arg;
    size_t total_wanted_files = client->wanted_files_count;

    // Get the list of peers that have the files we want
    request_seeders_peers_list(client);

    // Keep downloading until all desired files are obtained
    while (continue_downloading && current_file_idx < total_wanted_files) {
        int available_peers = client->peers[current_file_idx].peers_count;

        // Move to the next file if no peers are available for the current one
        if (available_peers <= 0) {
            printf("No peers available for file index %zu\n", current_file_idx);
            current_file_idx++;
            continue;
        }

        // Choose a random peer to download from
        int selected_peer_idx = (available_peers > 1) ? rand() % available_peers : 0;
        PeerInfo_t* selected_peer = &client->peers[current_file_idx].peers_array[selected_peer_idx];

        char* file_name = client->wanted_files[current_file_idx].file_name;
        int file_id = atoi(&file_name[strlen(file_name) - 1]);

        bool segment_downloaded = false;

        // If the file isn't already owned, add it to our owned files
        if (!file_is_owned(client, file_id)) {
            add_file_to_owned(client, file_id);
        }

        FileData_t* current_file_data = find_file_data(client->owned_files, client->owned_files_count, file_id);
        assert(current_file_data != NULL); // Ensure we have the file data

        // Look for missing segments and attempt to download them
        for (size_t segment_idx = current_file_data->segment_count; segment_idx < selected_peer->segment_count; ++segment_idx) {
            FileSegment_t segment = selected_peer->segments[segment_idx];

            if (!has_segment(current_file_data, segment)) {
                // Request the missing segment from the selected peer
                if (MPI_Send(segment.hash, HASH_SIZE - 1, MPI_CHAR, selected_peer->peer_rank, REQUEST_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                    fprintf(stderr, "MPI_Send failed while requesting segment.\n");
                    continue;
                }

                // Wait for the peer's acknowledgment
                if (MPI_Recv(buffer, BUFF_SIZE, MPI_CHAR, selected_peer->peer_rank, ACK_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS) {
                    fprintf(stderr, "MPI_Recv failed while receiving acknowledgment.\n");
                    continue;
                }

                // If the peer is okay with sending the segment, add it to our data
                if (strcmp(buffer, "OK") == 0) {
                    add_segment_to_file_data(current_file_data, segment);
                    downloaded_segments++;
                    segment_downloaded = true;

                    // Switch to another peer to balance the load
                    break;
                }
            }
        }

        // If no segment was downloaded from the current peer, handle accordingly
        if (!segment_downloaded) {
            if (downloaded_segments > 0) {
                // Inform the tracker about the newly downloaded segments
                if (MPI_Send("DOWN_X", 8, MPI_CHAR, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS ||
                    MPI_Send(&current_file_data->file_id, 1, MPI_INT, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                    fprintf(stderr, "MPI_Send failed while informing tracker.\n");
                    // Consider adding more robust error handling here
                }

                // Send the hashes of the latest segments to the tracker
                for (size_t i = current_file_data->segment_count - 10; i < current_file_data->segment_count; ++i) {
                    if (MPI_Send(current_file_data->segments[i].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                        fprintf(stderr, "MPI_Send failed while sending segment hashes.\n");
                        // Consider adding more robust error handling here
                    }
                }

                downloaded_segments = 0;
            }

            // Save the downloaded file and move to the next one
            char output_file_name[18];
            sprintf(output_file_name, "client%d_file%d", client->client_rank, file_id);
            write_to_file(output_file_name, current_file_data);
            current_file_idx++;
        }

        // Periodically update the tracker after downloading every 10 segments
        if (downloaded_segments > 0 && downloaded_segments % 10 == 0) {
            if (MPI_Send("DOWN_10", 8, MPI_CHAR, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS ||
                MPI_Send(&current_file_data->file_id, 1, MPI_INT, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                fprintf(stderr, "MPI_Send failed while sending DOWN_10 message.\n");
                // Consider adding more robust error handling here
            }

            // Send the hashes of the latest 10 segments to the tracker
            for (size_t i = current_file_data->segment_count - 10; i < current_file_data->segment_count; ++i) {
                if (MPI_Send(current_file_data->segments[i].hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                    fprintf(stderr, "MPI_Send failed while sending segment hashes for DOWN_10.\n");
                    // Consider adding more robust error handling here
                }
            }

            downloaded_segments = 0;

            // Ask the tracker for an updated list of peers
            if (MPI_Send("GIVE_PEERS", 11, MPI_CHAR, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                fprintf(stderr, "MPI_Send failed while requesting peers.\n");
                // Consider adding more robust error handling here
            }

            // Wait for the tracker to acknowledge the peer list request
            if (MPI_Recv(buffer, BUFF_SIZE, MPI_CHAR, TRACKER_RANK, ACK_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS) {
                fprintf(stderr, "MPI_Recv failed while receiving peer list acknowledgment.\n");
                // Consider adding more robust error handling here
            }

            if (strcmp(buffer, "OK") == 0) {
                printf("Requested peers, client %d\n", client->client_rank);
            }
        }
    }

    // Let the tracker know that all downloads are complete
    if (MPI_Send("FINISHED_DOWN_ALL", 18, MPI_CHAR, TRACKER_RANK, INFORM_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
        fprintf(stderr, "MPI_Send failed while sending FINISHED_DOWN_ALL.\n");
        // Consider adding more robust error handling here
    }

    return NULL;
}

void *upload_thread_func(void *arg)
{
    char buffer[BUFF_SIZE];
    MPI_Status mpi_status;

    while (true) {
        // Wait for any upload requests from peers
        if (MPI_Recv(buffer, BUFF_SIZE, MPI_CHAR, MPI_ANY_SOURCE, REQUEST_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS) {
            fprintf(stderr, "MPI_Recv failed in upload thread.\n");
            continue;
        }

        // Check if the signal to stop uploading has been received
        if (strcmp(buffer, "STOP_UPLOADING") == 0) {
            break;
        }

        // Acknowledge the upload request
        if (MPI_Send("OK", 2, MPI_CHAR, mpi_status.MPI_SOURCE, ACK_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
            fprintf(stderr, "MPI_Send failed while sending ACK in upload thread.\n");
            // Consider adding more robust error handling here
        }
    }

    return NULL;
}

void tracker(TrackerDataSet_t* tracker_data) {
    int total_downloading_clients = 0;

    MPI_Status mpi_status;
    char buffer[BUFF_SIZE];
    int finished_clients = 0;
    bool continue_tracking = true;

    // Share file information with all clients
    send_peers_to_clients(tracker_data);

    // Count how many clients are actively downloading (not seeders)
    for (int i = 0; i < tracker_data->client_count; ++i) {
        if (tracker_data->data[i].client_type == SEEDER)
            continue;
        total_downloading_clients++;
    }

    // Keep tracking until all downloading clients have finished
    while (continue_tracking) {
        // Listen for messages from any client
        if (MPI_Recv(buffer, BUFF_SIZE, MPI_CHAR, MPI_ANY_SOURCE, INFORM_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS) {
            fprintf(stderr, "MPI_Recv failed in tracker.\n");
            continue;
        }

        // Handle different types of messages
        if (strcmp(buffer, "FINISHED_DOWN_ALL") == 0) {
            // Mark the client as a seeder now that it's finished downloading
            Client_Type_t* client_type = &tracker_data->data[mpi_status.MPI_SOURCE - 1].client_type;
            if (*client_type == PEER)
                *client_type = SEEDER;

            finished_clients++;
        }
        else if (strcmp(buffer, "DOWN_10") == 0 || strcmp(buffer, "DOWN_X") == 0) {
            int client_rank = mpi_status.MPI_SOURCE;
            update_tracker_swarm(tracker_data, client_rank, buffer);

            // Let the client know the tracker has processed their update
            if (MPI_Send("OK", 2, MPI_CHAR, client_rank, ACK_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                fprintf(stderr, "MPI_Send failed while sending ACK to client %d.\n", client_rank);
                // Consider adding more robust error handling here
            }
        }
        else if (strcmp(buffer, "GIVE_PEERS") == 0) {
            printf("Updated peers requested.\n");
            // You might want to handle peer list sending here
        }
        else {
            printf("Received unknown message: %s from client %d\n", buffer, mpi_status.MPI_SOURCE);
        }

        // If all clients have finished downloading, stop tracking
        if (finished_clients == total_downloading_clients) {
            printf("All downloading clients have finished. Ending tracking.\n");
            continue_tracking = false;
        }
    }

    // Instruct all non-leeching clients to stop uploading
    for (int rank = 1; rank <= tracker_data->client_count; ++rank) {
        if (tracker_data->data[rank - 1].client_type != LEECHER) {
            if (MPI_Send("STOP_UPLOADING", 15, MPI_CHAR, rank, REQUEST_TAG, MPI_COMM_WORLD) != MPI_SUCCESS) {
                fprintf(stderr, "MPI_Send failed while sending STOP_UPLOADING to client %d.\n", rank);
                // Consider adding more robust error handling here
            }
        }
    }
}

void peer(int numtasks, int rank, ClientFiles_t* client) {
    void *thread_status;
    int thread_result;
    pthread_t download_thread;
    pthread_t upload_thread;

    // Start the upload thread if the client is not a leech
    if (client->client_type != LEECHER) {
        thread_result = pthread_create(&upload_thread, NULL, upload_thread_func, NULL);
        if (thread_result) {
            fprintf(stderr, "Error creating upload thread.\n");
            exit(EXIT_FAILURE);
        }
    }

    // Start the download thread if the client is not a seeder
    if (client->client_type != SEEDER) {
        thread_result = pthread_create(&download_thread, NULL, download_thread_func, (void *) client);
        if (thread_result) {
            fprintf(stderr, "Error creating download thread.\n");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for the upload thread to finish if it was started
    if (client->client_type != LEECHER) {
        thread_result = pthread_join(upload_thread, &thread_status);
        if (thread_result) {
            fprintf(stderr, "Error joining upload thread.\n");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for the download thread to finish if it was started
    if (client->client_type != SEEDER) {
        thread_result = pthread_join(download_thread, &thread_status);
        if (thread_result) {
            fprintf(stderr, "Error joining download thread.\n");
            exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char *argv[]) {
    int numtasks, rank;
    int mpi_provided;

    // Initialize MPI with support for multiple threads
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &mpi_provided);
    if (mpi_provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI does not support required threading level.\n");
        exit(EXIT_FAILURE);
    }

    // Get the total number of MPI tasks and the rank of this process
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // Allocate memory for client and tracker data structures
    ClientFiles_t *client_file = (ClientFiles_t *)calloc(1, sizeof(ClientFiles_t));
    TrackerDataSet_t *tracker_data = (TrackerDataSet_t *)calloc(1, sizeof(TrackerDataSet_t));

    if (!client_file || !tracker_data) {
        fprintf(stderr, "Memory allocation failed.\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
    }

    if (rank == TRACKER_RANK) {
        // If this process is the tracker, handle tracking operations
        receive_data_from_clients(tracker_data, numtasks);
        tracker(tracker_data);
        free_tracker(tracker_data);
    } else {
        // For peer clients, handle downloading and uploading
        read_from_file(client_file, rank);
        send_data_to_tracker(client_file);

        // Wait for acknowledgment from the tracker before proceeding
        char ack_buffer[3] = {0};
        MPI_Status mpi_status;
        if (MPI_Recv(ack_buffer, 2, MPI_CHAR, TRACKER_RANK, ACK_TAG, MPI_COMM_WORLD, &mpi_status) != MPI_SUCCESS) {
            fprintf(stderr, "Failed to receive acknowledgment from tracker.\n");
            free_client_files(client_file);
            free(client_file);
            free(tracker_data);
            MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        }

        // Start peer operations
        peer(numtasks, rank, client_file);
        free_client_files(client_file);
    }

    // Clean up allocated memory
    free(client_file);
    free(tracker_data);

    // Finalize the MPI environment
    MPI_Finalize();

    return 0;
}
