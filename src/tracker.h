#ifndef _TRACKER_H_
#define _TRACKER_H_

#include "utils.h"
#include "download.h"

void send_peers_to_clients(TrackerDataSet_t* m_tracker);

bool tracker_client_has_file(TrackerDataSet_t* m_tracker, int file_id, int rank);
void tracker_add_file_to_owned(TrackerDataSet_t* m_tracker, int file_id, int rank);


void update_tracker_swarm(TrackerDataSet_t* m_tracker, int rank, char* buff);


void receive_data_from_clients(TrackerDataSet_t* m_tracker, int numtasks);

void create_file_swarms(TrackerDataSet_t* m_tracker, int numtasks);

void free_tracker(TrackerDataSet_t* m_tracker);

#endif