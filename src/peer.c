#include "peer.h"

/* 
 * Helper function to handle MPI errors uniformly.
 * Aborts the program if an MPI call fails. 
 */
static void handle_mpi_error(int error_code, const char *error_message) {
    if (error_code != MPI_SUCCESS) {
        fprintf(stderr, "MPI Error: %s\n", error_message);
        MPI_Abort(MPI_COMM_WORLD, error_code);
    }
}

/*
 * Sends owned files information from the client to the tracker.
 * We do not change the function name or the name of the called functions.
 * We only renamed local variables and introduced new checks/comments.
 */
void send_data_to_tracker(ClientFiles_t *client) {
    /* 
     * Send the number of owned files to the tracker
     */
    int mpi_result = MPI_Send(&client->owned_files_count, 1, MPI_INT,
                              TRACKER_RANK, HASH_TAG, MPI_COMM_WORLD);
    handle_mpi_error(mpi_result, "Failed to send owned_files_count to tracker");

    /*
     * Send client type (SEEDER, PEER, or LEECHER)
     */
    mpi_result = MPI_Send(&client->client_type, 1, MPI_INT,
                          TRACKER_RANK, CLIENT_TYPE_TAG, MPI_COMM_WORLD);
    handle_mpi_error(mpi_result, "Failed to send client_type to tracker");

    /*
     * Now, for each owned file, send:
     * 1) file name
     * 2) number of segments
     * 3) each segment's hash
     */
    for (size_t file_idx = 0; file_idx < client->owned_files_count; ++file_idx) {
        /* Send file name */
        mpi_result = MPI_Send(client->owned_files[file_idx].file_name,
                              MAX_FILENAME,
                              MPI_CHAR,
                              TRACKER_RANK,
                              HASH_TAG,
                              MPI_COMM_WORLD);
        handle_mpi_error(mpi_result, "Failed to send file name to tracker");

        /* Send the segment count */
        size_t local_segment_count = client->owned_files[file_idx].segment_count;
        mpi_result = MPI_Send(&local_segment_count, 1, MPI_UNSIGNED,
                              TRACKER_RANK, HASH_TAG, MPI_COMM_WORLD);
        handle_mpi_error(mpi_result, "Failed to send segment_count to tracker");

        /* Send each segment's hash */
        for (size_t seg_idx = 0; seg_idx < local_segment_count; ++seg_idx) {
            mpi_result = MPI_Send(client->owned_files[file_idx].segments[seg_idx].hash,
                                  HASH_SIZE,
                                  MPI_CHAR,
                                  TRACKER_RANK,
                                  HASH_TAG,
                                  MPI_COMM_WORLD);
            handle_mpi_error(mpi_result, "Failed to send hash to tracker");
        }
    }
}

/* 
 * Helper function to safely open a file and handle errors.
 * Returns the file pointer if successful, otherwise exits the program.
 */
