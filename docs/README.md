# Pyroscope .NET Agent

The .NET profiling agent for Pyroscope.io. Based on [dd-trace-dotnet](https://github.com/DataDog/dd-trace-dotnet) with some key improvements:
- support for dynamic tags
- export to pyroscope

## Supported .NET versions:
 - .NET 6.0
 
## Supported platforms

| Spy Name     | Type         | Linux | macOS | Windows | Docker |
|:------------:|:------------:|:-----:|:-----:|:-------:|:------:|
| dotnetspy    | `embedded`   |   ✅  |       |         |        |

## Running the .NET profiler

1. Download `Pyroscope.Profiler.Native.so` and `Pyroscope.Linux.ApiWrapper.x64.so` from [latest release](https://github.com/pyroscope-io/pyroscope-dotnet/releases/)

2. Set the following required environment variables to enable profiler
```shell
PYROSCOPE_APPLICATION_NAME=rideshare.dotnet.app
PYROSCOPE_SERVER_ADDRESS=http://localhost:4040
PYROSCOPE_AUTH_TOKEN="psx-..." # optional auth token
PYROSCOPE_PROFILING_ENABLED=1
CORECLR_ENABLE_PROFILING=1
CORECLR_PROFILER={BD1A650D-AC5D-4896-B64F-D6FA25D6B26A}
CORECLR_PROFILER_PATH=Pyroscope.Profiler.Native.so
LD_PRELOAD=Pyroscope.Linux.ApiWrapper.x64.so
```

### Dynamic labels
It is possible to add labels to the profiling data. These labels can be used to filter the data in the UI.

1. Add dependency 
```shell
dotnet add package Pyroscope
````
2. Create a LabelSet and wrap a piece of code with Pyroscope.LabelsWrapper
```java
var labels = Pyroscope.LabelSet.Empty.BuildUpon()
    .Add("key1", "value1")
    .Build();
Pyroscope.LabelsWrapper.Do(labels, () =>
{
  SlowCode();
});
```

Labels can be nested. For nesting LabelSets use `LabelSet.BuildUpon` on non-empty set.
```java
var labels = Pyroscope.LabelSet.Empty.BuildUpon()
    .Add("key1", "value1")
    .Build();
Pyroscope.LabelsWrapper.Do(labels, () =>
{
  var labels2 = labels.BuildUpon()
    .Add("key2", "value2")
    .Build();
  Pyroscope.LabelsWrapper.Do(labels2, () =>
  {
    SlowCode();
  });
  FastCode();
});
```

## Configuration

| ENVIRONMENT VARIABLE            | Type         | DESCRIPTION |
|---------------------------------|------------|-----------|
| PYROSCOPE_PROFILING_LOG_DIR            | String       | Sets the directory for .NET Profiler logs. Defaults to /var/log/pyroscope/ . |
| PYROSCOPE_LABELS                       | String       | Static labels to apply to an uploaded profile. Must be a list of key:value separated by commas such as: layer:api,team:intake. |
| PYROSCOPE_PROFILING_ENABLED            | Boolean      | If set to true, enables the .NET Profiler. Defaults to false. |
| PYROSCOPE_PROFILING_WALLTIME_ENABLED   | Boolean      | If set to false, disables the Wall time profiling. Defaults to true. |
| PYROSCOPE_PROFILING_CPU_ENABLED        | Boolean      | If set to false, disables the CPU profiling. Defaults to true. |
| PYROSCOPE_PROFILING_EXCEPTION_ENABLED  | Boolean      | If set to true, enables the Exceptions profiling (beta). Defaults to false. |
| PYROSCOPE_PROFILING_ALLOCATION_ENABLED | Boolean      | If set to true, enables the Allocations profiling (beta). Defaults to false. |
| PYROSCOPE_PROFILING_LOCK_ENABLED       | Boolean      | If set to true, enables the Lock Contention profiling (beta). Defaults to false. |

## Copyright

Copyright (c) 2017 Datadog
[https://www.datadoghq.com](https://www.datadoghq.com/)

Copyright (c) 2022 Pyroscope

## License

See [license information](../LICENSE).

## Contact us

### Security Vulnerabilities

If you have found a security issue, please contact the security team directly at [security@pyroscope.io](mailto:security@pyroscope.io).
