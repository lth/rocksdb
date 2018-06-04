// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#ifndef ROCKSDB_LITE

#include <set>

#include "utilities/transactions/write_prepared_txn.h"

namespace rocksdb {

class WriteUnpreparedTxn : public WritePreparedTxn {
 public:
  WriteUnpreparedTxn(WritePreparedTxnDB* db,
                     const WriteOptions& write_options,
                     const TransactionOptions& txn_options);

  using TransactionBaseImpl::Put;
  virtual Status Put(ColumnFamilyHandle* column_family, const Slice& key,
                     const Slice& value) override;
  virtual Status Put(ColumnFamilyHandle* column_family, const SliceParts& key,
                     const SliceParts& value) override;

  using TransactionBaseImpl::Merge;
  virtual Status Merge(ColumnFamilyHandle* column_family, const Slice& key,
                       const Slice& value) override;

  using TransactionBaseImpl::Delete;
  virtual Status Delete(ColumnFamilyHandle* column_family,
                        const Slice& key) override;
  virtual Status Delete(ColumnFamilyHandle* column_family,
                        const SliceParts& key) override;

  using TransactionBaseImpl::SingleDelete;
  virtual Status SingleDelete(ColumnFamilyHandle* column_family,
                              const Slice& key) override;
  virtual Status SingleDelete(ColumnFamilyHandle* column_family,
                              const SliceParts& key) override;

 protected:
  void Initialize(const TransactionOptions& txn_options) override;

  Status PrepareInternal() override;

  Status CommitInternal() override;

 private:
  Status MaybeFlushWriteBatchToDB();
  size_t max_write_batch_size_;

  // Set of keys that have written to that have already been written to DB
  // (ie. not in write_batch_).
  //
  std::map<uint32_t, std::unordered_set<std::string>> write_key_set_;

  // List of unprep_seq sequence numbers that we have already written to DB.
  std::map<SequenceNumber, size_t> unprep_seq_;
};

}  // namespace rocksdb

#endif  // ROCKSDB_LITE
