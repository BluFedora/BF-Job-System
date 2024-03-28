var job__system_8cpp =
[
    [ "Job::TaskPtr", "struct_job_1_1_task_ptr.html", "struct_job_1_1_task_ptr" ],
    [ "Job::TaskFnStorage", "union_job_1_1_task_fn_storage.html", "union_job_1_1_task_fn_storage" ],
    [ "Job::Task", "struct_job_1_1_task.html", "struct_job_1_1_task" ],
    [ "Job::TaskMemoryBlock", "namespace_job.html#union_job_1_1_task_memory_block", [
      [ "next", "namespace_job.html#a3f738fb139edbd8b37aa827b7ef40a2f", null ],
      [ "storage", "namespace_job.html#a997127b0be46e34cd4b5a0e8ec41637f", null ]
    ] ],
    [ "Job::TaskPool", "namespace_job.html#struct_job_1_1_task_pool", [
      [ "memory", "namespace_job.html#ad78b67acabbfeffa4fa359033f9e283e", null ],
      [ "freelist", "namespace_job.html#a3eaa44dbda014534b66e81de453c2750", null ]
    ] ],
    [ "Job::ThreadLocalState", "namespace_job.html#struct_job_1_1_thread_local_state", [
      [ "normal_queue", "namespace_job.html#a6d438e37fa2b50e1317c0a1c3940d725", null ],
      [ "worker_queue", "namespace_job.html#a1e192ea5b6078206abfaee90b2113cda", null ],
      [ "task_allocator", "namespace_job.html#a9d4f4ef4764274265de7c74e12ab906e", null ],
      [ "allocated_tasks", "namespace_job.html#abde4d73b7c8c689c4268bf3851468ab0", null ],
      [ "num_allocated_tasks", "namespace_job.html#a69022452a94cf0826b6d41fd937a6a6e", null ],
      [ "last_stolen_worker", "namespace_job.html#a5b4c2833a75c54b2915fed6d47cf6841", null ],
      [ "rng_state", "namespace_job.html#a6c665b2e0616e1c9a1baeb2e0ec42704", null ],
      [ "thread_id", "namespace_job.html#a1c97516f3a6726b0ba43791fcbe341fc", null ]
    ] ],
    [ "Job::InitializationLock", "namespace_job.html#struct_job_1_1_initialization_lock", [
      [ "init_mutex", "namespace_job.html#af387d34b32af61c8e087ea987605f220", null ],
      [ "init_cv", "namespace_job.html#aea7eded61e2238418bb232de730caf53", null ],
      [ "num_workers_ready", "namespace_job.html#aa5e8e433ba17496dcee1ab1c057ee2b9", null ]
    ] ],
    [ "Job::JobSystemContext", "namespace_job.html#struct_job_1_1_job_system_context", [
      [ "workers", "namespace_job.html#a7136616a9c9b73ef1f3ef9b28e75f76e", null ],
      [ "num_workers", "namespace_job.html#a8c3b1e96a4c0f2e3a90b75dd8255dc3b", null ],
      [ "num_tasks_per_worker", "namespace_job.html#a938eecb18a6a3d6262aec76547e296ca", null ],
      [ "init_lock", "namespace_job.html#a0d5db0ce93f3106774a05370d8bde73d", null ],
      [ "sys_arch_str", "namespace_job.html#a99e33c9b5b872813774ce73d839c23bd", null ],
      [ "system_alloc_size", "namespace_job.html#ac0ea66f39f501ceaded0e28236773fe2", null ],
      [ "system_alloc_alignment", "namespace_job.html#aa640462a9e9997538a13538d1f63d026", null ],
      [ "needs_delete", "namespace_job.html#a6ed368a7c87ea03926bf80cfa2e47709", null ],
      [ "is_running", "namespace_job.html#a93ec86776cd50eb50948754288be9f7b", null ],
      [ "main_queue", "namespace_job.html#a049da5bae449544c7b17c3aceeee3a79", null ],
      [ "worker_sleep_mutex", "namespace_job.html#aab625f8e152ac7b501029f59f6ed4064", null ],
      [ "worker_sleep_cv", "namespace_job.html#a26844370c934a2287a6066b3e5889d98", null ],
      [ "num_available_jobs", "namespace_job.html#a9be19abdc70fad2d52894a42575fe0e6", null ]
    ] ],
    [ "NativePause", "job__system_8cpp.html#af1f8f96c9abf19bf3d0754524aaa2efc", null ],
    [ "TaskHandle", "job__system_8cpp.html#a66b4ae2d934892bb03f311c31506476e", null ],
    [ "TaskHandleType", "job__system_8cpp.html#af5130f87f126f7ce70872c29687c0ccd", null ],
    [ "AtomicTaskHandleType", "job__system_8cpp.html#adbbdb7aa147fca8ee364bf7e2d5e53d8", null ],
    [ "WorkerIDType", "job__system_8cpp.html#aef42ab2f4880b98a40cf86e5541503e0", null ],
    [ "AtomicInt32", "job__system_8cpp.html#a2b005d277912bb3695b8b53b19341a53", null ],
    [ "Byte", "job__system_8cpp.html#ae159e934d8d1f329f729f24874234d40", null ],
    [ "AtomicTaskPtr", "job__system_8cpp.html#a051e674ab31e8b9946de4990f3b4288b", null ],
    [ "k_CachelineSize", "job__system_8cpp.html#aef3303347f87a62f73106bf96a855e22", null ],
    [ "k_ExpectedTaskSize", "job__system_8cpp.html#aa141d8407e498c708be9a947805b6853", null ],
    [ "k_InvalidQueueType", "job__system_8cpp.html#abb6be9ba0f23d0e66306290cd06d6089", null ],
    [ "NullTaskHandle", "job__system_8cpp.html#a4bb56989a0a31d03a041c40e8ac99cc7", null ],
    [ "g_JobSystem", "job__system_8cpp.html#afb9d4821deb09320023a531d1cb87f58", null ],
    [ "g_CurrentWorker", "job__system_8cpp.html#abfa74abcec62d6d1b3d06edec0cf3b0d", null ]
];