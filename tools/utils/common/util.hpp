/*******************************************************************************
 * Copyright (C) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 ******************************************************************************/

#ifndef QPL_TEST_UTIL_HPP
#define QPL_TEST_UTIL_HPP

#include <cstdlib>
#include <cstdint>
#include <vector>
#include <fstream>

#include "qpl/qpl.h"
#include "hw_status.h"

#define HIGH_BIT_MASK 0x80
#define BYTE_BIT_LENGTH 8u

namespace qpl::test
{
    static std::vector<hw_compression_level> hw_levels{
        hw_compression_level::HW_NONE,
        hw_compression_level::SMALL,
        hw_compression_level::LARGE
    };

    static std::vector<sw_compression_level> sw_levels{
        sw_compression_level::SW_NONE, 
        sw_compression_level::LEVEL_0,
        sw_compression_level::LEVEL_1,
        sw_compression_level::LEVEL_2,
        sw_compression_level::LEVEL_3,
        sw_compression_level::LEVEL_4,
        sw_compression_level::LEVEL_9
    };

    static constexpr size_t   max_input_bit_width = 32;
    static constexpr uint32_t bit_bit_width = 1;
    static constexpr uint32_t additional_bytes_for_compression = 100;

    constexpr uint32_t max_bit_index = 7;
    constexpr uint32_t bit_to_byte_shift_offset = 3;
    constexpr uint32_t bib_eobs_bfinal_block_size = 3857;   // Every (except for the last one) has exactly this size in bytes
    constexpr uint32_t bib_eobs_decompressed_size = 111261; // Originally compressed file was bib, so assign bib file's size to this variable

    enum source_sizes_sequence_t
    {
        generic
    };

    // Status printer
    class hw_accelerator_status
    {
    public:
        hw_accelerator_status(::hw_accelerator_status status) : _status{status} {}

        operator ::hw_accelerator_status() const
        {
            return _status;
        }
    private:
        ::hw_accelerator_status _status;
    };

    static std::ostream & operator<<(std::ostream &strm, hw_accelerator_status const &status_ex)
    {
        ::hw_accelerator_status status = status_ex;
        strm << status;
        switch(status)
        {
        case HW_ACCELERATOR_STATUS_OK:
            strm << ": HW_ACCELERATOR_STATUS_OK";
            break;
        case HW_ACCELERATOR_NULL_PTR_ERR:
            strm << ": HW_ACCELERATOR_NULL_PTR_ERR";
            strm << " - null pointer error";
            break;
        case HW_ACCELERATOR_LIBACCEL_NOT_FOUND:
            strm << ": HW_ACCELERATOR_LIBACCEL_NOT_FOUND";
            strm << " - proper version of libaccel-config.so.1 was not found in the /usr/lib64";
            break;
        case HW_ACCELERATOR_LIBACCEL_ERROR:
            strm << ": HW_ACCELERATOR_LIBACCEL_ERROR";
            strm << " - libaccel-config internal error";
            break;
        case HW_ACCELERATOR_WORK_QUEUES_NOT_AVAILABLE:
            strm << ": HW_ACCELERATOR_WORK_QUEUES_NOT_AVAILABLE";
            strm << " - No enabled shared WQ";
            break;
        case HW_ACCELERATOR_SUPPORT_ERR:
            strm << ": HW_ACCELERATOR_SUPPORT_ERR";
            strm << " - System doesn't support accelerator";
            break;
        case HW_ACCELERATOR_WQ_IS_BUSY:
            strm << ": HW_ACCELERATOR_WQ_IS_BUSY";
            strm << " - Work queue is busy with task processing";
            break;
        }
        return strm;
    }

    static uint32_t bits_to_bytes(uint32_t bits_count) {
        uint32_t bytes_count = (bits_count + max_bit_index) >> bit_to_byte_shift_offset;
        return bytes_count;
    }

    static std::string OperationToString(qpl_operation operation)
    {
        switch (operation)
        {
            case qpl_op_scan_eq:
                return "ScanEQ";

            case qpl_op_scan_ne:
                return "ScanNE";

            case qpl_op_scan_lt:
                return "ScanLT";

            case qpl_op_scan_le:
                return "ScanLE";

            case qpl_op_scan_gt:
                return "ScanGT";

            case qpl_op_scan_ge:
                return "ScanGE";

            case qpl_op_scan_range:
                return "ScanRange";

            case qpl_op_scan_not_range:
                return "ScanNotRange";

            case qpl_op_extract:
                return "Extract";

            case qpl_op_select:
                return "Select";

            case qpl_op_expand:
                return "Expand";

            case qpl_op_compress:
                return "Compress";
            
            case qpl_op_decompress:
                return "Decompress";

            case qpl_op_crc64:
                return "CRC";

            default:
                return "";
        }
    }

