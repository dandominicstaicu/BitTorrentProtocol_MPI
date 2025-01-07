#ifndef _DOWNLOAD_H_
#define _DOWNLOAD_H_

#include "utils.h"

void request_seeders_peers_list(ClientFiles_t* client);

bool file_is_owned(ClientFiles_t* client, int file_id);

void add_file_to_owned(ClientFiles_t* client, int file_id);

bool has_segment(const FileData_t *data, const FileSegment_t seg);

bool add_segment_to_file_data(FileData_t *data, const FileSegment_t seg);

FileData_t* find_file_data(FileData_t* f_data, size_t search_count, int file_id);

void write_to_file(const char* file_name, FileData_t* data);


#endif