#  Pyroscope .NET Application

This is a sample .NET 8 Web API application configured with Pyroscope profiling integration, based on the GitHub issue [grafana/support-escalations#18286](https://github.com/grafana/support-escalations/issues/18286).

## Features

- .NET 8.0 Web API
- Pyroscope profiling integration (version 0.12.0)
- OpenTelemetry tracing with Pyroscope span processor
- Docker containerization with proper Pyroscope native libraries
- Test endpoints for profiling verification

## Quick Start

### Using Makefile (Recommended)

The project includes a Makefile for easy development workflow:

```bash
# View all available commands
make help

# Build and run with Docker (includes Pyroscope server)
make docker-run

# Test the application endpoints
make test-endpoints

# Generate load for profiling
make load-test

# Stop Docker containers
make docker-stop

# Clean up Docker resources
make docker-clean
```

### Using Docker Compose (with Local Pyroscope Server)

1. Clone this repository and navigate to the project directory
2. Build and run both the .NET application and Pyroscope server:
   ```bash
   docker compose up --build
   ```

3. The services will be available at:
   - .NET Application: `http://localhost:8080`
   - Pyroscope UI: `http://localhost:4040`

### Using Docker Compose (with External Pyroscope Server)

1. Copy the environment file and configure your settings:
   ```bash
   cp .env .env.local
   # Edit .env.local with your actual Pyroscope server details
   ```

2. Update the `PYROSCOPE_SERVER_ADDRESS` in `docker-compose.yml` to point to your external Pyroscope server
3. Build and run the application:
   ```bash
   docker compose up --build
   ```

### Testing Pyroscope Integration

Once the application is running, you can test the Pyroscope integration:

1. **Health Check**: `GET http://localhost:8080/Pyroscope/Health`
2. **Pyroscope Test Endpoint**: `GET http://localhost:8080/Pyroscope/Test`
3. **Pyroscope UI**: Visit `http://localhost:4040` to view the profiling data

The test endpoint will:
- Create a CPU-intensive workload with async operations
- Apply custom labels (`endpoint` with the request path)
- Generate profiling data that should appear in your Pyroscope dashboard

Generate load to see profiling data:
```bash
# Generate multiple requests to create profiling data
for i in {1..10}; do curl http://localhost:8080/Pyroscope/Test; done
```

## Configuration

### Environment Variables

The following environment variables are configured in `docker-compose.yml`:

- `PYROSCOPE_APPLICATION_NAME`: Application name in Pyroscope (set to `napiv1`)
- `PYROSCOPE_LABELS`: Custom labels for profiling data
- `PYROSCOPE_SERVER_ADDRESS`: Your Grafana Cloud Pyroscope endpoint
- `PYROSCOPE_PROFILING_ENABLED`: Enable/disable profiling
- Various profiling type enablement flags

### Pyroscope Configuration

The application is configured with:
- CPU tracking
- Memory allocation tracking
- Lock contention tracking
- Exception tracking
- OpenTelemetry span processor integration

## Project Structure

```
.
├── Controllers/
│   └── PyroscopeController.cs    # Test endpoints
├── Program.cs                    # Application startup and configuration
├── PyroscopeApp.csproj   # Project file with dependencies
├── Dockerfile                   # Docker configuration with Pyroscope libraries
├── docker-compose.yml           # Docker Compose setup
├── .env                         # Environment variables template
└── README.md                    # This file
```

## Troubleshooting

If labels are not appearing in Pyroscope:

1. Check the application logs for Pyroscope-related messages
2. Verify that the Pyroscope native libraries are correctly loaded
3. Ensure the `PYROSCOPE_SERVER_ADDRESS` and authentication are correct
4. Test the `/Pyroscope/Test` endpoint to generate profiling data
5. Check that labels are properly formatted in the environment variables

## Dependencies

- .NET 8.0
- Pyroscope 0.12.0
- Pyroscope.OpenTelemetry 0.3.0
- OpenTelemetry packages for ASP.NET Core integration