    static std::string ParserToString(qpl_parser parser)
    {
        switch (parser)
        {
            case qpl_p_le_packed_array:
                return "LE";

            case qpl_p_be_packed_array:
                return "BE";

            case qpl_p_parquet_rle:
                return "PRLE";

            default:
                return "";
        }
    }

    static uint8_t *set_format_count(uint32_t format, uint32_t count, uint8_t *destination_ptr)
    {
        uint8_t value;

        value                = (uint8_t) format & 1;    // format is 1 low bit
        value |= (uint8_t) (count & 0x3f) << 1;         // get 6 significant bits of count
        count >>= 6;                                    // remove these 6 bits from count
        if (0 < count)                                  // if there are more bits
        {
            value |= HIGH_BIT_MASK;              // mark var-int val
            *(destination_ptr++) = value;        // put 1st byte to dst
            value = (uint8_t) (count & 0x7f);    // get next bits from count
            count >>= 7;                         // remove these 7 bits from count
        } else
        {
            *(destination_ptr++) = value;    // put last byte to dst
            return destination_ptr;
        }
        if (0 < count) // if there are more bits
        {
            value |= HIGH_BIT_MASK;              // mark var-int val
            *(destination_ptr++) = value;        // put 2nd byte to dst
            value = (uint8_t) (count & 0x7f);    // get next bits from count
            count >>= 7;                         // remove these 7 bits from count
        } else
        {
            *(destination_ptr++) = value;    // put last byte to dst
            return destination_ptr;
        }
        if (0 < count) // if there are more bits
        {
            value |= HIGH_BIT_MASK;              // mark var-int val
            *(destination_ptr++) = value;        // put 3rd byte to dst
            value = (uint8_t) (count & 0x7f);    // get next bits from count
            count >>= 7;                         // remove these 7 bits from count
        } else
        {
            *(destination_ptr++) = value;    // put last byte to dst
            return destination_ptr;
        }
        if (0 < count) // if there are more bits
        {
            value |= HIGH_BIT_MASK;              // mark var-int val
            *(destination_ptr++) = value;        // put 4th byte to dst
            value = (uint8_t) (count & 0x1f);    // get last 5 bits from count
        } else
        {
            *(destination_ptr++) = value;    // put last byte to dst
            return destination_ptr;
        }
        *(destination_ptr++) = value;    // put last byte to dst
        return destination_ptr;
    }

    static uint32_t qpl_output_to_uint(qpl_out_format output)
    {
        switch (output)
        {
            case qpl_ow_nom:
                return 1u;

            case qpl_ow_8:
                return 8u;

            case qpl_ow_16:
                return 16u;

            case qpl_ow_32:
                return 32u;

            default:
                throw std::exception();
        }
    }

    static qpl_out_format uint_to_qpl_output(uint32_t output)
    {
        switch (output)
        {
            case 1u:
                return qpl_ow_nom;

            case 8u:
                return qpl_ow_8;

            case 16u:
                return qpl_ow_16;

            case 32u:
                return qpl_ow_32;

            default:
                return qpl_ow_nom;
        }
    }


    static uint32_t get_second_source_bit_length(qpl_operation operation,
                                                 uint32_t first_source_bit_width,
                                                 uint32_t first_source_number_of_elements)
    {
        uint32_t result_bit_length;

        switch (operation)
        {
            case qpl_op_select:
                result_bit_length = first_source_number_of_elements;
                break;

            case qpl_op_expand:
                result_bit_length = first_source_number_of_elements;
                break;

            default:
                result_bit_length = 0u;
                break;
        }

        return result_bit_length;
    }

#define SKIP_TEST_FOR(path) \
if (qpl::test::util::TestEnvironment::GetInstance().GetExecutionPath() == path) \
GTEST_SKIP()

#define SKIP_TEST_CASE_FOR(path, message) \
if (qpl::test::util::TestEnvironment::GetInstance().GetExecutionPath() == path) { \
    testing::AssertionSuccess() << "SKIPPED TEST CASE: " << GetTestCase() << message; \
    return; }
}

#endif // QPL_TEST_UTIL_HPP
