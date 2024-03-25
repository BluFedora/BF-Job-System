var job__api_8hpp =
[
    [ "Job::JobSystemCreateOptions", "namespace_job.html#struct_job_1_1_job_system_create_options", [
      [ "num_threads", "namespace_job.html#ae66ffe1e31258baf98a4db395ed6bff4", null ],
      [ "main_queue_size", "namespace_job.html#aa03f5b7291117b5e37a2a7f3f20d26d1", null ],
      [ "normal_queue_size", "namespace_job.html#a65f68139b9fe13d51e749b62b85b5614", null ],
      [ "worker_queue_size", "namespace_job.html#ad7f499ac9f75090b562e4d30afa070ed", null ],
      [ "job_steal_rng_seed", "namespace_job.html#a0882adb15126b4fe2a94c21f744b6525", null ]
    ] ],
    [ "Job::JobSystemMemoryRequirements", "namespace_job.html#struct_job_1_1_job_system_memory_requirements", [
      [ "options", "namespace_job.html#af08f113d634053c9df9fea936cf5bf95", null ],
      [ "byte_size", "namespace_job.html#ac8d9a46abe68344e8cc1cb30cf57850d", null ],
      [ "alignment", "namespace_job.html#a4176dfb73f1ddef41eee28f2e8a3ef78", null ]
    ] ],
    [ "Job::TaskData", "namespace_job.html#struct_job_1_1_task_data", [
      [ "ptr", "namespace_job.html#a206e9e451cca5c05ad02c69ad733d8f0", null ],
      [ "size", "namespace_job.html#a19841c4464efdc31fd3315982499ec87", null ]
    ] ],
    [ "Job::IndexIterator", "struct_job_1_1_index_iterator.html", "struct_job_1_1_index_iterator" ],
    [ "Job::IndexRange", "struct_job_1_1_index_range.html", "struct_job_1_1_index_range" ],
    [ "Job::StaticCountSplitter< max_count >", "struct_job_1_1_static_count_splitter.html", "struct_job_1_1_static_count_splitter" ],
    [ "Job::CountSplitter", "struct_job_1_1_count_splitter.html", "struct_job_1_1_count_splitter" ],
    [ "Job::StaticDataSizeSplitter< T, max_size >", "struct_job_1_1_static_data_size_splitter.html", "struct_job_1_1_static_data_size_splitter" ],
    [ "Job::DataSizeSplitter< T >", "struct_job_1_1_data_size_splitter.html", "struct_job_1_1_data_size_splitter" ],
    [ "WorkerID", "job__api_8hpp.html#ac7727f146165ae76b95cc50196df236e", null ],
    [ "TaskFn", "job__api_8hpp.html#a2a553c2727a284305ddbc9d746e58267", null ],
    [ "QueueType", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fb", [
      [ "NORMAL", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fba1e23852820b9154316c7c06e2b7ba051", null ],
      [ "MAIN", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fba186495f7da296bf880df3a22a492b378", null ],
      [ "WORKER", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fba531886e636f1aa36e0fc96d49f342613", null ]
    ] ],
    [ "checkTaskDataSize", "job__api_8hpp.html#a0c2a80b0061efcff2783b9ac1edffa34", null ],
    [ "taskQType", "job__api_8hpp.html#a725721f202ed179f6a4177b33f205666", null ],
    [ "taskPaddingStart", "job__api_8hpp.html#ac1efbfe3a8061891854820fba2050bcf", null ],
    [ "taskUsePadding", "job__api_8hpp.html#addf217a54cd7e0aee0463b3673b0aabf", null ],
    [ "mainQueueRunTask", "job__api_8hpp.html#ad6facce7775e71b7a21a96b69a704cf9", null ],
    [ "numSystemThreads", "job__api_8hpp.html#a69440377f73ecd3f40320af8e9cef349", null ],
    [ "MemRequirementsForConfig", "job__api_8hpp.html#a85395880746e9e731de28ca286cdbf88", null ],
    [ "Initialize", "job__api_8hpp.html#a0c74994fd5925bb416e81f84c85a1dbe", null ],
    [ "ProcessorArchitectureName", "job__api_8hpp.html#a77a997446d916b36e7478c7627694a51", null ],
    [ "NumWorkers", "job__api_8hpp.html#a5b606baa9964c9afd3e30294ccf7dc06", null ],
    [ "CurrentWorker", "job__api_8hpp.html#aaee5888463e3bb5cff3eff5f80d5265b", null ],
    [ "Shutdown", "job__api_8hpp.html#a9d923fa374392c284942ea7d8f90e466", null ],
    [ "TaskMake", "job__api_8hpp.html#a6a5491cbbe785588f2671f74b5d24380", null ],
    [ "TaskGetData", "job__api_8hpp.html#a7281d319d5869772540548055a18c8d7", null ],
    [ "TaskAddContinuation", "job__api_8hpp.html#a40443ff4af0a2b5e1426af43f58bc471", null ],
    [ "TaskSubmit", "job__api_8hpp.html#a136f0bed0a70a7389c7e5ce4821a2629", null ],
    [ "TaskDataAs", "job__api_8hpp.html#aa527ccf46bdca5c19e58d71c0187ad5c", null ],
    [ "TaskEmplaceData", "job__api_8hpp.html#aab93a1aea2b502fd2657cc30d0540e68", null ],
    [ "TaskSetData", "job__api_8hpp.html#a7ba53682d46eec5bdeab726d5648a2ce", null ],
    [ "TaskMake", "job__api_8hpp.html#a76170415e85eea5113a606a878a7dcf9", null ],
    [ "TaskIncRef", "job__api_8hpp.html#ab933882d5d7f7e4ba7c0fd63e2966f11", null ],
    [ "TaskDecRef", "job__api_8hpp.html#ad9be65ab1c2925023348fbdfcbf9fe0d", null ],
    [ "TaskIsDone", "job__api_8hpp.html#afa9f7e7a737f1babc1345e4ceaf64e79", null ],
    [ "TickMainQueue", "job__api_8hpp.html#ada20ebf3082f8aea7b75afc210d7621b", null ],
    [ "TickMainQueue", "job__api_8hpp.html#a0f5c158177872f256a96aba479a04b0f", null ],
    [ "WaitOnTask", "job__api_8hpp.html#a2961eee80e34c41deade7924657154b8", null ],
    [ "TaskSubmitAndWait", "job__api_8hpp.html#a21ff0cc938b7cd7efc74be4e7c82ed74", null ],
    [ "PauseProcessor", "job__api_8hpp.html#a9b2154154d8415e9f83b659a2673ea17", null ],
    [ "YieldTimeSlice", "job__api_8hpp.html#ac3a9fcd0c44df810bd831b086a960903", null ],
    [ "taskLambdaWrapper", "job__api_8hpp.html#aa0888cd9cc24f19f422a987c8c26dc74", null ],
    [ "ParallelFor", "job__api_8hpp.html#a9e954e52e77f521bd679ceedee2e9a3b", null ],
    [ "ParallelFor", "job__api_8hpp.html#a9d0b26b28577004e29e759e45c2425e2", null ],
    [ "ParallelInvoke", "job__api_8hpp.html#a4be4134569de0124394d50c8407ea70e", null ]
];