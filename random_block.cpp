/*******************************************************************************
 * random_block.cpp
 *
 * MPI Random Block Transmission Speed Test
 *
 * Copyright (C) 2018 Timo Bingmann <tb@panthema.net>
 *
 * All rights reserved. Published under the Boost Software License, Version 1.0
 ******************************************************************************/

#include <mpi.h>

#include <cassert>
#include <iostream>
#include <random>
#include <vector>

#include <thread_pool.hpp>

enum SendRecv { EMPTY, SEND, RECV };

struct Block {
    SendRecv op;
    char* buffer;
    size_t seq;
};

size_t block_size = 2 * 1024 * 1024;

size_t num_requests = 4;

size_t total_requests = 1000;
size_t remaining_requests = total_requests;
size_t seq = 0;

unsigned hosts, my_rank;

//! option to verify the transmitted data
static const bool g_check_data = false;

//! option to use MPI_Testany instead of MPI_Waitany (Testany is often slower?)
static const bool g_use_testany = false;

std::vector<Block> blocks;
// MPI_Request array for Testany
std::vector<MPI_Request> requests;

std::default_random_engine rnd(123456);

bool MaybeStartRequest(size_t r) {
    // pick next random send/recv pairs
    size_t s_rank = rnd() % hosts;
    size_t r_rank = rnd() % hosts;

    if (s_rank == r_rank)
        return false;

    // some processor pairs is going to do a request.
    --remaining_requests;
    ++seq;

    if (my_rank == s_rank) {
        // allocate block and fill with junk
        Block& buf = blocks[r];
        MPI_Request& req = requests[r];

        if (buf.op != EMPTY)
            abort();
        buf.op = SEND;
        buf.buffer = new char[block_size];
        if (buf.buffer == nullptr) {
            std::cout << "Out of memory" << std::endl;
            abort();
        }
        buf.seq = seq;

        size_t* sbuffer = reinterpret_cast<size_t*>(buf.buffer);
        for (size_t i = 0; i < block_size / sizeof(size_t); ++i)
            sbuffer[i] = i + seq;

        int r = MPI_Isend(buf.buffer, block_size, MPI_BYTE,
                          (int)r_rank, /* tag */ 0, MPI_COMM_WORLD, &req);
        if (r != 0)
            abort();

        return true;
    }
    else if (my_rank == r_rank) {
        // allocate block
        Block& buf = blocks[r];
        MPI_Request& req = requests[r];

        if (buf.op != EMPTY)
            abort();
        buf.op = RECV;
        buf.buffer = new char[block_size];
        if (buf.buffer == nullptr) {
            std::cout << "Out of memory" << std::endl;
            abort();
        }
        buf.seq = seq;

        int r = MPI_Irecv(buf.buffer, block_size, MPI_BYTE,
                          (int)s_rank, /* tag */ 0, MPI_COMM_WORLD, &req);
        if (r != 0)
            abort();

        return true;
    }

    return false;
}

void run_experiment()
{
    remaining_requests = total_requests;
    seq = 0;

    blocks.resize(num_requests);
    requests.resize(num_requests);

    for (size_t i = 0; i < num_requests; ++i)
        requests[i] = MPI_REQUEST_NULL;

    double ts_start = MPI_Wtime();

    ssize_t active = 0;

    tlx::ThreadPool pool(16);

    // perform first num_requests
    {
        while (active != num_requests && remaining_requests > 0) {
            if (MaybeStartRequest(active)) {
                ++active;
            }
        }
    }

    while (active != 0) {
        int out_index;
        if (g_use_testany) {
            int out_flag;
            int r = MPI_Testany(num_requests, requests.data(), &out_index,
                                &out_flag, MPI_STATUS_IGNORE);

            if (r != MPI_SUCCESS)
                abort();

            if (out_flag == 0)
                continue;
        }
        else {
            int r = MPI_Waitany(num_requests, requests.data(), &out_index,
                                MPI_STATUS_IGNORE);

            if (r != MPI_SUCCESS)
                abort();
        }

        assert(active > 0);
        --active;

        if (out_index >= 0 && out_index < (int)num_requests) {
            Block& buf = blocks[out_index];

            if (buf.op == SEND) {
                // std::cout << "Send " << out_index << " out_index." <<
                // std::endl;
                delete buf.buffer;
                buf.op = EMPTY;
            }
            else if (buf.op == RECV) {
                // std::cout << "Recv " << out_index << " out_index." <<
                // std::endl;
                size_t* sbuffer = reinterpret_cast<size_t*>(buf.buffer);

                if (g_check_data) {
                    pool.enqueue(
                        [sbuffer, seq = buf.seq]() {
                            for (size_t i = 0; i < block_size / sizeof(size_t); ++i) {
                                if (sbuffer[i] != i + seq) {
                                    std::cout << "Mismatch: got " << sbuffer[i]
                                              << " expected " << i + seq << std::endl;
                                    abort();
                                }
                            }
                            delete sbuffer;
                        });
                }
                else {
                    delete buf.buffer;
                }
                buf.op = EMPTY;
            }
            else {
                abort();
            }

            while (remaining_requests > 0) {
                if (MaybeStartRequest(out_index)) {
                    ++active;
                    break;
                }
            }
        }
        // std::cout << "active " << active << std::endl;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    double ts_end = MPI_Wtime();
    double ts_delta = ts_end - ts_start;

    uint64_t total_bytes = total_requests * block_size;

    std::cout << "RESULT"
              << " hosts=" << hosts
              << " block_size=" << block_size
              << " requests=" << num_requests
              << " total_requests=" << total_requests
              << " total_bytes=" << total_bytes
              << " ts=" << ts_delta
              << " bw=" << total_bytes / ts_delta / 1024 / 1024
              << std::endl;

    pool.loop_until_empty();
}

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    if (argc <= 1) {
        std::cout << argv[0]
                  << " <min-block_size [KB]> <min-block_size [KB]>"
                  << " <min-concurrent request>"
                  << " <max-concurrent request>"
                  << " <total bytes per PE [MB]>"
                  << std::endl;

        std::cout << "  Example: 8 8192 1 128 128"  << std::endl;

        MPI_Finalize();

        return 0;
    }

    MPI_Comm_size(MPI_COMM_WORLD, (int*)&hosts);
    MPI_Comm_rank(MPI_COMM_WORLD, (int*)&my_rank);

    unsigned min_block_size = atoi(argv[1]) * 1024;
    unsigned max_block_size = atoi(argv[2]) * 1024;

    unsigned min_requests = atoi(argv[3]);
    unsigned max_requests = atoi(argv[4]);

    unsigned request_factor = atoi(argv[5]) * 1024 * 1024;

    for (block_size = min_block_size; block_size <= max_block_size;
         block_size *= 2) {

        for (num_requests = min_requests; num_requests <= max_requests;
             num_requests *= 2) {

            // calculate total number of requests in experiment
            total_requests = request_factor * hosts / block_size;
            if (total_requests == 0)
                total_requests = 1;

            run_experiment();
        }
    }

    MPI_Finalize();

    return 0;
}

/******************************************************************************/
