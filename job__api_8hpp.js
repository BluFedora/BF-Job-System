var job__api_8hpp =
[
    [ "Job::JobSystemCreateOptions", "namespace_job.html#struct_job_1_1_job_system_create_options", [
      [ "num_user_threads", "namespace_job.html#acecbb32a0825377b5d3d6fce2ec8fe61", null ],
      [ "num_threads", "namespace_job.html#aeb8b880de236239bdcbfb5c42252bb02", null ],
      [ "main_queue_size", "namespace_job.html#aa03f5b7291117b5e37a2a7f3f20d26d1", null ],
      [ "normal_queue_size", "namespace_job.html#a65f68139b9fe13d51e749b62b85b5614", null ],
      [ "worker_queue_size", "namespace_job.html#ad7f499ac9f75090b562e4d30afa070ed", null ],
      [ "job_steal_rng_seed", "namespace_job.html#a0882adb15126b4fe2a94c21f744b6525", null ]
    ] ],
    [ "Job::JobSystemMemoryRequirements", "struct_job_1_1_job_system_memory_requirements.html", "struct_job_1_1_job_system_memory_requirements" ],
    [ "Job::TaskData", "namespace_job.html#struct_job_1_1_task_data", [
      [ "ptr", "namespace_job.html#a206e9e451cca5c05ad02c69ad733d8f0", null ],
      [ "size", "namespace_job.html#a19841c4464efdc31fd3315982499ec87", null ]
    ] ],
    [ "Job::Splitter", "struct_job_1_1_splitter.html", "struct_job_1_1_splitter" ],
    [ "WorkerID", "job__api_8hpp.html#ac7727f146165ae76b95cc50196df236e", null ],
    [ "TaskFn", "job__api_8hpp.html#a2a553c2727a284305ddbc9d746e58267", null ],
    [ "QueueType", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fb", [
      [ "NORMAL", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fba1e23852820b9154316c7c06e2b7ba051", null ],
      [ "MAIN", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fba186495f7da296bf880df3a22a492b378", null ],
      [ "WORKER", "job__api_8hpp.html#a686f6798e9f499e832cb9f125f0db5fba531886e636f1aa36e0fc96d49f342613", null ]
    ] ],
    [ "taskQType", "job__api_8hpp.html#a725721f202ed179f6a4177b33f205666", null ],
    [ "taskGetPrivateUserData", "job__api_8hpp.html#a66102c88da40e2c6b008bc97259d0d39", null ],
    [ "taskReservePrivateUserData", "job__api_8hpp.html#a5ed74505a8477781f8e252f6f05755ca", null ],
    [ "mainQueueTryRunTask", "job__api_8hpp.html#a8209af0db62c504d004ec94bf3889442", null ],
    [ "NumSystemThreads", "job__api_8hpp.html#ada79d1288c5458f1451a414ec89d2ba6", null ],
    [ "Initialize", "job__api_8hpp.html#a398a7fd0221839f321c00864238b9ab2", null ],
    [ "SetupUserThread", "job__api_8hpp.html#a9aab6d68cfe21248160a0c09f9ef8807", null ],
    [ "ProcessorArchitectureName", "job__api_8hpp.html#a77a997446d916b36e7478c7627694a51", null ],
    [ "NumWorkers", "job__api_8hpp.html#a5b606baa9964c9afd3e30294ccf7dc06", null ],
    [ "CurrentWorker", "job__api_8hpp.html#aaee5888463e3bb5cff3eff5f80d5265b", null ],
    [ "IsMainThread", "job__api_8hpp.html#ad0695eb307a63108942e4e634cae11f2", null ],
    [ "Shutdown", "job__api_8hpp.html#a9d923fa374392c284942ea7d8f90e466", null ],
    [ "TaskMake", "job__api_8hpp.html#a6a5491cbbe785588f2671f74b5d24380", null ],
    [ "TaskGetData", "job__api_8hpp.html#a1786f2fd8c5c13f53d066bf4e029445c", null ],
    [ "TaskAddContinuation", "job__api_8hpp.html#a381064dc945c407e9c80bf1685dd3ee7", null ],
    [ "TaskDataAs", "job__api_8hpp.html#a2b5a16b048e98b8a71dfc778d2c8f564", null ],
    [ "TaskEmplaceData", "job__api_8hpp.html#aab93a1aea2b502fd2657cc30d0540e68", null ],
    [ "TaskSetData", "job__api_8hpp.html#a7ba53682d46eec5bdeab726d5648a2ce", null ],
    [ "TaskDestructData", "job__api_8hpp.html#aeb32793271df92bd2ed13f1a6455f592", null ],
    [ "TaskMake", "job__api_8hpp.html#a76170415e85eea5113a606a878a7dcf9", null ],
    [ "TaskIncRef", "job__api_8hpp.html#ab933882d5d7f7e4ba7c0fd63e2966f11", null ],
    [ "TaskDecRef", "job__api_8hpp.html#ad9be65ab1c2925023348fbdfcbf9fe0d", null ],
    [ "TaskIsDone", "job__api_8hpp.html#afa9f7e7a737f1babc1345e4ceaf64e79", null ],
    [ "TickMainQueue", "job__api_8hpp.html#ada20ebf3082f8aea7b75afc210d7621b", null ],
    [ "TickMainQueue", "job__api_8hpp.html#a0f5c158177872f256a96aba479a04b0f", null ],
    [ "TaskSubmit", "job__api_8hpp.html#ae3e34b8468f9b731c3a56b6848587f4d", null ],
    [ "WaitOnTask", "job__api_8hpp.html#a2961eee80e34c41deade7924657154b8", null ],
    [ "TaskSubmitAndWait", "job__api_8hpp.html#a21ff0cc938b7cd7efc74be4e7c82ed74", null ],
    [ "PauseProcessor", "job__api_8hpp.html#ad3555facf3b46ce9ac3a4a0ae845b35d", null ],
    [ "YieldTimeSlice", "job__api_8hpp.html#a4f97b7424b855833a25294046caf3c3b", null ],
    [ "ParallelFor", "job__api_8hpp.html#a9e954e52e77f521bd679ceedee2e9a3b", null ],
    [ "ParallelReduce", "job__api_8hpp.html#af3b466a5f1cf2cd500569cb8bc394a29", null ],
    [ "ParallelFor", "job__api_8hpp.html#a9d0b26b28577004e29e759e45c2425e2", null ],
    [ "ParallelInvoke", "job__api_8hpp.html#a4be4134569de0124394d50c8407ea70e", null ]
];