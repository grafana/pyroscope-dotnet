# Issue 259 SIGSEGV stacktrace

Captured from the reproducer branch by rebuilding `rideshare-app-musl:net10.0` from this tree and running the app under `gdb` inside the container.

## Repro sequence

1. Start the musl `.NET 10` rideshare image under `gdb`:

```sh
docker run -d --name pyroscope-dotnet-gdb --cap-add SYS_PTRACE -p 5006:5000 \
  -e ASPNETCORE_URLS=http://*:5000 \
  -e DD_TRACE_DEBUG=true \
  -e DD_PROFILING_CPU_ENABLED=false \
  -e DD_PROFILING_WALLTIME_ENABLED=true \
  -e PYROSCOPE_APPLICATION_NAME=repro.walltime.gdb \
  rideshare-app-musl:net10.0 \
  sh -c "apk add --no-cache gdb >/dev/null && exec gdb -batch \
    -ex 'handle SIGUSR1 nostop noprint pass' \
    -ex 'handle SIGPROF nostop noprint pass' \
    -ex 'run' \
    -ex 'bt full' \
    --args dotnet /dotnet/rideshare.dll"
```

2. Hit the runtime toggle endpoints and then generate load:

```sh
curl -X POST http://localhost:5006/profiling/all/false
sleep 7
curl -X POST http://localhost:5006/profiling/cpu/true
curl http://localhost:5006/bike
```

The first `GET /bike` after re-enabling CPU tracking triggered the crash while the restarted wall-time sampler thread was entering `ResetThreadsCpuConsumption()`.

## Captured backtrace

```text
Thread 40 "DD_StackSampler" received signal SIGSEGV, Segmentation fault.
[Switching to LWP 60]
0x000078b54febb668 in OsSpecificApi::IsRunning (pThreadInfo=0x0) at /profiler/profiler/src/ProfilerEngine/Datadog.Profiler.Native.Linux/OsSpecificApi.cpp:195
#0  0x000078b54febb668 in OsSpecificApi::IsRunning (pThreadInfo=0x0) at /profiler/profiler/src/ProfilerEngine/Datadog.Profiler.Native.Linux/OsSpecificApi.cpp:195
        isRunning = false
        cpuTime = 0
#1  0x000078b55003bb8a in StackSamplerLoop::ResetThreadsCpuConsumption (this=0x7874b611d6a0) at /profiler/profiler/src/ProfilerEngine/Datadog.Profiler.Native/StackSamplerLoop.cpp:612
        it = {<std::__shared_ptr<ManagedThreadInfo, (__gnu_cxx::_Lock_policy)2>> = {<std::__shared_ptr_access<ManagedThreadInfo, (__gnu_cxx::_Lock_policy)2, false, false>> = {<No data fields>}, _M_ptr = 0x78b54fc641d0, _M_refcount = {_M_pi = 0x78b54fc641c0}}, <No data fields>}
        i = 0
        managedThreadsCount = 13
#2  0x000078b55003b91d in StackSamplerLoop::MainLoop (this=0x7874b611d6a0) at /profiler/profiler/src/ProfilerEngine/Datadog.Profiler.Native/StackSamplerLoop.cpp:154
#3  0x000078b55003e15c in StackSamplerLoop::StartImpl()::$_0::operator()() const (this=0x7874b966cf28) at /profiler/profiler/src/ProfilerEngine/Datadog.Profiler.Native/StackSamplerLoop.cpp:116
#4  0x000078b55003e125 in std::__invoke_impl<void, StackSamplerLoop::StartImpl()::$_0>(std::__invoke_other, StackSamplerLoop::StartImpl()::$_0&&) (__f=...) at /usr/bin/../lib/gcc/x86_64-alpine-linux-musl/12.2.1/../../../../include/c++/12.2.1/bits/invoke.h:61
#5  0x000078b55003e0e5 in std::__invoke<StackSamplerLoop::StartImpl()::$_0>(StackSamplerLoop::StartImpl()::$_0&&) (__fn=...) at /usr/bin/../lib/gcc/x86_64-alpine-linux-musl/12.2.1/../../../../include/c++/12.2.1/bits/invoke.h:96
#6  0x000078b55003e0bd in std::thread::_Invoker<std::tuple<StackSamplerLoop::StartImpl()::$_0> >::_M_invoke<0ul> (this=0x7874b966cf28) at /usr/bin/../lib/gcc/x86_64-alpine-linux-musl/12.2.1/../../../../include/c++/12.2.1/bits/std_thread.h:258
#7  0x000078b55003e095 in std::thread::_Invoker<std::tuple<StackSamplerLoop::StartImpl()::$_0> >::operator() (this=0x7874b966cf28) at /usr/bin/../lib/gcc/x86_64-alpine-linux-musl/12.2.1/../../../../include/c++/12.2.1/bits/std_thread.h:265
#8  0x000078b55003dff9 in std::thread::_State_impl<std::thread::_Invoker<std::tuple<StackSamplerLoop::StartImpl()::$_0> > >::_M_run (this=0x7874b966cf20) at /usr/bin/../lib/gcc/x86_64-alpine-linux-musl/12.2.1/../../../../include/c++/12.2.1/bits/std_thread.h:210
#9  0x000078b550779de3 in std::execute_native_thread_routine (__p=0x7874b966cf20) at /home/buildozer/aports/main/gcc/src/gcc-12-20220924/libstdc++-v3/src/c++11/thread.cc:82
#10 0x000078b5d0fac305 in dd_pthread_entry (arg=0x7874b966cf60) at /profiler/profiler/src/ProfilerEngine/Datadog.Linux.ApiWrapper/functions_to_wrap.c:599
#11 0x000078b5d102043a in start (p=<optimized out>) at src/thread/pthread_create.c:207
#12 0x000078b5d1021d88 in __clone () at src/thread/x86_64/clone.s:22
```

This confirms the reproducer is hitting a null dereference in the restarted stack sampler path, not just an HTTP-level failure.
