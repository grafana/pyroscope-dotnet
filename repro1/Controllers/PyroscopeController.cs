using Microsoft.AspNetCore.Mvc;
using Pyroscope;

namespace PyroscopeApp.Controllers;

[ApiController]
[Route("[controller]")]
public class PyroscopeController : ControllerBase
{
    private static readonly object _lock = new object();

    [HttpGet]
    [Route("Test")]
    public async Task<IActionResult> PyroscopeTest()
    {
        var requestPath = HttpContext.Request.Path.Value;
        if (string.IsNullOrEmpty(requestPath))
        {
            requestPath = "unknown_endpoint";
        }

        lock (_lock)
        {
            var labels = LabelSet.Empty.BuildUpon()
                .Add("endpoint", requestPath)
                .Build();

            var testValue = 1000;
            LabelsWrapper.Do(labels, () =>
            {
                for (long i = 0; i < testValue * 1000000000; i++)
                {
                }
                SomeMethod();
            });
        }

        // Simulate some async work as mentioned in the issue about async code
        await Task.Delay(10);

        return Ok(new { message = "Pyroscope test completed", endpoint = requestPath });
    }

    [HttpGet]
    [Route("Car")]
    public IActionResult FibonacciTest([FromQuery] int n = 10)
    {
        var requestPath = HttpContext.Request.Path.Value;
        if (string.IsNullOrEmpty(requestPath))
        {
            requestPath = "unknown_endpoint";
        }

        long result = 0;
        lock (_lock)
        {
            var labels = LabelSet.Empty.BuildUpon()
                .Add("endpoint", requestPath)
                .Add("fibonacci_input", n.ToString())
                .Build();

            LabelsWrapper.Do(labels, () =>
            {
                result = CalculateFibonacci(n);
            });
        }

        return Ok(new { message = "Fibonacci test completed", input = n, result = result, endpoint = requestPath });
    }

    [HttpGet]
    [Route("Health")]
    public IActionResult Health()
    {
        return Ok(new { status = "healthy", timestamp = DateTime.UtcNow });
    }

    private void SomeMethod()
    {
        // Simulate some work
        Thread.Sleep(10);
    }

    private long CalculateFibonacci(int n)
    {
        if (n <= 1)
            return n;

        return CalculateFibonacci(n - 1) + CalculateFibonacci(n - 2);
    }
}