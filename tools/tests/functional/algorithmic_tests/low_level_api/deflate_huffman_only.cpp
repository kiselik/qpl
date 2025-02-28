/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#include "algorithm"
#include "../../../common/operation_test.hpp"
#include "ta_ll_common.hpp"
#include "source_provider.hpp"

namespace qpl::test {
constexpr uint64_t no_flag = 0;

class DeflateTestHuffmanOnly : public JobFixture {
protected:
    void SetUp() override {
        JobFixture::SetUp();

        uint32_t job_size = 0u;
        auto     status   = qpl_get_job_size(GetExecutionPath(), &job_size);
        ASSERT_EQ(QPL_STS_OK, status);

        job_buffer            = new uint8_t[job_size];
        decompression_job_ptr = reinterpret_cast<qpl_job *>(job_buffer);
        status                = qpl_init_job(GetExecutionPath(), decompression_job_ptr);
        ASSERT_EQ(QPL_STS_OK, status);
    }

    void TearDown() override {
        JobFixture::TearDown();

        qpl_fini_job(decompression_job_ptr);
        delete[] job_buffer;
    }

    uint8_t *job_buffer            = nullptr;
    qpl_job *decompression_job_ptr = nullptr;

    void RunHuffmanOnlyDynamicTest(bool is_big_endian = false, bool omit_verification = true) {
        // Variables
        uint32_t seed       = GetSeed();
        uint32_t total_out_ptr[2];


        for (auto &dataset: util::TestEnvironment::GetInstance().GetAlgorithmicDataset().get_data()) {
            uint32_t status = QPL_STS_OK;
            source = dataset.second;

            // TODO: resolve verification limitation:
            // Huffman Only verification currently does not work if buffer is larger than 4KB
            // therefore this workaround temporarilty truncates the source buffer if verify is enabled
            if (!omit_verification) {
                source.resize(4096);
            }

            destination.resize(source.size() * 2);
            std::fill(destination.begin(), destination.end(), 0u);
            std::vector<uint8_t> reference_buffer(destination.size(), 0u);
            const uint32_t file_size = (uint32_t) source.size();

            qpl_huffman_table_t c_huffman_table;
            status = qpl_huffman_only_table_create(compression_table_type,
                                                   GetExecutionPath(),
                                                   DEFAULT_ALLOCATOR_C,
                                                   &c_huffman_table);

            ASSERT_EQ(status, QPL_STS_OK) << "Table creation failed";

            job_ptr->huffman_table = c_huffman_table;
            job_ptr->flags = QPL_FLAG_FIRST |
                QPL_FLAG_LAST |
                QPL_FLAG_NO_HDRS |
                QPL_FLAG_GEN_LITERALS |
                QPL_FLAG_DYNAMIC_HUFFMAN |
                ((is_big_endian) ? QPL_FLAG_HUFFMAN_BE : no_flag) |
                ((omit_verification) ? QPL_FLAG_OMIT_VERIFY : no_flag);

            for (uint32_t count_exec = 0; count_exec < 2; count_exec++) {
                job_ptr->op = qpl_op_compress;
                job_ptr->next_in_ptr = source.data();
                job_ptr->next_out_ptr = destination.data();
                job_ptr->available_in = file_size;
                job_ptr->available_out = file_size * 2;
                job_ptr->total_in = 0;
                job_ptr->total_out = 0;
                job_ptr->last_bit_offset = 0;
                job_ptr->first_index_min_value = 0;
                job_ptr->crc = 0;

                // Compress
                status = run_job_api(job_ptr);
                ASSERT_EQ(QPL_STS_OK, status);
                total_out_ptr[count_exec] = job_ptr->total_out;
            }
            ASSERT_EQ(total_out_ptr[0], total_out_ptr[1]);

            qpl_huffman_table_t d_huffman_table;

            status = qpl_huffman_only_table_create(decompression_table_type,
                                                   GetExecutionPath(),
                                                   DEFAULT_ALLOCATOR_C,
                                                   &d_huffman_table);

            ASSERT_EQ(status, QPL_STS_OK) << "Table creation failed";

            status = qpl_huffman_table_init_with_other(d_huffman_table, c_huffman_table);

            ASSERT_EQ(QPL_STS_OK, status) << "Decompression table creation failed";

            decompression_job_ptr->op          = qpl_op_decompress;
            decompression_job_ptr->next_in_ptr = destination.data();

            decompression_job_ptr->next_out_ptr                   = reference_buffer.data();
            decompression_job_ptr->available_in                   = job_ptr->total_out;
            decompression_job_ptr->available_out                  = file_size;
            if (is_big_endian) {
                decompression_job_ptr->ignore_end_bits            = (16 - job_ptr->last_bit_offset) & 15;
            } else {
                decompression_job_ptr->ignore_end_bits            = (8 - job_ptr->last_bit_offset) & 7;
            }
            decompression_job_ptr->huffman_table                  = d_huffman_table;
            decompression_job_ptr->flags                          = QPL_FLAG_NO_HDRS |
                                                                       ((is_big_endian) ? QPL_FLAG_HUFFMAN_BE : no_flag);
            decompression_job_ptr->flags                         |= QPL_FLAG_FIRST | QPL_FLAG_LAST;

            // Decompress
            status = run_job_api(decompression_job_ptr);

            // IAA 1.0 limitation: cannot work if ignore_end_bits is greater than 7 bits for BE16 decompress. Expect error in this case.
            bool skip_verify = false;
            if (is_big_endian && qpl_path_hardware == job_ptr->data_ptr.path && decompression_job_ptr->ignore_end_bits > 7) {
                ASSERT_EQ(QPL_STS_HUFFMAN_BE_IGNORE_MORE_THAN_7_BITS_ERR, status);
                skip_verify = true;
            } else {
                ASSERT_EQ(QPL_STS_OK, status);
            }

            // Free resources
            status = qpl_huffman_table_destroy(c_huffman_table);
            ASSERT_EQ(QPL_STS_OK, status) << "Compression table destruction failed";

            status = qpl_huffman_table_destroy(d_huffman_table);
            ASSERT_EQ(QPL_STS_OK, status) << "Decompression table destruction failed";

            qpl_fini_job(job_ptr);
            qpl_fini_job(decompression_job_ptr);

            // Verify
            if (!skip_verify) {
                ASSERT_TRUE(CompareVectors(source, reference_buffer, file_size));
            }
        }
    }

