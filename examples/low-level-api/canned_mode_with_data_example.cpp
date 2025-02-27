/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

//* [QPL_LOW_LEVEL_CANNED_MODE_EXAMPLE] */

#include <iostream>
#include <vector>
#include <memory>
#include <filesystem>
#include <fstream>
#include <stdexcept> // for runtime_error

#include "qpl/qpl.h"
#include "examples_utils.hpp" // for argument parsing function

/**
 * @brief This example requires a command line argument to set the execution path. Valid values are `software_path`
 * and `hardware_path`. This example also requires a second command line argument which specifies the dataset path.
 * In QPL, @ref qpl_path_software (`Software Path`) means that computations will be done with CPU.
 * Accelerator can be used instead of CPU. In this case, @ref qpl_path_hardware (`Hardware Path`) must be specified.
 * If there is no difference where calculations should be done, @ref qpl_path_auto (`Auto Path`) can be used to allow
 * the library to chose the path to execute. The Auto Path usage is not demonstrated by this example.
 *
 * @warning ---! Important !---
 * `Hardware Path` doesn't support all features declared for `Software Path`
 *
 */

auto main(int argc, char **argv) -> int {
    qpl_path_t execution_path = qpl_path_software;

    // Get path from input argument
    int extra_arg = 1;
    int parse_ret = parse_execution_path(argc, argv, &execution_path, extra_arg);
    if (parse_ret) {
        return 1;
    }

    std::string dataset_path = argv[2];

    // Source and output containers
    for (const auto &path: std::filesystem::directory_iterator(dataset_path)) {
        std::ifstream file(path.path().string(), std::ifstream::binary);

        if (!file.is_open()) {
            std::cout << "Couldn't open the file in " << dataset_path << std::endl;
            return 1;
        }

        std::vector<uint8_t> source;
        std::vector<uint8_t> destination;
        std::vector<uint8_t> reference;

        source.reserve(path.file_size());
        source.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        destination.resize(source.size() * 2);
        reference.resize(source.size());

        std::unique_ptr<uint8_t[]> job_buffer;
        qpl_status                 status;
        uint32_t                   size = 0;
        qpl_histogram              deflate_histogram{};

        // Job initialization
        status = qpl_get_job_size(execution_path, &size);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error acquired during job size getting.");
        }

        job_buffer = std::make_unique<uint8_t[]>(size);
        qpl_job *job = reinterpret_cast<qpl_job *>(job_buffer.get());
        status = qpl_init_job(execution_path, job);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error acquired during compression job initializing.");
        }

        // Huffman table initialization
        qpl_huffman_table_t huffman_table;

        status = qpl_deflate_huffman_table_create(combined_table_type,
                                                  execution_path,
                                                  DEFAULT_ALLOCATOR_C,
                                                  &huffman_table);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error acquired during Huffman table creation.");
        }

        // Filling deflate histogram first
        status = qpl_gather_deflate_statistics(source.data(),
                                               static_cast<uint32_t>(source.size()),
                                               &deflate_histogram,
                                               qpl_default_level,
                                               execution_path);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error acquired during gathering statistics for Huffman table.");
        }

        status = qpl_huffman_table_init_with_histogram(huffman_table, &deflate_histogram);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error acquired during Huffman table initialization.");
        }

        // Now perform canned mode compression
        job->op            = qpl_op_compress;
        job->level         = qpl_default_level;
        job->next_in_ptr   = source.data();
        job->next_out_ptr  = destination.data();
        job->available_in  = static_cast<uint32_t>(source.size());
        job->available_out = static_cast<uint32_t>(destination.size());
        job->flags         = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_CANNED_MODE | QPL_FLAG_OMIT_VERIFY;
        job->huffman_table = huffman_table;

        // Compression
        status = qpl_execute_job(job);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error while compression occurred.");
        }

        const uint32_t compressed_size = job->total_out;

        // Performing a decompression operation
        job->op            = qpl_op_decompress;
        job->next_in_ptr   = destination.data();
        job->next_out_ptr  = reference.data();
        job->available_in  = compressed_size;
        job->available_out = static_cast<uint32_t>(reference.size());
        job->flags         = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_CANNED_MODE;
        job->huffman_table = huffman_table;

        // Decompression
        status = qpl_execute_job(job);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error while decompression occurred.");
        }

        // Freeing resources
        status = qpl_huffman_table_destroy(huffman_table);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error while destroying table occurred.");
        }

        status = qpl_fini_job(job);
        if (status != QPL_STS_OK) {
            throw std::runtime_error("An error acquired during job finalization.");
        }

        // Compare reference functions
        for (size_t i = 0; i < source.size(); i++) {
            if (source[i] != reference[i]) {
                throw std::runtime_error("Content wasn't successfully compressed and decompressed.");
            }
        }

        std::cout << path.path().filename() << ": " << (float) source.size() / (float) compressed_size << std::endl;
        break;
    }

    return 0;
}

//* [QPL_LOW_LEVEL_CANNED_MODE_EXAMPLE] */
