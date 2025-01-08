### © 2025 Gabriel-Claudiu TINTU

# BitTorrent

- [Description](#Description)
- [Input](#Input)
- [Client](#Client)

    - [Download](#Download)
    - [Upload](#Upload)

- [Tracker](#Tracker)


---

## Description

This project simulates the BitTorrent protocol, a peer-to-peer protocol used for sharing files over the internet in a decentralized way. Users send and receive files using a BitTorrent client on their computer, which connects to a tracker. The tracker provides a list of available files and helps users find others from whom they can download the desired files.


## Client

Each client starts by processing its input file. The file contains a list of owned files and their chunks and the files the client wants to download. The owned files are sent to the tracker.

The clients wait for a response from the tracker, after which they proceed to download/upload using pthreads.

#### Download

In the download thread the client iterates through the files it wants to download and, for each one:

- requests file information from the tracker, such as the swarm, the number of chunks, and chunk hashes;
- for each chunk hash, cycles through the swarm retrieved from the tracker (round robin);
- every 10 chunks downloaded, the client sends an update request to the tracker (if any new clients have joined the swarm);
- after downloading a file, prints it and signals the tracker;
- once all files were downloaded stops the download thread and signals the tracker;

#### Upload

In the upload thread, two types of request can be received:

- from the tracker - **STOP** - stop the thread;
- from another client - **a download request** - the client receives a hash and searches for it in its own files or downloaded ones, if the hash was found send and **ACK** back to the client (simulating the chunk download);


## Tracker

The tracker first receives the list of owned files from all clients and stores the seeds, the number of chunks, and the chunk hashes for each file.
After receiving all the information, the tracker sends an **ACK** to all clients, enabling them to start communication.

The tracker can receive different types of requests:

- **request for file information** -- sends the file's swarm and the chunk hashes;
- **request for update** -- sends the files swarm;
- **request for finished file** -- updates the file's swarm by moving the client from peers to seeds;
- **request for finished files** -- decrement a counter, when the counter is 0 send a **STOP** request to all clients to end the upload thread;