    void RunHuffmanOnlyStaticTest(bool is_big_endian = false, bool omit_verification = true) {
        // Variables
        uint32_t seed       = GetSeed();
        uint32_t total_out_ptr[2];

        for (auto &dataset: util::TestEnvironment::GetInstance().GetAlgorithmicDataset().get_data()) {
            uint32_t status = QPL_STS_OK;
            source = dataset.second;

            // TODO: resolve verification limitation:
            // Huffman Only verification currently does not work if buffer is larger than 4KB
            // therefore this workaround temporarilty truncates the source buffer if verify is enabled
            if (!omit_verification) {
                source.resize(4096);
            }

            destination.resize(source.size() * 2);
            std::fill(destination.begin(), destination.end(), 0u);
            std::vector<uint8_t> reference_buffer(destination.size(), 0u);
            const uint32_t file_size = (uint32_t) source.size();

            qpl_huffman_table_t  c_huffman_table;
            status = qpl_huffman_only_table_create(compression_table_type,
                                                   GetExecutionPath(),
                                                   DEFAULT_ALLOCATOR_C,
                                                   &c_huffman_table);

            ASSERT_EQ(status, QPL_STS_OK) << "Table creation failed";

            // Building table
            job_ptr->op            = qpl_op_compress;
            job_ptr->next_in_ptr   = source.data();
            job_ptr->next_out_ptr  = destination.data();
            job_ptr->available_in  = file_size;
            job_ptr->available_out = file_size * 2;

            job_ptr->huffman_table = c_huffman_table;
            job_ptr->flags         = QPL_FLAG_FIRST |
                                     QPL_FLAG_LAST |
                                     QPL_FLAG_NO_HDRS |
                                     QPL_FLAG_GEN_LITERALS |
                                     QPL_FLAG_DYNAMIC_HUFFMAN |
                                     ((is_big_endian) ? QPL_FLAG_HUFFMAN_BE : no_flag) |
                                     ((omit_verification) ? QPL_FLAG_OMIT_VERIFY : no_flag);

            status = run_job_api(job_ptr);

            if (qpl_path_software == job_ptr->data_ptr.path) {
                ASSERT_EQ(QPL_STS_OK, status);
            } else {
                if (QPL_STS_OK != status) {
                    if (QPL_STS_INTL_VERIFY_ERR == status && is_big_endian) {
                        // Fix for HW issue in IAA 1.0 NO_HDR mode does not work for the case of BE16 compression.
                        // This is because we need to drop up to 15 bits, but we can only drop at most 7 bits.
                        // So in some cases, verify will fail in the BE16 case.
                        // The fix for this is too complicated, and this is really a niche operation.
                        // The recommendation is that we dont fix this, and add documentation that in this case
                        // (no headers and big-endian-16), verify not be used, and if the user wants to verify
                        // the output, they should (within the app) decompress the compressed buffer into a new
                        // buffer, and compare that against the original input.

                        std::cout << "Deflate verify stage failed with status: " << " " << status << "\n";
                        std::cout << "It is known issue for Huffman-only with BE16 format with IAA 1.0 - ignoring\n";
                    } else {
                        FAIL() << "Deflate status: " << status << "\n";
                    }
                }
            }

            // Now initialize compression job
            status = qpl_init_job(GetExecutionPath(), job_ptr);
            ASSERT_EQ(QPL_STS_OK, status);

            job_ptr->huffman_table = c_huffman_table;
            job_ptr->flags = QPL_FLAG_FIRST |
                QPL_FLAG_LAST |
                QPL_FLAG_NO_HDRS |
                QPL_FLAG_GEN_LITERALS |
                ((is_big_endian) ? QPL_FLAG_HUFFMAN_BE : no_flag) |
                ((omit_verification) ? QPL_FLAG_OMIT_VERIFY : no_flag);

            for (uint32_t count_exec = 0; count_exec < 2; count_exec++) {
                job_ptr->op = qpl_op_compress;
                job_ptr->next_in_ptr = source.data();
                job_ptr->next_out_ptr = destination.data();
                job_ptr->available_in = file_size;
                job_ptr->available_out = file_size * 2;
                job_ptr->total_in = 0;
                job_ptr->total_out = 0;
                job_ptr->last_bit_offset = 0;
                job_ptr->first_index_min_value = 0;
                job_ptr->crc = 0;

                status = run_job_api(job_ptr);

                if (qpl_path_software == job_ptr->data_ptr.path) {
                    ASSERT_EQ(QPL_STS_OK, status);
                }
                else {
                    if (QPL_STS_OK != status) {
                        if (QPL_STS_INTL_VERIFY_ERR == status && is_big_endian) {
                            // Fix for HW issue in IAA 1.0 NO_HDR mode does not work for the case of BE16 compression.
                            // This is because we need to drop up to 15 bits, but we can only drop at most 7 bits.
                            // So in some cases, verify will fail in the BE16 case.
                            // The fix for this is too complicated, and this is really a niche operation.
                            // The recommendation is that we dont fix this, and add documentation that in this case
                            // (no headers and big-endian-16), verify not be used, and if the user wants to verify
                            // the output, they should (within the app) decompress the compressed buffer into a new
                            // buffer, and compare that against the original input.

                            std::cout << "Deflate verify stage failed with status: " << " " << status << "\n";
                            std::cout << "It is known issue for Huffman-only with BE16 format with IAA 1.0 - ignoring\n";
                        }
                        else {
                            FAIL() << "Deflate status: " << status << "\n";
                        }
                    }
                }
                total_out_ptr[count_exec] = job_ptr->total_out;
            }
            ASSERT_EQ(total_out_ptr[0], total_out_ptr[1]);

            qpl_huffman_table_t d_huffman_table;

            status = qpl_huffman_only_table_create(decompression_table_type,
                                                   GetExecutionPath(),
                                                   DEFAULT_ALLOCATOR_C,
                                                   &d_huffman_table);

            ASSERT_EQ(status, QPL_STS_OK) << "Table creation failed";

            status = qpl_huffman_table_init_with_other(d_huffman_table, c_huffman_table);

            ASSERT_EQ(QPL_STS_OK, status) << "Decompression table creation failed";

            decompression_job_ptr->op          = qpl_op_decompress;
            decompression_job_ptr->next_in_ptr = destination.data();

            decompression_job_ptr->next_out_ptr                   = reference_buffer.data();
            decompression_job_ptr->available_in                   = job_ptr->total_out;
            decompression_job_ptr->available_out                  = file_size;
            if (is_big_endian) {
                decompression_job_ptr->ignore_end_bits            = (16 - job_ptr->last_bit_offset) & 15;
            } else {
                decompression_job_ptr->ignore_end_bits            = (8 - job_ptr->last_bit_offset) & 7;
            }
            decompression_job_ptr->huffman_table                  = d_huffman_table;
            decompression_job_ptr->flags                          = QPL_FLAG_NO_HDRS | ((is_big_endian) ? QPL_FLAG_HUFFMAN_BE : no_flag);
            decompression_job_ptr->flags                         |= QPL_FLAG_FIRST | QPL_FLAG_LAST;

            // Decompress
            status = run_job_api(decompression_job_ptr);

            // IAA 1.0 limitation: cannot work if ignore_end_bits is greater than 7 bits for BE16 decompress. Expect error in this case.
            bool skip_verify = false;
            if (is_big_endian && qpl_path_hardware == job_ptr->data_ptr.path && decompression_job_ptr->ignore_end_bits > 7) {
                ASSERT_EQ(QPL_STS_HUFFMAN_BE_IGNORE_MORE_THAN_7_BITS_ERR, status);
                skip_verify = true;
            } else {
                ASSERT_EQ(QPL_STS_OK, status);
            }

            // Free resources
            status = qpl_huffman_table_destroy(c_huffman_table);
            ASSERT_EQ(QPL_STS_OK, status) << "Compression table creation failed";

            status = qpl_huffman_table_destroy(d_huffman_table);
            ASSERT_EQ(QPL_STS_OK, status) << "Decompression table creation failed";

            qpl_fini_job(job_ptr);
            qpl_fini_job(decompression_job_ptr);

            // Verify
            if (!skip_verify) {
                ASSERT_TRUE(CompareVectors(source, reference_buffer, file_size));
            }
        }
    }

