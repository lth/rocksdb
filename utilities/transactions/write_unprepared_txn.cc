//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#ifndef ROCKSDB_LITE

#include "utilities/transactions/write_prepared_txn_db.h"
#include "utilities/transactions/write_unprepared_txn_db.h"
#include "utilities/transactions/write_unprepared_txn.h"

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

namespace rocksdb {

WriteUnpreparedTxn::WriteUnpreparedTxn(WritePreparedTxnDB* txn_db,
                                   const WriteOptions& write_options,
                                   const TransactionOptions& txn_options)
    : WritePreparedTxn(txn_db, write_options, txn_options)
{
  max_write_batch_size_ = txn_options.max_write_batch_size;
  write_batch_.SetMaxBytes(0);
}

void WriteUnpreparedTxn::Initialize(const TransactionOptions& txn_options) {
  max_write_batch_size_ = txn_options.max_write_batch_size;
  PessimisticTransaction::Initialize(txn_options);
  write_batch_.SetMaxBytes(0);
}

Status WriteUnpreparedTxn::Put(ColumnFamilyHandle* column_family,
                               const Slice& key, const Slice& value) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::Put(column_family, key, value);
}

Status WriteUnpreparedTxn::Put(ColumnFamilyHandle* column_family,
                               const SliceParts& key, const SliceParts& value) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::Put(column_family, key, value);
}

Status WriteUnpreparedTxn::Merge(ColumnFamilyHandle* column_family,
                                 const Slice& key, const Slice& value) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::Merge(column_family, key, value);
}

Status WriteUnpreparedTxn::Delete(ColumnFamilyHandle* column_family,
                                  const Slice& key) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::Delete(column_family, key);
}

Status WriteUnpreparedTxn::Delete(ColumnFamilyHandle* column_family,
                                  const SliceParts& key) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::Delete(column_family, key);
}

Status WriteUnpreparedTxn::SingleDelete(ColumnFamilyHandle* column_family,
                                        const Slice& key) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::SingleDelete(column_family, key);
}

Status WriteUnpreparedTxn::SingleDelete(ColumnFamilyHandle* column_family,
                                        const SliceParts& key) {
  Status s = MaybeFlushWriteBatchToDB();
  if (!s.ok()) {
    return s;
  }
  return TransactionBaseImpl::SingleDelete(column_family, key);
}

Status WriteUnpreparedTxn::MaybeFlushWriteBatchToDB() {
  assert(name_.size() > 0);
  if (write_batch_.GetDataSize() > max_write_batch_size_) {
    Status s = PrepareInternal();
    if (!s.ok()) {
      return s;
    }
  }
  return Status::OK();
}

Status WriteUnpreparedTxn::PrepareInternal() {
  Status s = WritePreparedTxn::PrepareInternal();
  if (!s.ok()) {
    return s;
  }

  // Reset id_ and write_batch_.
  unprep_seq_[id_] = prepare_batch_cnt_;

  id_ = 0;
  prepare_batch_cnt_ = 0;
  write_batch_.Clear();
  WriteBatchInternal::InsertNoop(write_batch_.GetWriteBatch());
  return s;
}