static FILE *safe_fopen(const char *path, const char *mode) {
    FILE *file_ptr = fopen(path, mode);
    if (!file_ptr) {
        fprintf(stderr, "Error: Could not open file %s\n", path);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    return file_ptr;
}

/* 
 * Helper function to trim the trailing newline from a string if it exists.
 */
static void trim_newline(char *str) {
    if (!str) return;
    size_t len = strlen(str);
    if (len == 0) return;
    if (str[len - 1] == '\n') {
        str[len - 1] = '\0';
    }
}

/* 
 * Reads the client's file data from an input file named "in<rank>.txt"
 * We do not change the function name or the name of the called functions.
 * We only renamed local variables and added new checks/comments.
 */
void read_from_file(ClientFiles_t *client, int rank) {
    /* Construct the file name (e.g., in2.txt, in3.txt, etc.) */
    char formatted_file_name[32]; /* Enough to hold "in_9999.txt" safely */
    sprintf(formatted_file_name, "in%d.txt", rank);

    /* Open the file safely */
    FILE *file_ptr = safe_fopen(formatted_file_name, "r");

    /* Buffer for reading lines */
    char read_buffer[BUFF_SIZE];

    /* Read the number of owned files */
    if (!fgets(read_buffer, BUFF_SIZE, file_ptr)) {
        fprintf(stderr, "Error: Could not read owned_files_count\n");
        fclose(file_ptr);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    client->owned_files_count = (size_t) atoi(read_buffer);

    /* Set the client rank */
    client->client_rank = rank;

    /* If the client has no owned files */
    if (client->owned_files_count == 0) {
        client->owned_files = NULL;
    } else {
        /* Allocate memory for the owned files */
        client->owned_files = (FileData_t *) malloc(sizeof(FileData_t) * client->owned_files_count);
        if (!client->owned_files) {
            fprintf(stderr, "Error: Memory allocation failed for owned_files\n");
            fclose(file_ptr);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        /* For each owned file, read its name and segment data */
        for (size_t file_idx = 0; file_idx < client->owned_files_count; ++file_idx) {
            if (!fgets(read_buffer, BUFF_SIZE, file_ptr)) {
                fprintf(stderr, "Error: Could not read owned file info (line %zu)\n", file_idx);
                fclose(file_ptr);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            /* Parse the file name and segment count */
            char *parsed_file_name = strtok(read_buffer, " ");
            if (!parsed_file_name) {
                fprintf(stderr, "Error: Invalid file name format\n");
                fclose(file_ptr);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            strcpy(client->owned_files[file_idx].file_name, parsed_file_name);

            /* Derive a numeric file id from the last character */
            client->owned_files[file_idx].file_id = atoi(&parsed_file_name[strlen(parsed_file_name) - 1]);

            /* Next token for the segment count */
            char *parsed_segment_count = strtok(NULL, "\n");
            if (!parsed_segment_count) {
                fprintf(stderr, "Error: Invalid segment count format\n");
                fclose(file_ptr);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }
            size_t local_segment_count = (size_t) atoi(parsed_segment_count);
            client->owned_files[file_idx].segment_count = local_segment_count;

            /* For each segment, read its hash */
            for (size_t seg_idx = 0; seg_idx < local_segment_count; ++seg_idx) {
                if (!fgets(read_buffer, BUFF_SIZE, file_ptr)) {
                    fprintf(stderr, "Error: Could not read segment hash (line %zu)\n", seg_idx);
                    fclose(file_ptr);
                    MPI_Abort(MPI_COMM_WORLD, 1);
                }
                /* Copy the hash string, ensuring we don't exceed HASH_SIZE */
                strncpy(client->owned_files[file_idx].segments[seg_idx].hash,
                        read_buffer,
                        HASH_SIZE);
                /* Null-terminate explicitly at HASH_SIZE */
                client->owned_files[file_idx].segments[seg_idx].hash[HASH_SIZE] = '\0';
            }
        }
    }

    /* Read the number of wanted files */
    if (!fgets(read_buffer, BUFF_SIZE, file_ptr)) {
        fprintf(stderr, "Error: Could not read wanted_files_count\n");
        fclose(file_ptr);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }
    client->wanted_files_count = (size_t) atoi(read_buffer);

    /* If the client wants no files */
    if (client->wanted_files_count == 0) {
        client->wanted_files = NULL;
    } else {
        /* Allocate memory for the wanted files */
        client->wanted_files = (FileName_t *) malloc(sizeof(FileName_t) * client->wanted_files_count);
        if (!client->wanted_files) {
            fprintf(stderr, "Error: Memory allocation failed for wanted_files\n");
            fclose(file_ptr);
            MPI_Abort(MPI_COMM_WORLD, 1);
        }

        /* Read the names of the wanted files */
        for (size_t want_idx = 0; want_idx < client->wanted_files_count; ++want_idx) {
            if (!fgets(read_buffer, BUFF_SIZE, file_ptr)) {
                fprintf(stderr, "Error: Could not read wanted file info (line %zu)\n", want_idx);
                fclose(file_ptr);
                MPI_Abort(MPI_COMM_WORLD, 1);
            }

            /* Trim the newline to avoid strange prints */
            trim_newline(read_buffer);

            /* Copy the wanted file name safely */
            strncpy(client->wanted_files[want_idx].file_name,
                    read_buffer,
                    sizeof(client->wanted_files[want_idx].file_name) - 1);
            client->wanted_files[want_idx].file_name[sizeof(client->wanted_files[want_idx].file_name) - 1] = '\0';
        }
    }

    /* Initialize the peers array for the wanted files */
    client->peers = (PeersList_t *) calloc(client->wanted_files_count, sizeof(PeersList_t));
    if (!client->peers && client->wanted_files_count > 0) {
        fprintf(stderr, "Error: Memory allocation failed for peers list\n");
        fclose(file_ptr);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    /* Determine client type */
    if (client->owned_files && client->wanted_files)
        client->client_type = PEER;
    else if (client->owned_files && !client->wanted_files)
        client->client_type = SEEDER;
    else
        client->client_type = LEECHER;

    /* Close the file */
    fclose(file_ptr);
}

/*
 * Frees allocated memory associated with the ClientFiles_t structure.
 * We do not change the function name or called functions.
 * Only added comments and local variables if needed.
 */
void free_client_files(ClientFiles_t *cf) {
    /* Free the owned_files array */
    if (cf->owned_files) {
        free(cf->owned_files);
        cf->owned_files = NULL;
    }

    /* Free the wanted_files array */
    if (cf->wanted_files) {
        free(cf->wanted_files);
        cf->wanted_files = NULL;
    }

    /* Free the peers array and its subarrays */
    if (cf->peers) {
        for (size_t i = 0; i < cf->wanted_files_count; ++i) {
            if (cf->peers[i].peers_array) {
                free(cf->peers[i].peers_array);
                cf->peers[i].peers_array = NULL;
            }
        }

        free(cf->peers);
        cf->peers = NULL;
    }
}