    // Huffman only compression on SW path was inefficient due to incorrect huffman table construction
    // ISAL routine to compute histogram for HT construction did not do huffman only
    // Manually computing the histogram with a for loop and then constructing HT works properly
    // This test checks (both paths, they should create same output) for correct compression in huffman only
    void RunHuffmanOnlyDynamicCorrectnessTest() {
        auto     execution_path = util::TestEnvironment::GetInstance().GetExecutionPath();

        const qpl_compression_levels compression_level = qpl_default_level;
        const uint32_t               source_size       = 1000;
        const uint32_t               destination_size  = source_size * 2;

        std::vector<uint8_t> source(source_size);
        std::vector<uint8_t> destination(destination_size);

        std::fill(source.begin(), source.end(), 5u);

        qpl_status status;

        // Allocate job structure
        uint32_t job_size = 0;
        status = qpl_get_job_size(execution_path, &job_size);
        ASSERT_EQ(QPL_STS_OK, status) << "Failed to get job size\n";

        auto job_buffer = std::make_unique<uint8_t[]>(job_size);
        auto job        = reinterpret_cast<qpl_job *>(job_buffer.get());

        // Initialize job structure for compression
        status = qpl_init_job(execution_path, job);
        ASSERT_EQ(QPL_STS_OK, status) << "Failed to initialize job\n";

        // Allocate compression Huffman table
        qpl_huffman_table_t c_huffman_table;
        status = qpl_huffman_only_table_create(compression_table_type, execution_path, DEFAULT_ALLOCATOR_C, &c_huffman_table);
        ASSERT_EQ(QPL_STS_OK, status) << "Failed to allocate compression table\n";

        // Fill in job structure for Huffman only compression
        job->op            = qpl_op_compress;
        job->level         = compression_level;
        job->next_in_ptr   = source.data();
        job->available_in  = source_size;
        job->next_out_ptr  = destination.data();
        job->available_out = destination_size;
        job->flags         = QPL_FLAG_FIRST | QPL_FLAG_LAST | QPL_FLAG_NO_HDRS | QPL_FLAG_GEN_LITERALS | QPL_FLAG_DYNAMIC_HUFFMAN | QPL_FLAG_OMIT_VERIFY;
        job->huffman_table = c_huffman_table;

        // Compress
        status = qpl_execute_job(job);
        EXPECT_EQ(QPL_STS_OK, status) << "Error in compression\n";

        // Get the size of compressed data
        const uint32_t compressed_size = job->total_out;

        // Free resources
        status = qpl_huffman_table_destroy(c_huffman_table);
        EXPECT_EQ(QPL_STS_OK, status) << "Compression table destruction failed\n";

        status = qpl_fini_job(job);
        ASSERT_EQ(QPL_STS_OK, status) << "Finishing job failed\n";

        // divide by 8 since source is 8 bit int and should be compressed to 1 bit
        ASSERT_EQ(compressed_size, 125) << "Compressed size was not equal to expected compressed size\n";
    }
};

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only, dynamic_le, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyDynamicTest();
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only, dynamic_be, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyDynamicTest(true);
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only, static_le, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyStaticTest();
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only, static_be, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyStaticTest(true);
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only_verify, dynamic_le, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyDynamicTest(false, false);
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only_verify, dynamic_be, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyDynamicTest(true, false);
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only_verify, static_le, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyStaticTest(false, false);
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only_verify, static_be, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyStaticTest(true, false);
}

QPL_LOW_LEVEL_API_ALGORITHMIC_TEST_F(huffman_only, dynamic_correct_single_value_source, DeflateTestHuffmanOnly) {
    RunHuffmanOnlyDynamicCorrectnessTest();
}

}
