var job__system_8cpp =
[
    [ "job::TaskPtr", "structjob_1_1_task_ptr.html", "structjob_1_1_task_ptr" ],
    [ "job::Task", "structjob_1_1_task.html", "structjob_1_1_task" ],
    [ "job::TaskMemoryBlock", "namespacejob.html#unionjob_1_1_task_memory_block", [
      [ "next", "namespacejob.html#a8cf0b30dbf7ad10fbb3b6414d7223640", null ],
      [ "storage", "namespacejob.html#a53e77a9e912d21a3f906277ebdba1dbb", null ]
    ] ],
    [ "job::TaskPool", "namespacejob.html#structjob_1_1_task_pool", [
      [ "memory", "namespacejob.html#a89aea7d990a986b8b4d49e13ead779db", null ],
      [ "freelist", "namespacejob.html#a8e9745b93c055db1a04e82026f4d1980", null ]
    ] ],
    [ "job::ThreadLocalState", "namespacejob.html#structjob_1_1_thread_local_state", [
      [ "normal_queue", "namespacejob.html#a6eb67b24feeeecd74f2ecfcda5d45e30", null ],
      [ "worker_queue", "namespacejob.html#a419d840f4881c5c17a34e194e208cfc1", null ],
      [ "task_allocator", "namespacejob.html#abace9a94dbf67f36aa76b6b7b86267ea", null ],
      [ "allocated_tasks", "namespacejob.html#ad958662c695fa9fe139d64d0c631eeee", null ],
      [ "num_allocated_tasks", "namespacejob.html#a94660044d39a3982e0355b74890aeb9f", null ],
      [ "last_stolen_worker", "namespacejob.html#a3f46d20a6c4e6a419dae6f262aa0ebe9", null ],
      [ "rng_state", "namespacejob.html#ad7cf84cffac655aac4987d5508a7ef22", null ],
      [ "thread_id", "namespacejob.html#af6ef6202e282d2ce885e0af1f84f48b1", null ]
    ] ],
    [ "job::InitializationLock", "namespacejob.html#structjob_1_1_initialization_lock", [
      [ "init_mutex", "namespacejob.html#a99a6b698e961b9739f97245ecdfeb79d", null ],
      [ "init_cv", "namespacejob.html#ae2324149d8110eaea5958099b3341a7b", null ],
      [ "num_workers_ready", "namespacejob.html#ab17a1094429d8e4b2311e83140e05f13", null ]
    ] ],
    [ "job::JobSystemContext", "namespacejob.html#structjob_1_1_job_system_context", [
      [ "workers", "namespacejob.html#a3c48c9e6c8e94bf74c4c6ee8a144a2ac", null ],
      [ "num_workers", "namespacejob.html#ad99146cdeb02f60863f651fbe51a3275", null ],
      [ "num_user_threads_setup", "namespacejob.html#ad830cc90309e0619ea71ac583058f42a", null ],
      [ "num_tasks_per_worker", "namespacejob.html#a98fd212d7fbfb37b0ebbf2baa15eb08f", null ],
      [ "needs_delete", "namespacejob.html#a8b8546bc05dced4dc9cc76e080a47c26", null ],
      [ "is_running", "namespacejob.html#a19030991a9f5a2bdf078724c99359ce0", null ],
      [ "init_lock", "namespacejob.html#a35d4e5208fa0bea06993cf9ab3fb37e7", null ],
      [ "sys_arch_str", "namespacejob.html#a039173530b335cfc5daee64e7ac6b3d7", null ],
      [ "system_alloc_size", "namespacejob.html#ae45fa3542b2a028e5f0a7d8d8ce62563", null ],
      [ "system_alloc_alignment", "namespacejob.html#ac056e4bcad3b40f2f6ce2d31223a9b71", null ],
      [ "worker_sleep_mutex", "namespacejob.html#a1f5799771d4c30ff84c992c9a4313d48", null ],
      [ "worker_sleep_cv", "namespacejob.html#a2c8ee7099752a85b43a7de60caad9578", null ],
      [ "num_available_jobs", "namespacejob.html#a937ad16a8eae1e3ef3dade4e21081c55", null ]
    ] ],
    [ "NativePause", "job__system_8cpp.html#af1f8f96c9abf19bf3d0754524aaa2efc", null ],
    [ "TaskHandle", "job__system_8cpp.html#ae6f84df6ce72c5e2c12d73f9aeda93b5", null ],
    [ "TaskHandleType", "job__system_8cpp.html#a60a1a63555e5452f703cb3277b93a72d", null ],
    [ "AtomicTaskHandleType", "job__system_8cpp.html#a657b6393569d5bb88c8545ae0d80c229", null ],
    [ "WorkerIDType", "job__system_8cpp.html#acdde51f0f6d48f3d7d9e76d01c44ce5f", null ],
    [ "AtomicInt32", "job__system_8cpp.html#ab5c81575dbc18b089122cf4453d8cfcd", null ],
    [ "Byte", "job__system_8cpp.html#ad1cbfdb4aa447b83c9b660e04de547ac", null ],
    [ "AtomicTaskPtr", "job__system_8cpp.html#a251ce3237809df3b8db5845340266ca8", null ],
    [ "k_CachelineSize", "job__system_8cpp.html#a2e2527e09a00692e0e862f89a5dbdf41", null ],
    [ "k_ExpectedTaskSize", "job__system_8cpp.html#ae5648248c80793f10399a670bb37e93b", null ],
    [ "NullTaskHandle", "job__system_8cpp.html#ac1442a78a218a605452140b358eeba80", null ],
    [ "g_JobSystem", "job__system_8cpp.html#acf9188028f95dbc4757c97bbe2bddfc0", null ],
    [ "g_CurrentWorker", "job__system_8cpp.html#a7e421b0dce2eab1e3b9f6bc928e88058", null ]
];