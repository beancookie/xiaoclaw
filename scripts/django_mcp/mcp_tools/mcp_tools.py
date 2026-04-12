"""
MCP Tools for ESP32 Bridge

Register tools using registry directly.
"""
import json
from datetime import datetime
from mcp_server.registry import registry


def get_device_status() -> dict:
    """Get current device status including uptime, temperature, humidity, and memory"""
    return {
        "status": "online",
        "uptime_seconds": 3600,
        "temperature": 36.5,
        "humidity": 65,
        "memory_free": 102400,
        "timestamp": datetime.now().isoformat(),
    }


def set_led(color: str, brightness: int) -> dict:
    """Control LED with specified color and brightness (0-100)"""
    if brightness < 0 or brightness > 100:
        return {"error": "Brightness must be between 0 and 100"}
    return {
        "success": True,
        "color": color.lower(),
        "brightness": brightness,
        "applied_at": datetime.now().isoformat(),
    }


def read_sensor(sensor_id: str, sensor_type: str = "temperature") -> dict:
    """Read sensor value by ID and type (temperature/humidity/pressure)"""
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


def control_motor(speed: int, direction: str = "forward") -> dict:
    """Control motor with speed (0-100) and direction (forward/reverse)"""
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


def get_time(timezone: str = "UTC") -> dict:
    """Get current time in specified timezone"""
    now = datetime.now()
    return {
        "datetime": now.isoformat(),
        "unix_timestamp": int(now.timestamp()),
        "timezone": timezone,
    }


def echo(message: str) -> dict:
    """Echo back the provided message for testing"""
    return {"echo": message, "received_at": datetime.now().isoformat()}


# Register all tools
registry.register_tool("get_device_status", get_device_status,
    "Get current device status including uptime, temperature, humidity, and memory",
    {"type": "object", "properties": {}}, False, None)

registry.register_tool("set_led", set_led,
    "Control LED with specified color and brightness (0-100)",
    {"type": "object", "properties": {"color": {"type": "string"}, "brightness": {"type": "integer"}}, "required": ["color", "brightness"]},
    False, None)

registry.register_tool("read_sensor", read_sensor,
    "Read sensor value by ID and type (temperature/humidity/pressure)",
    {"type": "object", "properties": {"sensor_id": {"type": "string"}, "sensor_type": {"type": "string"}}, "required": ["sensor_id"]},
    False, None)

registry.register_tool("control_motor", control_motor,
    "Control motor with speed (0-100) and direction (forward/reverse)",
    {"type": "object", "properties": {"speed": {"type": "integer"}, "direction": {"type": "string"}}, "required": ["speed"]},
    False, None)

registry.register_tool("get_time", get_time,
    "Get current time in specified timezone",
    {"type": "object", "properties": {"timezone": {"type": "string"}}, "required": []},
    False, None)

registry.register_tool("echo", echo,
    "Echo back the provided message for testing",
    {"type": "object", "properties": {"message": {"type": "string"}}, "required": ["message"]},
    False, None)
