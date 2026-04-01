#!/usr/bin/env python3
"""
MCP Server for ESP32 Bridge

A simple MCP (Model Context Protocol) server that exposes tools
for the ESP32 mimi agent to call.

Usage:
    pip install "mcp[cli]" "uvicorn[standard]"
    python scripts/mcp_server.py

Example:
    python scripts/mcp_server.py
    # Server will start at http://0.0.0.0:8000/mcp
"""

import json
from datetime import datetime

from mcp.server.fastmcp import FastMCP

# Create FastMCP server instance with defaults
mcp = FastMCP("ESP32 Bridge", host="0.0.0.0", port=8000, streamable_http_path="/mcp")


@mcp.tool()
def get_device_status() -> str:
    """
    Get the current device status information.

    Returns:
        JSON string with device status including temperature, humidity, etc.
    """
    status = {
        "status": "online",
        "uptime_seconds": 3600,
        "temperature": 36.5,
        "humidity": 65,
        "memory_free": 102400,
        "timestamp": datetime.now().isoformat(),
    }
    return json.dumps(status)


@mcp.tool()
def set_led(color: str, brightness: int) -> str:
    """
    Control an LED attached to the ESP32.

    Args:
        color: LED color name (e.g., "red", "green", "blue", "white")
        brightness: Brightness level 0-100

    Returns:
        JSON string confirming the LED settings applied
    """
    if brightness < 0 or brightness > 100:
        return json.dumps({"error": "Brightness must be between 0 and 100"})

    result = {
        "success": True,
        "color": color.lower(),
        "brightness": brightness,
        "applied_at": datetime.now().isoformat(),
    }
    return json.dumps(result)


@mcp.tool()
def read_sensor(sensor_id: str, sensor_type: str = "temperature") -> str:
    """
    Read data from a sensor attached to the ESP32.

    Args:
        sensor_id: Unique identifier for the sensor
        sensor_type: Type of sensor ("temperature", "humidity", "pressure")

    Returns:
        JSON string with sensor reading
    """
    # Simulated sensor readings
    readings = {
        "temperature": 23.5,
        "humidity": 55.0,
        "pressure": 1013.25,
    }

    value = readings.get(sensor_type.lower(), 0.0)

    result = {
        "sensor_id": sensor_id,
        "sensor_type": sensor_type,
        "value": value,
        "unit": "celsius" if sensor_type == "temperature" else ("%" if sensor_type == "humidity" else "hPa"),
        "timestamp": datetime.now().isoformat(),
    }
    return json.dumps(result)


@mcp.tool()
def control_motor(speed: int, direction: str = "forward") -> str:
    """
    Control a DC motor attached to the ESP32.

    Args:
        speed: Motor speed 0-100
        direction: Direction ("forward" or "reverse")

    Returns:
        JSON string confirming motor settings
    """
    if speed < 0 or speed > 100:
        return json.dumps({"error": "Speed must be between 0 and 100"})

    if direction not in ("forward", "reverse"):
        return json.dumps({"error": "Direction must be 'forward' or 'reverse'"})

    result = {
        "success": True,
        "speed": speed,
        "direction": direction,
        "applied_at": datetime.now().isoformat(),
    }
    return json.dumps(result)


@mcp.tool()
def get_time() -> str:
    """
    Get the current server time.

    Returns:
        JSON string with current datetime
    """
    result = {
        "datetime": datetime.now().isoformat(),
        "unix_timestamp": int(datetime.now().timestamp()),
        "timezone": "UTC",
    }
    return json.dumps(result)


@mcp.tool()
def echo(message: str) -> str:
    """
    Echo back a message (for testing connectivity).

    Args:
        message: Message to echo back

    Returns:
        The same message wrapped in JSON
    """
    return json.dumps({"echo": message, "received_at": datetime.now().isoformat()})


def main():
    print(f"Starting MCP Server: http://0.0.0.0:8000/mcp")
    print(f"Tools available:")
    for tool in ["get_device_status", "set_led", "read_sensor", "control_motor", "get_time", "echo"]:
        print(f"  - {tool}")
    print()
    print("Press Ctrl+C to stop the server")
    print()

    # Run the server (transport configured in constructor)
    mcp.run()


if __name__ == "__main__":
    main()
