# Unified Runtime tracing tool

urtrace is a command-line tool for tracing and profiling Unified Runtime library
calls in a process. It can be used to inspect the function arguments of each call,
and provides options for filtering, output formatting, and profiling.

The tool is implemented in Python as a CLI, which uses UR's XPTI-based tracing layer
to register the subscriber to UR function traces. The XPTI subscriber is written in C++ and
is responsible for handling the performance-sensitive aspects of the tool,
such as profiling and function tracing. The necessary arguments from the
Python CLI to the C++ collector are passed through an environment variable.

The urtrace tool requires unified runtime loader to be built with XPTI support
and that the xptifw is present in the system in a location where urtrace can find
it. The locations where the tool looks for dynamic libraries, such as xptifw, can
be specified with the option `--libpath`.

See [XPTI framework github repository](https://github.com/intel/llvm/tree/sycl/xptifw) for more information.

## Examples

See `urtrace --help` to get detailed information on its usage.
Here are a few examples:

### Trace all UR calls made by `./myapp --my-arg`
urtrace ./myapp --my-arg

### Trace and profile only UR functions that match the regex ".*(Device|Platform).*"
urtrace --profiling --filter ".*(Device|Platform).*" ./hello_world

### Use a custom adapter and also trace function begins
urtrace --adapter libur_adapter_cuda.so --begin ./sycl_app

### Force load the null adapter and look for it in a custom path
urtrace --null --libpath /opt/custom/ ./foo