Status WriteUnpreparedTxn::CommitInternal() {
  // We take the commit-time batch and append the Commit marker.  The Memtable
  // will ignore the Commit marker in non-recovery mode
  WriteBatch* working_batch = GetCommitTimeWriteBatch();
  const bool empty = working_batch->Count() == 0;
  WriteBatchInternal::MarkCommit(working_batch, name_);

  const bool for_recovery = use_only_the_last_commit_time_batch_for_recovery_;
  if (!empty && for_recovery) {
    // When not writing to memtable, we can still cache the latest write batch.
    // The cached batch will be written to memtable in WriteRecoverableState
    // during FlushMemTable
    WriteBatchInternal::SetAsLastestPersistentState(working_batch);
  }

  const bool includes_data = !empty && !for_recovery;
  size_t commit_batch_cnt = 0;
  if (UNLIKELY(includes_data)) {
    ROCKS_LOG_WARN(db_impl_->immutable_db_options().info_log,
                   "Duplicate key overhead");
    SubBatchCounter counter(*wpt_db_->GetCFComparatorMap());
    auto s = working_batch->Iterate(&counter);
    assert(s.ok());
    commit_batch_cnt = counter.BatchCount();
  }
  const bool disable_memtable = !includes_data;
  const bool do_one_write =
      !db_impl_->immutable_db_options().two_write_queues || disable_memtable;
  const bool publish_seq = do_one_write;
  // Note: CommitTimeWriteBatch does not need AddPrepared since it is written to
  // DB in one shot. min_uncommitted still works since it requires capturing
  // data that is written to DB but not yet committed, while
  // CommitTimeWriteBatch commits with PreReleaseCallback.
  WriteUnpreparedCommitEntryPreReleaseCallback update_commit_map(
      wpt_db_, db_impl_, unprep_seq_, commit_batch_cnt,
      publish_seq);
  uint64_t seq_used = kMaxSequenceNumber;
  // Since the prepared batch is directly written to memtable, there is already
  // a connection between the memtable and its WAL, so there is no need to
  // redundantly reference the log that contains the prepared data.
  const uint64_t zero_log_number = 0ull;
  size_t batch_cnt = UNLIKELY(commit_batch_cnt) ? commit_batch_cnt : 1;
  auto s = db_impl_->WriteImpl(write_options_, working_batch, nullptr, nullptr,
                               zero_log_number, disable_memtable, &seq_used,
                               batch_cnt, &update_commit_map);
  assert(!s.ok() || seq_used != kMaxSequenceNumber);
  if (LIKELY(do_one_write || !s.ok())) {
    if (LIKELY(s.ok())) {
      // Note RemovePrepared should be called after WriteImpl that publishsed
      // the seq. Otherwise SmallestUnCommittedSeq optimization breaks.
      for (const auto& seq : unprep_seq_) {
        wpt_db_->RemovePrepared(seq.first, seq.second);
      }
    }
    return s;
  }  // else do the 2nd write to publish seq
  // Note: the 2nd write comes with a performance penality. So if we have too
  // many of commits accompanied with ComitTimeWriteBatch and yet we cannot
  // enable use_only_the_last_commit_time_batch_for_recovery_ optimization,
  // two_write_queues should be disabled to avoid many additional writes here.
  class PublishSeqPreReleaseCallback : public PreReleaseCallback {
   public:
    explicit PublishSeqPreReleaseCallback(DBImpl* db_impl)
        : db_impl_(db_impl) {}
    virtual Status Callback(SequenceNumber seq, bool is_mem_disabled) override {
#ifdef NDEBUG
      (void)is_mem_disabled;
#endif
      assert(is_mem_disabled);
      assert(db_impl_->immutable_db_options().two_write_queues);
      db_impl_->SetLastPublishedSequence(seq);
      return Status::OK();
    }

   private:
    DBImpl* db_impl_;
  } publish_seq_callback(db_impl_);
  WriteBatch empty_batch;
  empty_batch.PutLogData(Slice());
  // In the absence of Prepare markers, use Noop as a batch separator
  WriteBatchInternal::InsertNoop(&empty_batch);
  const bool DISABLE_MEMTABLE = true;
  const size_t ONE_BATCH = 1;
  const uint64_t NO_REF_LOG = 0;
  s = db_impl_->WriteImpl(write_options_, &empty_batch, nullptr, nullptr,
                          NO_REF_LOG, DISABLE_MEMTABLE, &seq_used, ONE_BATCH,
                          &publish_seq_callback);
  assert(!s.ok() || seq_used != kMaxSequenceNumber);
  // Note RemovePrepared should be called after WriteImpl that publishsed the
  // seq. Otherwise SmallestUnCommittedSeq optimization breaks.
  for (const auto& seq : unprep_seq_) {
    wpt_db_->RemovePrepared(seq.first, seq.second);
  }
  unprep_seq_.clear();
  return s;
}

}  // namespace rocksdb

#endif  // ROCKSDB_LITE
