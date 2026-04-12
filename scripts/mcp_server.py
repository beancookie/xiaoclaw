#!/usr/bin/env python3
"""
MCP Server for ESP32 Bridge - Using FastMCP

A simplified MCP server implementation using the FastMCP library.

Usage:
    python scripts/mcp_server.py [--host HOST] [--port PORT] [--path PATH] [--http]

Examples:
    python scripts/mcp_server.py                           # stdio mode
    python scripts/mcp_server.py --http                    # HTTP mode on 0.0.0.0:8000
    python scripts/mcp_server.py --http --port 8080        # ESP32 client compatible
"""

import argparse
import logging
import sys
from datetime import datetime

from fastmcp import FastMCP

# Configure logging
logging.basicConfig(
    level=logging.DEBUG,
    format='%(asctime)s - %(levelname)s - %(message)s',
    stream=sys.stdout
)
logger = logging.getLogger("mcp_server")

# Initialize FastMCP server
mcp = FastMCP("esp32-bridge")


# ============ Tool Implementations ============

@mcp.tool()
def get_device_status() -> dict:
    """Get current device status including uptime, temperature, humidity, and memory"""
    logger.info("Tool called: get_device_status()")
    return {
        "status": "online",
        "uptime_seconds": 3600,
        "temperature": 36.5,
        "humidity": 65,
        "memory_free": 102400,
        "timestamp": datetime.now().isoformat(),
    }


@mcp.tool()
def set_led(color: str, brightness: int) -> dict:
    """Control LED with specified color and brightness (0-100)"""
    logger.info(f"Tool called: set_led(color={color}, brightness={brightness})")
    if brightness < 0 or brightness > 100:
        return {"error": "Brightness must be between 0 and 100"}
    return {
        "success": True,
        "color": color.lower(),
        "brightness": brightness,
        "applied_at": datetime.now().isoformat(),
    }


@mcp.tool()
def read_sensor(sensor_id: str, sensor_type: str = "temperature") -> dict:
    """Read sensor value by ID and type (temperature/humidity/pressure)"""
    logger.info(f"Tool called: read_sensor(sensor_id={sensor_id}, sensor_type={sensor_type})")
    readings = {
        "temperature": 23.5,
        "humidity": 55.0,
        "pressure": 1013.25,
    }
    value = readings.get(sensor_type.lower(), 0.0)
    units = {
        "temperature": "celsius",
        "humidity": "%",
        "pressure": "hPa",
    }
    return {
        "sensor_id": sensor_id,
        "sensor_type": sensor_type,
        "value": value,
        "unit": units.get(sensor_type.lower(), "unknown"),
        "timestamp": datetime.now().isoformat(),
    }


@mcp.tool()
def control_motor(speed: int, direction: str = "forward") -> dict:
    """Control motor with speed (0-100) and direction (forward/reverse)"""
    logger.info(f"Tool called: control_motor(speed={speed}, direction={direction})")
    if speed < 0 or speed > 100:
        return {"error": "Speed must be between 0 and 100"}
    if direction not in ("forward", "reverse"):
        return {"error": "Direction must be 'forward' or 'reverse'"}
    return {
        "success": True,
        "speed": speed,
        "direction": direction,
        "applied_at": datetime.now().isoformat(),
    }


@mcp.tool()
def get_time(timezone: str = "UTC") -> dict:
    """Get current time in specified timezone"""
    logger.info(f"Tool called: get_time(timezone={timezone})")
    now = datetime.now()
    return {
        "datetime": now.isoformat(),
        "unix_timestamp": int(now.timestamp()),
        "timezone": timezone,
    }


@mcp.tool()
def echo(message: str) -> dict:
    """Echo back the provided message for testing"""
    logger.info(f"Tool called: echo(message={message})")
    return {"echo": message, "received_at": datetime.now().isoformat()}


def main():
    parser = argparse.ArgumentParser(description="MCP Server for ESP32 Bridge")
    parser.add_argument("--host", default="0.0.0.0", help="Host to bind to (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8000, help="Port to listen on (default: 8000)")
    parser.add_argument("--path", default="/mcp", help="HTTP endpoint path (default: /mcp)")
    parser.add_argument("--http", action="store_true", help="Use HTTP transport instead of stdio")
    args = parser.parse_args()

    if args.http:
        # Run with HTTP transport for ESP32 client or direct JSON-RPC access
        logger.info(f"Starting MCP server HTTP on {args.host}:{args.port}{args.path}")
        mcp.run(transport="http", host=args.host, port=args.port, path=args.path)
    else:
        # Run with stdio transport for MCP protocol (Claude Code, etc.)
        logger.info("Starting MCP server in stdio mode")
        mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
