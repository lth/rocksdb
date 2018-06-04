//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#ifndef ROCKSDB_LITE

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "utilities/transactions/transaction_test.h"

namespace rocksdb {

class WriteUnpreparedTransactionTest
    : public TransactionTestBase,
      virtual public ::testing::WithParamInterface<
          std::tuple<bool, bool, TxnDBWritePolicy>> {
 public:
  WriteUnpreparedTransactionTest()
      : TransactionTestBase(std::get<0>(GetParam()),
                            std::get<1>(GetParam()),
                            std::get<2>(GetParam())){};
};

INSTANTIATE_TEST_CASE_P(
    WriteUnpreparedTransactionTest, WriteUnpreparedTransactionTest,
    ::testing::Values(std::make_tuple(false, false, WRITE_UNPREPARED),
                      std::make_tuple(false, true, WRITE_UNPREPARED)));

TEST_P(WriteUnpreparedTransactionTest, UnpreparedBatch) {
  WriteOptions write_options;
  TransactionOptions txn_options;
  txn_options.max_write_batch_size = 20;

  std::string value;
  Status s;

  Transaction* txn1 = db->BeginTransaction(write_options, txn_options);
  txn1->SetName("xid1");
  ASSERT_TRUE(txn1);

  s = txn1->Put("foo", "bar");
  ASSERT_OK(s);
  s = txn1->Put("foo2", "bar");
  ASSERT_OK(s);
  s = txn1->Put("foo3", "bar");
  ASSERT_OK(s);
  s = txn1->Put("foo4", "bar");
  ASSERT_OK(s);
  s = txn1->Put("foo5", "bar");

  s = txn1->Prepare();
  ASSERT_OK(s);

  s = txn1->Commit();

  delete txn1;
}


}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#else
#include <stdio.h>

int main(int /*argc*/, char** /*argv*/) {
  fprintf(stderr,
          "SKIPPED as Transactions are not supported in ROCKSDB_LITE\n");
  return 0;
}

#endif  // ROCKSDB_LITE
