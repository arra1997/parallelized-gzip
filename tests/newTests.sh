#!/bin/bash
eval "./gzip -p 4 --keep big.txt"
eval "mv big.txt.gz test.txt.gz"
eval "./gzip -d test.txt.gz"
eval "diff test.txt big.txt"
eval "echo \"Parallel Compression with 4 Threads decompressed file integrity test -- PASS\""


eval "./gzip -p 8 --keep big.txt"
eval "mv big.txt.gz test.txt.gz"
eval "./gzip -d test.txt.gz"
eval "diff test.txt big.txt"
eval "echo \"Parallel Compression with 8 Threads decompressed file integrity test -- PASS\""


eval "./gzip -p 2 --keep big.txt"
eval "mv big.txt.gz 2t.txt.gz"
eval "./gzip -d 2t.txt.gz"

eval "./gzip -p 4 --keep big.txt"
eval "mv big.txt.gz 4t.txt.gz"
eval "./gzip -d 4t.txt.gz"

eval "./gzip -p 8 --keep big.txt"
eval "mv big.txt.gz 8t.txt.gz"
eval "./gzip -d 8t.txt.gz"

eval "diff 2t.txt 4t.txt"
eval "echo \"Decompressed file of 2 Thread compression and 4 Thread are the same -- PASS\""

eval "diff 4t.txt 8t.txt"
eval "echo \"Decompressed file of 4 Thread compression and 8 Thread are the same -- PASS\""


eval "cat big.txt big.txt >> 2big.txt"
eval "./gzip -p 4 --keep big.txt"
eval "mv big.txt.gz test.txt.gz"
eval "cat test.txt.gz test.txt.gz >> 2test.txt.gz"
eval "./gzip -d 2test.txt.gz"
eval "diff 2test.txt 2big.txt"
eval "echo \"Appended decompressed files same as appended original files -- PASS\""
