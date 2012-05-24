// Copyright (c) 2012 Cloudera, Inc. All rights reserved.

#include "exec/hdfs-text-scanner.h"
#include "runtime/row-batch.h"
#include "util/string-parser.h"

using namespace std;
using namespace impala;

// Functions in this file are cross compiled to IR with clang.  These functions
// are modified at runtime with a query specific codegen'd WriteTuple

// This function will output tuples to the row batch from parsed field locations.
// The fields locations should be aligned to the start of the tuple (field at 0 is
// the first materialized slot).
// This function takes more arguments that is strictly necessary (they could be
// computed inside this function) but this is done to minimize the clang dependencies,
// specifically, calling function on the scan node.
int HdfsTextScanner::WriteAlignedTuples(RowBatch* row_batch, FieldLocation* fields,
    int num_tuples, int max_added_tuples, int slots_per_tuple, char** line_start) {
  
  DCHECK(tuple_ != NULL);
  // Reserve tuple rows from the batch.  num_tuples will never overflow the
  // batch because we never parse for more slots than we can handle.
  int row_idx = row_batch->AddRows(num_tuples);
  DCHECK(row_idx != RowBatch::INVALID_ROW_INDEX);

  uint8_t* tuple_row_mem = reinterpret_cast<uint8_t*>(row_batch->GetRow(row_idx));
  uint8_t* tuple_mem = reinterpret_cast<uint8_t*>(tuple_);
  Tuple* tuple = reinterpret_cast<Tuple*>(tuple_mem);
  TupleRow* tuple_row = reinterpret_cast<TupleRow*>(tuple_row_mem);

  uint8_t error[slots_per_tuple];
  memset(error, 0, sizeof(error));

  int tuples_returned = 0;

  // Loop through the fields and materialize all the tuples
  for (int i = 0; i < num_tuples; ++i) {
    int row_len;
    uint8_t error_in_row = false;
    // Materialize a single tuple.  This function will be replaced by a codegen'd
    // function.
    if (WriteCompleteTuple(fields, tuple, tuple_row, error, &error_in_row, &row_len)) {
      ++tuples_returned;
      tuple_mem += tuple_byte_size_;
      tuple_row_mem += row_batch->row_byte_size();
      tuple = reinterpret_cast<Tuple*>(tuple_mem);
      tuple_row = reinterpret_cast<TupleRow*>(tuple_row_mem);
    }

    // Report parse errors
    if (UNLIKELY(error_in_row)) {
      if (!ReportTupleParseError(fields, error, *line_start, row_len - 1)) {
        return -1;
      }
    }
    boundary_row_.Clear();
    
    if (tuples_returned == max_added_tuples) {
      break;
    }

    // Advance to the start of the next tuple
    fields += slots_per_tuple;
    *line_start += row_len;
  }

  // Commit all the tuples that were materialized to the row batch
  row_batch->CommitRows(tuples_returned);

  // Update tuple_ for next batch
  tuple_ = reinterpret_cast<Tuple*>(tuple_mem);

  return tuples_returned;
}

// Define the string parsing functions for llvm.  Stamp out the templated functions
#ifdef IR_COMPILE
extern "C"
bool IrStringToBool(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToBool(s, len, result);
}

int8_t IrStringToInt8(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToInt<int8_t>(s, len, result);
}

extern "C"
int16_t IrStringToInt16(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToInt<int16_t>(s, len, result);
}

extern "C"
int32_t IrStringToInt32(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToInt<int32_t>(s, len, result);
}

extern "C"
int64_t IrStringToInt64(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToInt<int64_t>(s, len, result);
}

extern "C"
float IrStringToFloat(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToFloat<float>(s, len, result);
}

extern "C"
double IrStringToDouble(const char* s, int len, StringParser::ParseResult* result) {
  return StringParser::StringToFloat<double>(s, len, result);
}
#endif

