# BitTorrent Protocol Simulation with MPI

### **Staicu Dan-Dominic 331CA**

## Overview

This project implements a simulation of the BitTorrent protocol for peer-to-peer file sharing using the Message Passing Interface (MPI). It models the decentralized file-sharing process, focusing on efficient distribution, segment-based file transfer, and dynamic peer collaboration.

## Features

- **BitTorrent Protocol Emulation**: Implements all the critical aspects of the BitTorrent protocol, ensuring adherence to its decentralized file-sharing principles.
- **Role-Based Peer Communication**:
    - **Seeder**: Provides complete file segments to peers for download.
    - **Peer**: Simultaneously uploads and downloads segments to and from the swarm.
    - **Leecher**: Focuses solely on downloading segments, starting without any file content.
- **Segmented File Transfer**: Divides files into multiple fixed-size segments for distributed downloads, increasing speed and efficiency.
- **Dynamic Peer Interaction**: Uses a centralized tracker to facilitate efficient peer discovery and dynamic updates to swarm lists during transfers.
- **Multi-threaded Execution**: Leverages threading to separate upload and download tasks for concurrent operations, mimicking real-world peer-to-peer interactions.
- **Fault Tolerance**: Ensures the simulation handles incomplete downloads and resumes effectively from interrupted states.

## Implementation Details

### Tracker Role

The tracker serves as a central coordinator for the file-sharing process. While it does not store file data, it plays an essential role in managing the swarm and ensuring peers connect efficiently.

Key tracker functionalities:

1. **Swarm Management**: Maintains up-to-date information about which peers have which file segments.
2. **Initial Setup**: Receives initial file ownership details from clients and registers them as seeds.
3. **Ongoing Coordination**: Provides updated swarm lists to requesting peers and adjusts roles dynamically as clients transition between leecher, peer, and seeder roles.

### Client Behavior

Clients represent individual participants in the BitTorrent swarm, contributing to the file-sharing network based on their assigned roles. They interact with the tracker to identify potential peers and engage in file-sharing tasks.

#### Initialization

1. Clients parse input files to determine:
    - Files they own and can upload.
    - Files they wish to download.
2. The list of owned files and segments is sent to the tracker for registration.

#### Download Thread

- The download thread coordinates segment acquisition:
    - Queries the tracker for the latest swarm information.
    - Sends requests to peers or seeds for required segments.
    - Ensures data integrity by validating segment hashes.
    - Updates the tracker periodically to include newly downloaded segments.

#### Upload Thread

- The upload thread manages incoming requests from other clients:
    - Responds to segment requests from peers.
    - Ensures efficient sharing by distributing segments equitably across peers.

### Efficiency Measures

The implementation incorporates several mechanisms to ensure efficient resource utilization and fairness:

1. **Load Balancing**: Downloads are evenly distributed across multiple peers to prevent overloading any single peer.
2. **Dynamic Updates**: Regular tracker updates help clients access new peers joining the swarm, ensuring minimal delays in locating required segments.
3. **Hash-Based Verification**: All received segments are verified using cryptographic hashes to prevent data corruption and ensure integrity.
4. **Round-Robin Requests**: Requests are distributed in a round-robin fashion among available peers to avoid reliance on a single node.

### Compilation Instructions

To compile the project, use the provided Makefile with the following:
```
make build
```