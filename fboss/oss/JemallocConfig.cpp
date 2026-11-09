// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

extern "C" {
const char* malloc_conf =
    "background_thread:true,"
    "metadata_thp:auto,"
    "dirty_decay_ms:1000,"
    "muzzy_decay_ms:0";
}
